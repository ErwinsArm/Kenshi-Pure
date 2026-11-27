// KenshiLib 0.5.0 as a source of 1.0.65 facts for scripts/sites.mjs (read-only):
//   - its address map: mangled name -> index (Source/kenshi/symbols.asm + functions/*.inc +
//     globals.inc) and index -> RVA (RE_Kenshi/RVAs/Steam_1.0.65.br, raw little-endian int32s);
//   - its headers: field offsets ("type name; // 0x70 Member") and vtable offsets
//     ("virtual ...;// ... vtable offset = 0x8").
// KenshiLib's names and layouts are community guesses; sites.mjs proves each one it uses in the exe.
import { readFileSync, readdirSync, existsSync } from "node:fs";
import { join } from "node:path";

export class KenshiLibMap {
  constructor(srcRoot, rvaFile) {
    const kdir = join(srcRoot, "Source", "kenshi");
    const bases = new Map();
    for (const m of readFileSync(join(kdir, "symbols.asm"), "latin1").matchAll(/^(_\w+_base) EQU (\d+)\*8/gm))
      bases.set(m[1], Number(m[2]));
    this.rvas = readFileSync(rvaFile);
    this.byName = new Map();    // mangled -> { index, rva, decl }
    let maxIndex = -1;
    for (const f of readdirSync(join(kdir, "functions"))) {
      // A proc with a jmp is a slot; an empty proc ("_NV_x", "_CONSTRUCTOR") falls through to the
      // next proc's jmp, so it names the same slot.
      const lines = readFileSync(join(kdir, "functions", f), "latin1").split(/\r?\n/);
      let pending = [], decl = "";
      for (const l of lines) {
        let m;
        if ((m = l.match(/^; (.*)$/))) decl = m[1];
        else if ((m = l.match(/^(\S+) proc EXPORT$/))) pending.push(m[1]);
        else if ((m = l.match(/^jmp function_pointers\[(_\w+_base)\+(\d+)\*8\]$/))) {
          const base = bases.get(m[1]);
          if (base === undefined) throw new Error(`unknown base ${m[1]} in ${f}`);
          const index = base + Number(m[2]);
          maxIndex = Math.max(maxIndex, index);
          for (const name of pending) this.byName.set(name, { index, decl, kind: "code" });
          pending = [];
        }
      }
    }
    // Globals follow the function slots in the same RVA file, in globals.inc order.
    const g = readFileSync(join(kdir, "globals.inc"), "latin1");
    let gi = 0;
    for (const m of g.matchAll(/^; (.*)\r?\n(\S+) proc EXPORT/gm)) this.byName.set(m[2], { index: maxIndex + 1 + gi++, decl: m[1], kind: "global" });
    const total = this.rvas.length / 4;
    if (maxIndex + 1 + gi !== total) throw new Error(`KenshiLib map: ${maxIndex + 1 + gi} slots but ${total} RVAs`);
    for (const e of this.byName.values()) e.rva = this.rvas.readInt32LE(e.index * 4);
  }

  get(mangled) {
    const e = this.byName.get(mangled);
    if (!e) throw new Error(`KenshiLib has no symbol ${mangled}`);
    return e;
  }

  // All code RVAs (sorted, unique, > 0): neighbours for the shift check.
  codeRvas() {
    const s = new Set();
    for (const e of this.byName.values()) if (e.kind === "code" && e.rva > 0) s.add(e.rva);
    return [...s].sort((a, b) => a - b);
  }
}

// Body lines of `class Name` (depth-1 lines only) in a KenshiLib header. `qual` is "Outer::Inner" or "Name".
function classBody(includeRoot, qual) {
  const parts = qual.split("::");
  const outer = parts[0], inner = parts[parts.length - 1];
  const isDef = (l) => new RegExp(`^\\s*(class|struct) ${inner}\\b(?!.*;\\s*$)`).test(l);
  const own = join(includeRoot, "kenshi", outer + ".h");
  // The class's own header, else the one header that defines it (e.g. InventorySection in Inventory.h).
  let file = existsSync(own) ? own : null;
  if (!file) {
    const dir = join(includeRoot, "kenshi");
    const defs = readdirSync(dir).filter((f) => f.endsWith(".h") &&
      readFileSync(join(dir, f), "latin1").split(/\r?\n/).some((l) => new RegExp(`^\\s*(class|struct) ${outer}\\b(?!.*;\\s*$)`).test(l)));
    if (defs.length !== 1) throw new Error(`${defs.length} headers define ${outer}`);
    file = join(dir, defs[0]);
  }
  const lines = readFileSync(file, "latin1").split(/\r?\n/);
  const start = lines.findIndex(isDef);
  if (start < 0) throw new Error(`class ${inner} not found in ${file}`);
  const body = [];
  let depth = 0, opened = false;
  for (let i = start; i < lines.length; i++) {
    const l = lines[i].replace(/\/\/(?!.*(Member|vtable offset)).*$/, "");
    for (const ch of l) {
      if (ch === "{") { depth++; opened = true; }
      else if (ch === "}") depth--;
    }
    if (opened && depth === 1 && !/^\s*(class|struct)\b/.test(lines[i])) body.push(lines[i]);
    if (opened && depth === 0) break;
  }
  return { file, body };
}

// Offset of a data member from its "// 0x.. Member" comment.
export function headerFieldOffset(includeRoot, qualClass, member) {
  const { file, body } = classBody(includeRoot, qualClass);
  const re = new RegExp(`[\\s*&>]${member}(\\[[^\\]]*\\])?\\s*;\\s*//\\s*(0x[0-9A-Fa-f]+) Member`);
  const hits = body.map((l) => l.match(re)).filter(Boolean);
  if (hits.length !== 1) throw new Error(`${qualClass}::${member}: ${hits.length} member lines in ${file}`);
  return parseInt(hits[0][2], 16);
}

// Vtable byte offset of a virtual method from its "vtable offset = 0x.." comment.
export function headerVtableOffset(includeRoot, qualClass, method) {
  const { file, body } = classBody(includeRoot, qualClass);
  const re = new RegExp(`^\\s*virtual [^/]*\\b${method}\\(.*vtable offset = (0x[0-9A-Fa-f]+)`);
  const hits = body.map((l) => l.match(re)).filter(Boolean);
  if (hits.length !== 1) throw new Error(`${qualClass}::${method}: ${hits.length} virtual lines in ${file}`);
  return parseInt(hits[0][1], 16);
}
