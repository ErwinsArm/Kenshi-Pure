#!/usr/bin/env node
// KenshiPure's address table client/sites.tsv: add rows, re-prove them, generate the C++ side.
//
//   node scripts/sites.mjs add <name> --kind code   --sym <mangled> [--need required|optional:<feature>]
//   node scripts/sites.mjs add <name> --kind global --sym <mangled>                       (anchor found automatically)
//   node scripts/sites.mjs add <name> --kind field  --field Class::member --via <mangled method of Class>
//   node scripts/sites.mjs add <name> --kind vslot  --sym <mangled virtual> --method Class::method --type .?AVClass@@
//   node scripts/sites.mjs check            re-prove every row in both exes; exit 1 on any difference
//   node scripts/sites.mjs gen <dir>        write kp_sites_gen.h / .cpp / kp_thunks.asm for the build
// Options: --game <Kenshi folder> (or KENSHI_DIR; default: Steam's default folder),
//          --exe65 <path> --exe68 <path> to name the exes directly.
//
// Rules (docs/plan-own-loader.md, "Address layer"):
//   - 1.0.65 values come from KenshiLib 0.5.0 (address map, header offsets) and are checked in the exe.
//   - Patterns are made here from the exes (operands that move are masked with the iced-x86 decoder);
//     each must match exactly once in .text of both exes.
//   - Shift check: a 1.0.68 anchor must have the same shift as its matched neighbour functions on at
//     least one side, and keep their order; otherwise the row is rejected even if the match is unique.
import { readFileSync, writeFileSync, existsSync, mkdirSync } from "node:fs";
import { join, dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { PeImage, parsePattern, formatPattern, hex } from "./lib/pe.mjs";
import { KenshiLibMap, headerFieldOffset, headerVtableOffset } from "./lib/kenshilib.mjs";
import { loadIced, decodeAt, maskFor } from "./lib/x64.mjs";

const REPO = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const TSV = join(REPO, "client", "sites.tsv");
const KL_SRC = join(REPO, "tools", "build", "kenshilib-src", "KenshiLib-0.5.0");
const KL_RVAS = join(REPO, "tools", "build", "kenshilib", "RE_Kenshi", "RVAs", "Steam_1.0.65.br");
const KINDS = ["code", "global", "field", "vslot", "patch", "callsite", "data"];
const COLS = ["name", "kind", "module", "need", "sym", "1.0.65", "1.0.68", "pattern", "site", "read", "expect", "evidence"];
const MIN_LEN = 16, MAX_LEN = 64;

// ---------------------------------------------------------------- table I/O

export function readTable(path = TSV) {
  const text = readFileSync(path, "utf8");
  const head = [], builds = [], rows = [];
  let cols = null;
  for (const line of text.split(/\r?\n/)) {
    if (!line.trim()) continue;
    if (line.startsWith("#")) {
      head.push(line);
      const m = line.match(/^#build\s+(\S+)\s+([0-9a-f]{64})\s*(.*)$/);
      if (m) builds.push({ label: m[1], sha256: m[2], note: m[3] });
      continue;
    }
    const f = line.split("\t");
    if (!cols) { cols = f; if (cols.join() !== COLS.join()) throw new Error(`sites.tsv columns must be: ${COLS.join(" ")}`); continue; }
    const r = {};
    cols.forEach((c, i) => (r[c] = f[i] ?? ""));
    rows.push(r);
  }
  return { head, builds, rows };
}

export function writeTable(t, path = TSV) {
  const out = [...t.head, COLS.join("\t"), ...t.rows.map((r) => COLS.map((c) => r[c] ?? "").join("\t"))];
  writeFileSync(path, out.join("\n") + "\n");
}

// "0x70@0x643920" -> {value, anchor}; "0x787e70" -> {value, anchor: value}; "" -> null
export function parseCell(s) {
  if (!s) return null;
  const [v, a] = s.split("@");
  const value = parseInt(v, 16) * (v.startsWith("-") ? -1 : 1);
  return { value, anchor: a ? parseInt(a, 16) : null };
}
const cell = (value, anchor) => (anchor === null || anchor === undefined ? hex(value) : `${hex(value)}@${hex(anchor)}`);

// ---------------------------------------------------------------- exes

function opt(args, name, def = null) {
  const i = args.indexOf("--" + name);
  return i >= 0 ? args[i + 1] : def;
}

function openExes(args, builds) {
  const game = opt(args, "game", process.env.KENSHI_DIR ||
    join(process.env["ProgramFiles(x86)"] || "C:\\Program Files (x86)", "Steam", "steamapps", "common", "Kenshi"));
  const p65 = opt(args, "exe65", join(game, "RE_Kenshi", "Kenshi_x64.exe"));
  const p68 = opt(args, "exe68", join(game, "kenshi_x64.exe"));
  for (const p of [p65, p68]) if (!existsSync(p)) throw new Error(`exe not found: ${p} (pass --game <Kenshi folder>)`);
  const e65 = new PeImage(p65), e68 = new PeImage(p68);
  const want = Object.fromEntries(builds.map((b) => [b.label, b.sha256]));
  if (e65.sha256 !== want["1.0.65"]) throw new Error(`${p65} is not the 1.0.65 build of sites.tsv (sha256 ${e65.sha256})`);
  if (e68.sha256 !== want["1.0.68"]) throw new Error(`${p68} is not the 1.0.68 build of sites.tsv (sha256 ${e68.sha256})`);
  return { e65, e68 };
}

// ---------------------------------------------------------------- patterns

// Pattern starting at `start` (an instruction boundary) in exe `img`, covering at least `cover`
// bytes, unique in .text of img (at `start`) and of `other` (any place). `read` = [off, size] of an
// operand (relative to start) that must be masked because it is the value we read.
// Returns {pat, hits} with hits = matches in `other`, or throws.
function makePattern(img, other, start, { cover = 0, read = null } = {}) {
  const insns = decodeAt(img, start, MAX_LEN + 16);
  const tries = [];
  for (const allDisp of [false, true]) {
    let len = 0;
    for (const i of insns) {
      len = i.rva + i.len - start;
      if (len < Math.max(MIN_LEN, cover)) continue;
      if (len > MAX_LEN) break;
      const mask = maskFor(insns, start, len, { allDisp, extra: read ? [read] : [] });
      const pat = { bytes: new Uint8Array(img.bytes(start, len)), mask };
      const own = img.findAll(pat, { max: 2 });
      if (own.length !== 1 || own[0] !== start) continue;
      const hits = other.findAll(pat, { max: 3 });
      tries.push({ allDisp, len, hits: hits.length });
      if (hits.length === 1) return { pat, hits, allDisp };
      if (hits.length === 0) break;     // longer will not appear; try masking field offsets
    }
  }
  throw new Error(`no pattern at ${hex(start)} that is unique in both exes (${JSON.stringify(tries)})`);
}

// ---------------------------------------------------------------- shift check

let neighbourCache = null;
function neighbourShift(kl, e65, e68, rva) {
  neighbourCache ??= new Map();
  const all = kl.codeRvas();
  const shiftOf = (n) => {
    if (neighbourCache.has(n)) return neighbourCache.get(n);
    let r = null;
    try {
      const insns = decodeAt(e65, n, 48);
      let len = 0;
      for (const i of insns) {
        len = i.rva + i.len - n;
        if (len < 12) continue;
        const pat = { bytes: new Uint8Array(e65.bytes(n, len)), mask: maskFor(insns, n, len) };
        const own = e65.findAll(pat, { lo: n - 0x40000, hi: n + 0x40000, max: 2 });
        if (own.length !== 1) continue;
        const h = e68.findAll(pat, { lo: n - 0x10000, hi: n + 0x10000, max: 2 });
        if (h.length === 1) r = { n65: n, n68: h[0], shift: h[0] - n };
        break;
      }
    } catch { r = null; }
    neighbourCache.set(n, r);
    return r;
  };
  let lo = 0, hi = all.length;
  while (lo < hi) { const m = (lo + hi) >> 1; if (all[m] < rva) lo = m + 1; else hi = m; }
  const before = [], after = [];
  for (let i = lo - 1; i >= 0 && before.length < 4 && lo - i < 40; i--) { const s = shiftOf(all[i]); if (s) before.push(s); }
  for (let i = all[lo] === rva ? lo + 1 : lo; i < all.length && after.length < 4 && i - lo < 40; i++) { const s = shiftOf(all[i]); if (s) after.push(s); }
  return { before, after };
}

// Throws unless anchor a68 has its neighbours' block shift and keeps their order.
function shiftCheck(kl, e65, e68, a65, a68) {
  const { before, after } = neighbourShift(kl, e65, e68, a65);
  const s = a68 - a65;
  const side = (l) => l.length >= 2 && l.every((x) => x.shift === s);
  const orderOk = (before.length === 0 || Math.max(...before.map((x) => x.n68)) < a68) &&
                  (after.length === 0 || Math.min(...after.map((x) => x.n68)) > a68);
  const describe = (l) => l.map((x) => hex(x.shift)).join(",") || "none";
  if (!orderOk) throw new Error(`order check failed at ${hex(a65)} -> ${hex(a68)} (before ${describe(before)}, after ${describe(after)})`);
  if (side(before) || side(after)) {
    const n = (side(before) ? before.length : 0) + (side(after) ? after.length : 0);
    return `shift ${hex(s)} = ${n} neighbour functions, order kept (before ${describe(before)}; after ${describe(after)})`;
  }
  // Functions also change size inside a block (measured: GameWorld::_NV_mainLoop_GPUSensitiveStuff
  // +0xb90 between neighbours at +0xb40 and +0xba0). Then: each side's neighbours agree among
  // themselves and the anchor lies between the nearest ones in both exes.
  const agree = (l) => l.length >= 2 && l.every((x) => x.shift === l[0].shift);
  if (agree(before) && agree(after) && s >= Math.min(before[0].shift, after[0].shift) && s <= Math.max(before[0].shift, after[0].shift))
    return `shift ${hex(s)} between its neighbours' blocks (before ${describe(before)}; after ${describe(after)}), order kept`;
  throw new Error(`shift ${hex(s)} at ${hex(a65)} does not fit its neighbours (before ${describe(before)}, after ${describe(after)})`);
}

// ---------------------------------------------------------------- proving one row

// Finds the instruction in method `via` (65) that reads/writes this->member at `off`: memory base is
// `this` (rcx at entry, or a register it was copied to before rcx is overwritten).
function findFieldAnchor(e65, via, off) {
  const insns = decodeAt(e65, via, 0x400);
  const R = loadIced(REPO).Register;
  const vol = [R.RAX, R.RCX, R.RDX, R.R8, R.R9, R.R10, R.R11];
  let thisRegs = new Set([R.RCX]);
  for (const i of insns) {
    // Offset 0 (first member of a class without vtable): a memory operand with no displacement at all.
    if (!i.ipRel && thisRegs.has(i.memBase) && i.memIndex === 0 && i.disp === off && (i.dispSize > 0 || (off === 0 && i.mnemonic !== loadIced(REPO).Mnemonic.Lea))) return i;
    if (i.movRegReg && thisRegs.has(i.movRegReg[1])) { thisRegs.add(i.movRegReg[0]); continue; }
    for (const w of i.writes) thisRegs.delete(w);
    if (i.flow === 5 || i.flow === 6) for (const v of vol) thisRegs.delete(v);
    if (i.flow === 4 || i.flow === 1 || i.flow === 2) break;   // ret / jmp: end of the linear part
    if (thisRegs.size === 0) break;
  }
  throw new Error(`no this->+${hex(off)} access in the linear start of ${hex(via)}`);
}

// Finds an instruction in a KenshiLib-mapped function that references `target` RIP-relatively.
function findRipAnchor(e65, kl, target) {
  const t = e65.text;
  const buf = e65.buf, base = t.raw - t.rva;
  const fns = kl.codeRvas();
  const found = [];
  for (let o = t.raw; o < t.raw + (t.end - t.rva) - 4 && found.length < 40; o++) {
    const rva = o - base;
    const d = buf.readInt32LE(o);
    if (![0, 1, 2, 4].some((tail) => rva + 4 + tail + d === target)) continue;
    // A candidate disp32 at rva: decode its function linearly from the start, so the instruction
    // boundary is the real one (decoding a few bytes back can start inside the instruction).
    let lo = 0, hi = fns.length;
    while (lo < hi) { const m = (lo + hi) >> 1; if (fns[m] <= rva) lo = m + 1; else hi = m; }
    if (lo === 0 || rva - fns[lo - 1] > 0x1000) continue;
    const ins = decodeAt(e65, fns[lo - 1], rva - fns[lo - 1] + 16).find((i) => i.rva + i.dispOff === rva && i.dispSize === 4);
    if (ins && ins.ipRel && ins.ipTarget === target) found.push(ins);
  }
  if (!found.length) throw new Error(`no RIP-relative reference to ${hex(target)} inside a mapped function`);
  return found;
}

function readOperand(img, anchor, read) {
  const [kind, a, b] = read.split(":");
  if (kind === "rip") return anchor + Number(b) + img.bytes(anchor + Number(a), 4).readInt32LE(0);
  if (kind === "disp") return Number(b) === 1 ? img.bytes(anchor + Number(a), 1).readInt8(0) : img.bytes(anchor + Number(a), 4).readInt32LE(0);
  throw new Error(`unknown read ${read}`);
}

// Computes a full row from its identity (name, kind, sym, field/method info) and proves it.
function proveRow(spec, ctx) {
  const { kl, e65, e68 } = ctx;
  const row = { name: spec.name, kind: spec.kind, module: "exe", need: spec.need || "required", sym: spec.sym || "", expect: "" };
  const ev = [];
  if (spec.kind === "code") {
    const a65 = kl.get(spec.sym).rva;
    const { pat, hits } = makePattern(e65, e68, a65);
    ev.push(`65: KenshiLib map`, `68: ${shiftCheck(kl, e65, e68, a65, hits[0])}`);
    Object.assign(row, { "1.0.65": cell(a65), "1.0.68": cell(hits[0]), pattern: formatPattern(pat), site: "0", read: "" });
  } else if (spec.kind === "global" || spec.kind === "data") {
    const target = kl.get(spec.sym).rva;
    let last = null;
    for (const ins of findRipAnchor(e65, kl, target)) {
      try {
        const read = [ins.dispOff, 4];
        const { pat, hits } = makePattern(e65, e68, ins.rva, { cover: ins.len, read });
        const r = `rip:${ins.dispOff}:${ins.len}`;
        const v68 = readOperand(e68, hits[0], r);
        const sec65 = e65.sectionOfRva(target)?.name, sec68 = e68.sectionOfRva(v68)?.name;
        if (sec65 !== sec68) throw new Error(`target section ${sec65} vs ${sec68}`);
        ev.push(`65: KenshiLib map; anchor ${hex(ins.rva)} reads it`, `68: anchor ${shiftCheck(kl, e65, e68, ins.rva, hits[0])}; target in ${sec68}`);
        Object.assign(row, { "1.0.65": cell(target, ins.rva), "1.0.68": cell(v68, hits[0]), pattern: formatPattern(pat), site: "0", read: r });
        last = null;
        break;
      } catch (e) { last = e; }
    }
    if (last) throw last;
  } else if (spec.kind === "field") {
    const [cls, member] = splitMember(spec.field);
    const off = headerFieldOffset(join(KL_SRC, "Include"), cls, member);
    if (!spec.via) {
      // No method touches it through `this`: 1.0.65 from the header (the build's static_assert
      // checks the header), 1.0.68 left empty = unresolved there, so its feature is off on 1.0.68.
      ev.push("65: header offset only (no anchor)", "68: not proven");
      Object.assign(row, { sym: `${cls}|${member}|`, "1.0.65": cell(off), "1.0.68": "", pattern: "", site: "", read: "" });
      row.evidence = ev.join("; ");
      return row;
    }
    const via = kl.get(spec.via).rva;
    const ins = findFieldAnchor(e65, via, off);
    const read = ins.dispSize ? [ins.dispOff, ins.dispSize] : null;
    const { pat, hits } = makePattern(e65, e68, ins.rva, { cover: ins.len, read });
    // no displacement byte (offset 0): the same instruction bytes in 1.0.68 mean offset 0 there too
    const r = ins.dispSize ? `disp:${ins.dispOff}:${ins.dispSize}` : "disp0";
    const v68 = ins.dispSize ? readOperand(e68, hits[0], r) : 0;
    ev.push(`65: header offset; this->${member} in ${spec.via} at ${hex(ins.rva)}`, `68: anchor ${shiftCheck(kl, e65, e68, ins.rva, hits[0])}`);
    Object.assign(row, { sym: `${cls}|${member}|${spec.via}`, "1.0.65": cell(off, ins.rva), "1.0.68": cell(v68, hits[0]), pattern: formatPattern(pat), site: "0", read: r });
  } else if (spec.kind === "vslot") {
    const [cls, method] = splitMember(spec.method);
    const off = headerVtableOffset(join(KL_SRC, "Include"), cls, method);
    const f65 = kl.get(spec.sym).rva;
    const vt65 = e65.vtableOf(spec.type), vt68 = e68.vtableOf(spec.type);
    if (vt65 === null || vt68 === null) throw new Error(`RTTI vtable of ${spec.type} not found`);
    if (e65.vtableSlot(vt65, off) !== f65) throw new Error(`65: vtable ${spec.type}+${hex(off)} is ${hex(e65.vtableSlot(vt65, off))}, KenshiLib says ${hex(f65)}`);
    const { pat, hits } = makePattern(e65, e68, f65);
    const s = shiftCheck(kl, e65, e68, f65, hits[0]);
    if (e68.vtableSlot(vt68, off) !== hits[0]) throw new Error(`68: vtable ${spec.type}+${hex(off)} is ${hex(e68.vtableSlot(vt68, off))}, the function matched at ${hex(hits[0])}`);
    ev.push(`65: header slot; vtable ${spec.type} slot = KenshiLib's ${hex(f65)}`, `68: slot holds the matched function (${s})`);
    Object.assign(row, { sym: `${cls}|${method}|${spec.sym}|${spec.type}`, "1.0.65": cell(off, vt65), "1.0.68": cell(off, vt68), pattern: formatPattern(pat), site: "0", read: "vt" });
  } else {
    throw new Error(`kind ${spec.kind}: add it by hand (not automated yet)`);
  }
  row.evidence = ev.join("; ");
  return row;
}

function splitMember(q) {
  const i = q.lastIndexOf("::");
  if (i < 0) throw new Error(`expected Class::member, got ${q}`);
  return [q.slice(0, i), q.slice(i + 2)];
}

function specOf(row) {
  const s = { name: row.name, kind: row.kind, need: row.need, sym: row.sym };
  if (row.kind === "field") { const [c, m, v] = row.sym.split("|"); Object.assign(s, { field: `${c}::${m}`, via: v }); }
  if (row.kind === "vslot") { const [c, m, sym, t] = row.sym.split("|"); Object.assign(s, { method: `${c}::${m}`, sym, type: t }); }
  return s;
}

// ---------------------------------------------------------------- commands

function context(args, t) {
  loadIced(REPO);
  const kl = new KenshiLibMap(KL_SRC, KL_RVAS);
  return { kl, ...openExes(args, t.builds) };
}

function cmdAdd(args) {
  const t = readTable();
  const name = args[0];
  const spec = { name, kind: opt(args, "kind"), need: opt(args, "need", "required"), sym: opt(args, "sym", ""),
                 field: opt(args, "field"), via: opt(args, "via"), method: opt(args, "method"), type: opt(args, "type") };
  if (!KINDS.includes(spec.kind)) throw new Error(`--kind must be one of ${KINDS.join(", ")}`);
  const row = proveRow(spec, context(args, t));
  const i = t.rows.findIndex((r) => r.name === name);
  if (i >= 0) t.rows[i] = row; else t.rows.push(row);
  writeTable(t);
  console.log(`${i >= 0 ? "replaced" : "added"} ${name}: 1.0.65 ${row["1.0.65"]}, 1.0.68 ${row["1.0.68"]}\n  ${row.evidence}`);
}

// Lists methods of Class (KenshiLib map) whose linear start touches this->member: --via candidates.
function cmdVias(args) {
  const t = readTable();
  const ctx = context(args, t);
  const [cls, member] = splitMember(args[0]);
  const off = headerFieldOffset(join(KL_SRC, "Include"), cls, member);
  let n = 0;
  for (const [name, e] of ctx.kl.byName) {
    if (e.kind !== "code" || e.rva <= 0 || !e.decl.startsWith(cls + "::") || e.decl.slice(cls.length + 2).includes("::")) continue;
    try {
      const ins = findFieldAnchor(ctx.e65, e.rva, off);
      console.log(`${name}\t${hex(ins.rva)}\t${e.decl}`);
      if (++n >= 15) break;
    } catch { /* not in this method */ }
  }
  if (!n) console.log(`no method of ${cls} touches this->+${hex(off)} in its linear start`);
}

function cmdCheck(args) {
  const t = readTable();
  const ctx = context(args, t);
  let bad = 0;
  const names = new Set();
  for (const row of t.rows) {
    const errs = [];
    if (names.has(row.name)) errs.push("duplicate name");
    names.add(row.name);
    try {
      const fresh = proveRow(specOf(row), ctx);
      for (const c of ["1.0.65", "1.0.68", "pattern", "site", "read"]) if ((fresh[c] || "") !== (row[c] || "")) errs.push(`${c}: table ${row[c] || "-"}, proven ${fresh[c] || "-"}`);
      // the recorded pattern itself must still match where the table says
      const pat = row.pattern ? parsePattern(row.pattern) : null;
      for (const [img, b] of [[ctx.e65, "1.0.65"], [ctx.e68, "1.0.68"]]) {
        const c = parseCell(row[b]);
        if (!pat || !c) continue;
        const at = (c.anchor ?? c.value) - Number(row.site || 0);
        if (row.kind !== "vslot" && !img.matchAt(at, pat)) errs.push(`${b}: pattern does not match at ${hex(at)}`);
        if (img.findAll(pat, { max: 2 }).length !== 1) errs.push(`${b}: pattern not unique`);
      }
    } catch (e) { errs.push(e.message); }
    if (errs.length) { bad++; console.log(`FAIL ${row.name}: ${errs.join("; ")}`); }
    else console.log(`ok   ${row.name}`);
  }
  console.log(`${t.rows.length - bad}/${t.rows.length} rows proven in both exes`);
  if (bad) process.exit(1);
}

// ---------------------------------------------------------------- code generation

const cIdent = (s) => s.replace(/[^A-Za-z0-9_]/g, "_");
const cStr = (s) => JSON.stringify(s);

export function generate(t) {
  const rows = t.rows;
  const feats = ["required", ...new Set(rows.map((r) => r.need).filter((n) => n !== "required"))];
  const fields = rows.filter((r) => r.kind === "field");
  const thunks = rows.filter((r) => r.kind === "code" && r.sym);
  const h = [];
  h.push("// Generated by scripts/sites.mjs gen from client/sites.tsv. Do not edit.", "#pragma once", "");
  h.push(`#define KP_SITE_COUNT ${rows.length}`, `#define KP_BUILD_COUNT ${t.builds.length}`, `#define KP_FEATURE_COUNT ${feats.length}`, "");
  h.push("enum KpSiteId {");
  rows.forEach((r, i) => h.push(`    KPS_${cIdent(r.name)} = ${i},`));
  h.push("};", "");
  const ch = [];
  ch.push("// Generated by scripts/sites.mjs gen from client/sites.tsv. Do not edit.");
  ch.push('#include "Game.h"   // KenshiLib headers of every class with a field row', "#include <stddef.h>", "");
  ch.push("namespace kp {", "");
  ch.push("const KpBuild g_builds[KP_BUILD_COUNT] = {");
  for (const b of t.builds) ch.push(`    { ${cStr(b.label)}, ${cStr(b.sha256)}, ${cStr(b.note)} },`);
  ch.push("};", "");
  ch.push("const char* const g_features[KP_FEATURE_COUNT] = {", ...feats.map((f) => `    ${cStr(f)},`), "};", "");
  const kindEnum = { code: "SK_CODE", global: "SK_GLOBAL", field: "SK_FIELD", vslot: "SK_VSLOT", patch: "SK_PATCH", callsite: "SK_CALLSITE", data: "SK_DATA" };
  rows.forEach((r, i) => {
    if (!r.pattern) return;
    const p = parsePattern(r.pattern);
    ch.push(`static const unsigned char s_pat${i}[] = { ${Array.from(p.bytes, (b) => "0x" + b.toString(16)).join(", ")} };`);
    ch.push(`static const unsigned char s_mask${i}[] = { ${Array.from(p.mask).join(", ")} };`);
  });
  ch.push("", "const KpSiteRow g_sites[KP_SITE_COUNT] = {");
  rows.forEach((r, i) => {
    const vals = t.builds.map((b) => { const c = parseCell(r[b.label]); return c ? `{ 1, ${c.value}LL, ${c.anchor ?? c.value}LL }` : "{ 0, 0, 0 }"; });
    let rk = "RD_NONE", ra = 0, rb = 0;
    if (r.read.startsWith("rip:")) { rk = "RD_RIP"; [ra, rb] = r.read.slice(4).split(":").map(Number); }
    else if (r.read.startsWith("disp:")) { rk = "RD_DISP"; [ra, rb] = r.read.slice(5).split(":").map(Number); }
    else if (r.read === "vt") rk = "RD_VTABLE";
    const n = r.pattern ? parsePattern(r.pattern).bytes.length : 0;
    const pp = n ? `s_pat${i}, s_mask${i}` : "NULL, NULL";
    ch.push(`    { ${cStr(r.name)}, ${kindEnum[r.kind]}, ${feats.indexOf(r.need)}, ${pp}, ${n}, ${Number(r.site || 0)}, ${rk}, ${ra}, ${rb}, { ${vals.join(", ")} } },`);
  });
  ch.push("};", "");
  // Field rows: the 1.0.65 offset must equal the KenshiLib header's (proves table and header agree).
  for (const r of fields) {
    const [cls, member] = r.sym.split("|");
    const v = parseCell(r["1.0.65"]).value;
    ch.push(`static_assert(offsetof(${cls}, ${member}) == ${hex(v)}, ${cStr(`sites.tsv ${r.name}: 1.0.65 offset differs from the KenshiLib header`)});`);
  }
  ch.push("", "} // namespace kp", "");
  // Field ids by member pointer: KP_FIELD(obj, Class, member) compiles only for rows in the table.
  h.push("namespace kp {", "template <typename T, T M> struct FieldRow;", "}");
  const fh = ["// Generated by scripts/sites.mjs gen from client/sites.tsv. Do not edit.", "#pragma once", "namespace kp {"];
  for (const r of fields) {
    const [cls, member] = r.sym.split("|");
    fh.push(`template <> struct FieldRow<decltype(&${cls}::${member}), &${cls}::${member}> { enum { id = KPS_${cIdent(r.name)} }; };`);
  }
  fh.push("}");
  const asm = [];
  asm.push("; Generated by scripts/sites.mjs gen from client/sites.tsv. Do not edit.");
  asm.push("; Each game function KenshiPure calls is defined here as `jmp [slot]`; every slot starts at a trap");
  asm.push("; stub that reports the row (KpTrapHit) instead of jumping to 0.", "");
  asm.push("EXTERN KpTrapHit:PROC", "", ".data", "ALIGN 8", "PUBLIC kp_slots", "kp_slots LABEL QWORD");
  rows.forEach((r, i) => asm.push(r.kind === "code" ? `    QWORD kp_trap_${i}` : "    QWORD 0"));
  asm.push("", ".code");
  rows.forEach((r, i) => {
    if (r.kind !== "code") return;
    asm.push(`kp_trap_${i} PROC`, `    mov ecx, ${i}`, "    jmp KpTrapHit", `kp_trap_${i} ENDP`);
  });
  for (const r of thunks) {
    const i = rows.indexOf(r);
    asm.push(`; ${r.name}`, `${r.sym} PROC`, `    jmp QWORD PTR kp_slots[${i * 8}]`, `${r.sym} ENDP`);
  }
  asm.push("", "END", "");
  return { header: h.join("\n") + "\n", fieldHeader: fh.join("\n") + "\n", cpp: ch.join("\n"), asm: asm.join("\r\n") };
}

function cmdGen(args) {
  const dir = resolve(args[0] || join(REPO, "build", "client", "gen"));
  mkdirSync(dir, { recursive: true });
  const g = generate(readTable());
  writeFileSync(join(dir, "kp_sites_gen.h"), g.header);
  writeFileSync(join(dir, "kp_fields_gen.h"), g.fieldHeader);
  writeFileSync(join(dir, "kp_sites_gen.cpp"), g.cpp);
  writeFileSync(join(dir, "kp_thunks.asm"), g.asm);
  console.log(`generated ${dir}`);
}

const [cmd, ...rest] = process.argv.slice(2);
if (import.meta.url === `file:///${process.argv[1].replace(/\\/g, "/")}` || process.argv[1]?.endsWith("sites.mjs")) {
  try {
    if (cmd === "add") cmdAdd(rest);
    else if (cmd === "check") cmdCheck(rest);
    else if (cmd === "vias") cmdVias(rest);
    else if (cmd === "gen") cmdGen(rest);
    else { console.log(readFileSync(fileURLToPath(import.meta.url), "utf8").split("\n").slice(1, 9).join("\n")); process.exit(cmd ? 1 : 0); }
  } catch (e) {
    console.error(`sites.mjs: ${e.message}`);
    process.exit(1);
  }
}
