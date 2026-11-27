// Reads a PE32+ file (Kenshi's exe) for scripts/sites.mjs: sections, RVA <-> file offset, masked
// pattern search, RTTI vtable lookup. Read-only; the file is never changed.
import { readFileSync } from "node:fs";
import { createHash } from "node:crypto";

export class PeImage {
  constructor(path) {
    this.path = path;
    this.buf = readFileSync(path);
    this.sha256 = createHash("sha256").update(this.buf).digest("hex");
    const b = this.buf;
    if (b.readUInt16LE(0) !== 0x5a4d) throw new Error(`${path}: not an MZ file`);
    const pe = b.readUInt32LE(0x3c);
    if (b.readUInt32LE(pe) !== 0x4550) throw new Error(`${path}: no PE header`);
    const nSec = b.readUInt16LE(pe + 6);
    const optSize = b.readUInt16LE(pe + 20);
    const opt = pe + 24;
    if (b.readUInt16LE(opt) !== 0x20b) throw new Error(`${path}: not PE32+`);
    this.imageBase = b.readBigUInt64LE(opt + 24);
    this.sections = [];
    for (let i = 0; i < nSec; i++) {
      const s = opt + optSize + 40 * i;
      this.sections.push({
        name: b.toString("latin1", s, s + 8).replace(/\0+$/, ""),
        vsize: b.readUInt32LE(s + 8), rva: b.readUInt32LE(s + 12),
        rawSize: b.readUInt32LE(s + 16), raw: b.readUInt32LE(s + 20),
      });
    }
    const text = this.section(".text");
    this.text = { rva: text.rva, end: text.rva + Math.min(text.vsize, text.rawSize), raw: text.raw };
  }

  section(name) {
    const s = this.sections.find((x) => x.name === name);
    if (!s) throw new Error(`${this.path}: no ${name} section`);
    return s;
  }

  sectionOfRva(rva) {
    return this.sections.find((s) => rva >= s.rva && rva < s.rva + Math.max(s.vsize, s.rawSize)) || null;
  }

  // File offset of an RVA, or -1 if it is not backed by file bytes.
  off(rva) {
    const s = this.sectionOfRva(rva);
    if (!s || rva - s.rva >= s.rawSize) return -1;
    return s.raw + (rva - s.rva);
  }

  rvaOfOff(off) {
    const s = this.sections.find((x) => off >= x.raw && off < x.raw + x.rawSize);
    return s ? s.rva + (off - s.raw) : -1;
  }

  bytes(rva, n) {
    const o = this.off(rva);
    if (o < 0 || o + n > this.buf.length) throw new Error(`rva 0x${rva.toString(16)} not in the file`);
    return this.buf.subarray(o, o + n);
  }

  inText(rva) { return rva >= this.text.rva && rva < this.text.end; }

  // Does `pat` ({bytes, mask}) match at rva? mask[i] = 1 compares the byte, 0 ignores it.
  matchAt(rva, pat) {
    const o = this.off(rva);
    if (o < 0 || o + pat.bytes.length > this.buf.length) return false;
    for (let i = 0; i < pat.bytes.length; i++) if (pat.mask[i] && this.buf[o + i] !== pat.bytes[i]) return false;
    return true;
  }

  // RVAs in .text where the masked pattern matches; [lo, hi) limits the search (RVAs), max stops early.
  findAll(pat, { lo = this.text.rva, hi = this.text.end, max = Infinity } = {}) {
    lo = Math.max(lo, this.text.rva);
    hi = Math.min(hi, this.text.end);
    const { start, len } = longestRun(pat.mask);
    if (len === 0) throw new Error("pattern has no fixed bytes");
    const needle = Buffer.from(pat.bytes.subarray(start, start + len));
    const base = this.text.raw - this.text.rva;   // file offset = rva + base inside .text
    const out = [];
    let from = lo + start + base;
    const stop = hi + base;
    for (;;) {
      const at = this.buf.indexOf(needle, from);
      if (at < 0 || at >= stop) break;
      const rva = at - base - start;
      if (rva >= lo && rva + pat.bytes.length <= hi && this.matchAt(rva, pat)) {
        out.push(rva);
        if (out.length >= max) break;
      }
      from = at + 1;
    }
    return out;
  }

  // Primary vtable RVA of a class by its RTTI type name (e.g. ".?AVCharacter@@"), or null.
  vtableOf(typeName) {
    const name = Buffer.from(typeName + "\0", "latin1");
    const hits = [];
    for (let at = this.buf.indexOf(name); at >= 0; at = this.buf.indexOf(name, at + 1)) hits.push(at);
    if (hits.length !== 1) return null;
    const tdRva = this.rvaOfOff(hits[0]) - 16;         // TypeDescriptor: pVFTable, spare, name
    const rdata = this.section(".rdata");
    const base = rdata.raw, end = rdata.raw + rdata.rawSize;
    const cols = [];
    for (let o = base + 12; o + 12 <= end; o += 4) {   // COL: sig, offset, cdOffset, pTD, pCHD, pSelf
      if (this.buf.readInt32LE(o) !== tdRva) continue;
      const col = o - 12;
      if (this.buf.readUInt32LE(col) !== 1 || this.buf.readUInt32LE(col + 4) !== 0) continue;
      if (this.buf.readUInt32LE(col + 20) !== this.rvaOfOff(col)) continue;
      cols.push(this.rvaOfOff(col));
    }
    if (cols.length !== 1) return null;
    const va = this.imageBase + BigInt(cols[0]);
    const vas = [];
    for (let o = base; o + 8 <= end; o += 8) if (this.buf.readBigUInt64LE(o) === va) vas.push(o);
    if (vas.length !== 1) return null;
    return this.rvaOfOff(vas[0]) + 8;
  }

  // Function RVA in vtable slot `slotOff` (bytes) of the vtable at vtRva. Kenshi is linked
  // incrementally: slots point to `jmp rel32` thunks, which are followed (measured, Character vtable).
  vtableSlot(vtRva, slotOff) {
    const rva = Number(this.bytes(vtRva + slotOff, 8).readBigUInt64LE(0) - this.imageBase);
    return this.followJmp(rva);
  }

  followJmp(rva) {
    const b = this.bytes(rva, 5);
    return b[0] === 0xe9 ? rva + 5 + b.readInt32LE(1) : rva;
  }
}

export function longestRun(mask) {
  let best = { start: 0, len: 0 }, cur = 0;
  for (let i = 0; i < mask.length; i++) {
    cur = mask[i] ? cur + 1 : 0;
    if (cur > best.len) best = { start: i - cur + 1, len: cur };
  }
  return best;
}

// "48 8B ?? 05" <-> {bytes, mask}
export function parsePattern(text) {
  const parts = text.trim().split(/\s+/).filter(Boolean);
  const bytes = new Uint8Array(parts.length), mask = new Uint8Array(parts.length);
  parts.forEach((p, i) => {
    if (p === "??") return;
    if (!/^[0-9A-Fa-f]{2}$/.test(p)) throw new Error(`bad pattern byte "${p}"`);
    bytes[i] = parseInt(p, 16);
    mask[i] = 1;
  });
  return { bytes, mask };
}

export function formatPattern(pat) {
  return Array.from(pat.bytes, (b, i) => (pat.mask[i] ? b.toString(16).toUpperCase().padStart(2, "0") : "??")).join(" ");
}

export const hex = (n) => (n < 0 ? "-0x" + (-n).toString(16) : "0x" + n.toString(16));
