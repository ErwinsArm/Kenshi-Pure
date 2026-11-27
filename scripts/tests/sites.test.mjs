// node --test scripts/tests/*.test.mjs   (no game files needed)
import { test } from "node:test";
import assert from "node:assert/strict";
import { parseCell, generate } from "../sites.mjs";
import { parsePattern, formatPattern, longestRun } from "../lib/pe.mjs";
import { check } from "../check-fields.mjs";

test("cells: value, value@anchor, empty", () => {
  assert.deepEqual(parseCell("0x787e70"), { value: 0x787e70, anchor: null });
  assert.deepEqual(parseCell("0x70@0x64e02a"), { value: 0x70, anchor: 0x64e02a });
  assert.equal(parseCell(""), null);
});

test("patterns round-trip and the longest fixed run", () => {
  const p = parsePattern("48 8D 0D ?? ?? ?? ?? E8");
  assert.deepEqual(Array.from(p.mask), [1, 1, 1, 0, 0, 0, 0, 1]);
  assert.equal(formatPattern(p), "48 8D 0D ?? ?? ?? ?? E8");
  assert.deepEqual(longestRun(p.mask), { start: 0, len: 3 });
});

test("generate: code rows get thunks and trap stubs, fields get ids and a header check", () => {
  const t = {
    builds: [{ label: "1.0.65", sha256: "a".repeat(64), note: "x" }, { label: "1.0.68", sha256: "b".repeat(64), note: "y" }],
    rows: [
      { name: "GameWorld::mainLoop", kind: "code", need: "required", sym: "?f@@YAXXZ", "1.0.65": "0x10", "1.0.68": "0x20", pattern: "C3 ??", site: "0", read: "" },
      { name: "MedicalSystem::blood", kind: "field", need: "optional:probe", sym: "MedicalSystem|blood|?g@@YAXXZ", "1.0.65": "0x70@0x100", "1.0.68": "", pattern: "", site: "", read: "" },
    ],
  };
  const g = generate(t);
  assert.match(g.asm, /\?f@@YAXXZ PROC\r\n    jmp QWORD PTR kp_slots\[0\]/);
  assert.match(g.asm, /kp_trap_0 PROC/);
  assert.match(g.cpp, /static_assert\(offsetof\(MedicalSystem, blood\) == 0x70/);
  assert.match(g.cpp, /\{ 0, 0, 0 \}/);                       // no 1.0.68 value = unresolved there
  assert.match(g.fieldHeader, /FieldRow<decltype\(&MedicalSystem::blood\), &MedicalSystem::blood> \{ enum \{ id = KPS_MedicalSystem__blood \}/);
});

test("check-fields: raw access through game-typed variables is refused, own structs pass", () => {
  const members = { names: new Set(["blood", "name", "item"]), classes: new Set(["MedicalSystem", "GameData"]) };
  const src = [
    "struct Own { float blood; };",
    "float f(MedicalSystem* m, Own* o)",
    "{",
    "    float a = m->blood;",
    "    float b = o->blood;",
    "    float c = KP_FIELD(m, MedicalSystem, blood);",
    "    Item* i = KP_FIELD(&v[0], X, y)[1].item;",
    "    float d = m->blood; // kp-not-game: test",
    "}",
    "void g(Own* m) { float e = m->blood; }",
  ].join("\n");
  assert.deepEqual(check(src, members).map((h) => h.line), [4, 7]);
});
