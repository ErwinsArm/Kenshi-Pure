// x64 decoding for scripts/sites.mjs with the pinned iced-x86 (tools/build/iced-x86, from
// scripts/setup-build.ps1): masked patterns and operand reads.
import { createRequire } from "node:module";
import { existsSync } from "node:fs";
import { join } from "node:path";

let ice = null;
export function loadIced(repoRoot) {
  if (ice) return ice;
  const p = join(repoRoot, "tools", "build", "iced-x86", "package", "iced_x86.js");
  if (!existsSync(p)) throw new Error(`iced-x86 not found (${p}). Run scripts\\setup-build.ps1.`);
  ice = createRequire(import.meta.url)(p);
  return ice;
}

const BRANCHES = new Set([1, 3, 5]);   // FlowControl: UnconditionalBranch, ConditionalBranch, Call

// Decodes instructions starting at rva. Returns plain objects (iced objects are freed).
export function decodeAt(img, rva, maxBytes) {
  const bytes = img.bytes(rva, Math.min(maxBytes + 16, img.text.end - rva));
  const d = new ice.Decoder(64, bytes, ice.DecoderOptions.None);
  d.ip = BigInt(rva);
  const info = new ice.InstructionInfoFactory();
  const out = [];
  let pos = 0;
  while (pos < maxBytes && d.canDecode) {
    const ins = d.decode();
    if (ins.isInvalid) { ins.free(); break; }
    const c = d.getConstantOffsets(ins);
    const ii = info.info(ins);
    const writes = [];
    for (const u of ii.usedRegisters()) {
      if (u.access === 3 || u.access === 4 || u.access === 5 || u.access === 6) writes.push(ice.RegisterExt.fullRegister(u.register));
      u.free();
    }
    const o = {
      rva: rva + pos, len: ins.length, flow: ins.flowControl, mnemonic: ins.mnemonic,
      dispOff: c.displacementOffset, dispSize: c.displacementSize,
      immOff: c.immediateOffset, immSize: c.immediateSize,
      ipRel: ins.isIpRelMemoryOperand,
      memBase: ice.RegisterExt.fullRegister(ins.memoryBase), memIndex: ins.memoryIndex,
      disp: Number(BigInt.asIntN(64, ins.memoryDisplacement)),
      ipTarget: ins.isIpRelMemoryOperand ? Number(ins.memoryDisplacementU64) : 0,
      // mov r64, r64: copies of `this`
      movRegReg: ins.mnemonic === ice.Mnemonic.Mov && ins.opCount === 2 && ins.op0Kind === 0 && ins.op1Kind === 0 &&
        ice.RegisterExt.fullRegister(ins.op0Register) === ins.op0Register && ins.op1Register === ice.RegisterExt.fullRegister(ins.op1Register)
        ? [ins.op0Register, ins.op1Register] : null,
      writes,
    };
    c.free(); ii.free(); ins.free();
    out.push(o);
    pos += o.len;
  }
  info.free();
  d.free();
  return out;
}

// Mask of the bytes that change when code or data moves: rel32 branch/call targets and RIP-relative
// displacements; with `allDisp` also every other memory displacement (field offsets).
export function maskFor(insns, startRva, len, { allDisp = false, extra = [] } = {}) {
  const mask = new Uint8Array(len).fill(1);
  const clear = (from, n) => { for (let i = 0; i < n; i++) if (from + i >= 0 && from + i < len) mask[from + i] = 0; };
  for (const i of insns) {
    const at = i.rva - startRva;
    if (BRANCHES.has(i.flow) && i.immSize === 4) clear(at + i.immOff, 4);
    if (i.dispSize && (i.ipRel || (allDisp && i.dispSize >= 1))) clear(at + i.dispOff, i.dispSize);
  }
  for (const [o, n] of extra) clear(o, n);
  return mask;
}

export const REG = () => ice.Register;
