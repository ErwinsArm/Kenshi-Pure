# AnarchyKenshi.dll: Spine topic layouts

- File: `AnarchyKenshi.dll` from mod v0.133.1, SHA-256
  `c3d2419d1c02b516552032e9037d29ec1a1b88661199ddac7775f601d97b51bb`, PE32+ x64, image base
  `0x180000000`. All addresses are RVAs in this file (globals too: `0x194830c` = VA `0x18194830c`).
- Evidence: Ghidra 12.2 decompile (no auto-analysis; every topic vtable slot and its callees to depth 2)
  and `llvm-objdump` disassembly; read statically, nothing was run, no server was contacted. Labels:
  **M** = measured (sizes, order, limits, RVAs, strings seen in decompile or disassembly), **I** = inferred
  (meanings, names). Function and field names are guesses unless marked M. Two reviewers spot-checked
  the conventions, thunks, key reader, machine trailers and the vote, wallet, weather and rules sections against the
  decompile; most topics were not re-checked by a second reader. Field meanings are the weakest part.
- The mechanism (wire format, ops, slots, topic table) is in [spine.md](spine.md). This file gives, per
  topic, the value layout of the server -> client rows and the client -> server intent payloads.
- Coverage: all 48 registrations. **full** = every row form and intent op decoded with its limits;
  **partial** = the wire layout is decoded but some effect, meaning or side path is not (listed under "Open").

## Conventions (M unless marked)

**Reader primitives** (reader struct `{u8* buf @0, u32 len @8, u32 pos @0xc, u8 ok @0x10}`; an overrun clears `ok`):

| type | reader | wire form |
|---|---|---|
| u8 / i8 | inline or `0x543b90` | 1 byte |
| u16 | `0x9f8d0` | 2 bytes LE |
| u32 | `0x543bc0` | two u16, low half first |
| f32 | `0x543bf0` | u32 bit pattern; NaN or beyond ±3.0e38 (`0x12c1bd8`, `0xeb5e6c`) clears `ok`. cellhost uses `0x9f940` (rejects only NaN/Inf) |
| str16<=N | `0x543c50(reader, out, N)` | u16 length + bytes; a length > N (or past the end) clears `ok` and yields an empty string: the row is rejected, not truncated |
| str8<=N | `0x9f9a0(reader, out, N)` | u8 length + bytes, same reject rule (only in the cellhost combat pack) |
| raw n | `0x53a120(reader, out, n)` | n bytes |
| raw tail | `0x543e40` | all remaining bytes (fails if fewer than asked) |
| key | `0x543f90` (writer `0x543aa0`) | `u8 type` + payload (below) |

"End exact" = the reader checks `ok && pos == len` after the last field; otherwise apply returns 2 (bad) and
nothing is stored. Apply return codes: 0 ok, 1 hold (target not present yet; retried), 2 bad.
"Printable" = every byte 0x20..0x7e.

**Writers**: u8 = insert one byte `0xd940(str, pos, ch)` (also `0xbf40`; pos 0 puts the op/tag byte first, pos = size
appends), u16 `0x5439f0` (also `0xcae2b0`), u32 = two u16 (low first), f32 = its u32 bits, str16 = u16 length +
append `0x4240` / `0x4360`.

**Keys** (`u8 type` + payload, 0x18 B in memory):
- type 1 `body:` u32 entity id; type 4 `player:` u32; type 5 `global:` u32 id (singletons use 1; some topics use
  4-char ids, e.g. trade `0x54524448` "TRDH").
- type 2 `place:` = 4 x u32. Built by `0x543430(x, y, z, d)`: words = floor(v*2+0.5) as int32 (0 if |v| > ~1e9;
  constants 2.0 at `0xeb553c`, 0.5 at `0xeba8bc`), decoded as `int * 0.5`. The 4th word `d` selects the row
  form: a 4-char tag (written below as the constant and its ASCII reading, e.g. `0x424d4143` "BMAC"), a small
  number (slave/cage place rows: 8), or FNV-1a32 of a name (structure, buildings).
- type 3 `name:` u32 = FNV-1a32 (basis `0x811c9dc5`, prime `0x1000193`) of a name string (`0x5433f0`, chained
  over more strings by `0x5433c0`), other key words 0. **Salted** form: FNV-1a32 over the 4 LE bytes of a
  record id, started with `(byte0 xor salt)`. Salts: "corp" `0x636f7270`, "look" `0x6c6f6f6b`, "carr" `0x63617272`,
  "tomb" `0x746f6d62`, "zone" `0x7a6f6e65`, "drop" `0x64726f70`, "grnd" `0x67726e64` (corpses), `0x53545a4e`
  "STZN" (stores); research uses string salts "res:", "wm:".
- Place-key tags seen: "SHLF" `0x53484c46`, "STOR" `0x53544f52`, "TRDF" `0x54524446`, "FURN" `0x4655524e`
  (stores, trade), "LOCK" `0x4c4f434b`, "SLOG" `0x534c4f47` (locks), "BLED" `0x424c4544`, "BMAC" `0x424d4143`
  (base), "TLAY" `0x544c4159` (townlayout).

**Intents and notices**:
- Client senders build `[op][fields...]` and call `0x547b00(kind, buf)` (reliable) or `0x547ce0` (lossy); both
  return the intent's seq (0 = not sent, I). Some senders take the kind from the topic's vtable slot 1 instead
  of an immediate; acidrain/acidenv pass the instance member `this+8`.
- Intents without an op byte: probe, heldby, acidrain/acidenv, shots. All others start with `u8 op`.
- The server sets global `0x194830c` to the intent's 4-byte seq (in `0x540590`) before slot 12 runs. A reply is a
  kind-1 notice `0x547e50(player, kind, code = that seq, flag 1 ok / 0 refused, text)`; blob
  `u8 1, u16 0, u16 0, u8 7, u16 topic, u32 seq, u8 flag, u16 len (<=200), text`. The client's slot 20
  matches the code against its pending sets (per topic).

**Other shared values**:
- Value positions are quantised `floor(v*2.0+0.5)*0.5`; tenths `floor(v*10+0.5)` (10.0 at `0xeb681c`).
- Position plausibility `0xa5e960`: x, z in (-160000, 160000), y in (-50000, 50000).
- Zone id = `(cz << 16) | (cx & 0xffff)`, cell = floor(coord / `*(float*)0xebe4bc`) + 0x20.
- The shared 43-byte event record `{u32, u32, u32, 6 x f32, u8, u16, u32}` appears in squads tag 'H', cellhost
  body/player rows and squads op 5.
- Player ids in text form: `anarchy-pf-` + 16 hex (faction id, validator `0x60f0b0`), `anarchy-pg-` + 16 hex
  (group id, `0x696b30`).
- Engine hooks used by topics: table `0x1965140`; `*0x1965150()` local-avatar guard, `*0x1965158(id)` body by
  id. Host handle `0x1942128` != 0 on the server/host process; several client applies do nothing there.
- Stubs: `0x7ffa80` no-op, `0x935c40` apply "unsupported" (returns 2), `0x935c60` period. Ghidra shows some real
  bodies under tiny thunk entries (`xor r9d,r9d; jmp X`), e.g. the applies of locks, rank, stores, trade, splint,
  wear, research.
- **Evidence caveat.** "M" on a field means it was read in the Ghidra decompile (and in the disassembly where a topic
  says so). Ghidra can drop call arguments (e.g. the weather call to `0x9368e0`) and shows raw-bit f32 reads as
  `(float)(int)` casts, so field widths and float types from decompile-only reads deserve a disassembly check before
  they are built on. Engine-side offsets (Kenshi struct fields such as `+0x174`) refer to the Steam `kenshi_x64.exe`
  named in [spine.md](spine.md) and are build-dependent; "host fn 0x113"-style numbers are signature-table ordinals
  (`0xcd9930(ordinal)`), not RVAs.

## Upstream chunk framing (work, limbs, splint)
Written by `0x5423b0` + `0x53a200` (**M**). Blob = `u8 (version | 0x80)`, `u32 seq` (global counter `0x1948338`, written as u8, u8, u16), then ops. It is sent through the client export `SendIntent` (reliable, `0x19424d0`). A chunk is closed at about `0x1fc0` bytes and a new one started (check `0x1fc0 < size + 10 + len`).

Ops per entity (`id` = body id u32, `aux` = u32 stored next to the id; each u32 is written as two u16, lo then hi):

| op | fields | meaning |
|---|---|---|
| 1 | `u8 1, u32 id, u32 aux, u16 len, len bytes` | upsert. `bytes` = the row value of that topic, identical to the server -> client row value. Values > `0x1000` bytes are skipped. Emitted only when the FNV-1a hash of the value or `aux` changed against the last sent state (cache map at `param_1+0x10`). |
| 2 | `u8 2, u32 id, u32 aux` | remove |
| 3 | `u8 3` | start of a full resend (written when the dirty/full flag or a timer, `0x100a310`, says so) |
| 4 | `u8 4` | end of a full resend |

The server-side consumer is the judge `0x53dd40` (plausibility check, per-host mirror); its rules are topic-specific (splint slot 28 below). The meaning of `aux` is not decoded (**I**: the Spine fills it; no topic function sets it).

---

## Topics (by kind)

### probe (kind 2) - round-trip self-test (echo)
Vtable `0x1212e68`, v1, period 2 s. Status: **full**.
Functions: gather `0x55f970`, apply `0x55fe20`, clear-all `0x5601f0` (slot 15), intent `0x55fc80`, client tick `0x5603f0`, notice `0x560250`, stats string `0x5606d0`.

**Rows (server -> client).** Gather `0x55f970` keeps a counter at `this+8` (++ per call); rows sorted by key.
| key | value (M) |
|---|---|
| `player:<pid>` (type 4, pid = viewer's player id) | `u32 player id`, `u32 epoch` (= gather counter >> 3); 8 bytes |
| `global:1` (type 5) | `u32 n` = (end-begin)/4 of the u32 vector at player struct `+0x18..+0x20` (I: count of the viewer's owned bodies) |
| `global:2` (type 5) | `u32 1`; written only when `(counter/10)&1 == 0` |

Client apply `0x55fe20`: key type 4 -> reads 2 x u32, text "player <p> epoch <e>"; any other key type -> reads 1 x u32, text "value <n>". Text kept in a map at `this+0x30` keyed by key; `this+0xc`++. Only the reader ok flag is checked: no exact-end check, never returns 2 (`xor eax,eax` at `0x5601c0`). Slot 15 ignores the key and clears the whole map (`0x560ab0`), `this+0x10`++. Strings "echo ", "value ", "player ", " epoch " at `0x1212e00`, `0x1212e08`, `0x11eb840`, `0x11bee70`.

**Intent (client -> server).** No op byte.
| fields | effect / reply |
|---|---|
| `u32 n` | Server `0x55fc80` (no exact-end check): replies with a kind-1 notice to the sender: topic 2, code = seq of the intent being handled (global `0x194830c`, set at `0x540590` before dispatch; I), flag 1, text "echo <n>". |

Sender: client tick `0x5603f0` every 10.0 s (float `0xeb681c`, accumulates dt); `n` = `this+0x2c`, incremented before each send; returned seq stored in `this+0x28`; `this+0x14`++ per successful send. Client notice `0x560250` counts ok (`this+0x18`) if flag != 0 and code == `this+0x28` and text == "echo <this+0x2c>", else bad (`this+0x1c`).

### factions (kind 3) - Kenshi faction table
Vtable `0x121f208`, v1, period 2 s. Status: **full** (meaning of field 3 not decoded).
Functions: gather `0x5f54a0`, row-value builder `0x5f4950` (+ `0x5f4860`), apply `0x5f5930` (5-byte `jmp` thunk) -> body `0x5f6990`, clear-all slots 15/21 `0x5f5940`/`0x5f5960` (ignore the key, clear the table map at `this+0xa8`), inner apply `0x61bbb0`/`0x61c320`. No intent (slot 12 stub), no upstream.

**Rows (server -> client).** Key: type 3 `name:`, `u32 id` = FNV-1a32 (offset `0x811c9dc5`, prime `0x1000193`) of the faction-name string s1; the other four key u32s (key +8,+0xc,+0x10,+0x14) must be 0. Other type / hash mismatch / non-zero tail -> counter `this+0x2c`, return 2. (M, `0x5f6990`, `0x5f4950`.)
| field | type | limits | meaning |
|---|---|---|---|
| s1 name/id | str16<=0x40 | printable 0x20..0x7e, length 1..0x40 (checked in `0x61bbb0`) | faction name (M) |
| s2 label | str16<=0x40 | | display label, from Kenshi faction record `+0xb8` (I) |
| u32 | 4 bytes (2 x u16) | | from element `+0x50`, copied from Kenshi faction struct `+0x174`; meaning not decoded (I: int/flag/tier) |
| flag | u8 | 0/1 | 1 in the variant sent for the viewer's own faction row, 0 otherwise (M: variant built by `0x5f4860`, `edi==1`) |

End exact (`pos == len`), else `this+0x20`++, return 2. Table capped at 0x800 rows (`this+0xb8`); further new keys counted in `this+0x30`.
Returns (M, `xor eax,eax` @`0x5f6e52`, `mov eax,1` @`0x5f6e93`, `mov eax,2` @`0x5f6edc`): 0 applied, 1 hold/retry (`this+0x24`), 2 bad.
Apply effect (I, from `0x61bbb0`/`0x61c320` and log strings): flag 0 -> create/rename the client faction s1 with label s2 (`0x61bbb0(s1,s2,&changed,0)`). flag 1 and `0x696b30(s1)` true -> s1 must be exactly 27 chars = prefix `anarchy-pg-` (`0x121f4c0`, 11 chars) + 16 chars of [0-9a-f], else bad (counter `0x1965d68`, return 2); then `0x61bbb0(s1,s2,&c,1)` = the player's own group twin faction.
Gather (M, `0x5f54a0`): per viewer, iterates the vector at `this+0x80` (0xc0-byte prebuilt rows: key @0x50, value variants @0x68 (others) and @0x90 (own), FNV of each variant @0xb8/0xbc); rotating window of at most 0x3ff rows per call; skips rows whose value+key is unchanged for the viewer (helpers `0x5529d0`/`0x552d10`); picks variant 1 for the row whose name equals the viewer's own faction string (`0x615fb0`/`0x616150`). Rows named like the viewer's second (player-group) string are skipped when it differs from the first (I; counter `this+0x34`).
Source (M): Kenshi faction list (globals `0x1965f80`/`0x1965f88`), 0x58-byte elements: name, label, u32 @0x50, bool @0x54 (bool not on the wire). Console `spine factions fake` adds rows "anarchyfake<N>" / "Fake<N> Faction" (strings `0x121f0d8`, `0x121f0e4`, `0x1214f00`).

**Open.**
- Meaning of field 3.
- Side effects of inner apply `0x61bbb0` on Kenshi's faction manager beyond the string evidence.

### relations (kind 4) - faction-to-faction relations of the viewer's faction
Vtable `0x121f358`, v1, period 2 s. Status: **full** (meaning of `kind` not decoded).
Functions: gather `0x5f9200`, apply `0x5f9750` and `0x5f9760` (slots 14/18, 8-byte `jmp` thunks) -> body `0x5fa1b0` (inner apply `0x61c630`), remove-by-key slot 15 `0x5f9770` (range erase `0x560b50` in map `this+0x68`), clear-all slot 21 `0x5f9780`, list builder `0x616520`. No intent, no upstream.

**Rows (server -> client).** Key (M, `0x5fa1b0`, `0x5f9200`): type 3 `name:`, `u32 id` = FNV-1a32 of the string s1; other four u32 = 0 (same as factions).
| field | type | limits | meaning |
|---|---|---|---|
| s1 other faction name | str16<=0x60 | printable ASCII, 1..0x60 chars (`0x61c630`); FNV must equal key id | the other faction |
| relation | u32 (2 x u16), read as signed | -10000..+10000 (`param+10000 < 0x4e21`) | client sets float `value / 100.0` (const `0xebcd9c`) |
| kind | u8 | < 8 (0..7) | I: Kenshi relation type/flag; not decoded |

End exact (`pos == len`), else `this+0x20`++, return 2. Max 0x400 rows (`this+0x78`); non-matching key counters `this+0x28`/`+0x2c`. Returns (M, disasm of `0x5fa1b0`): 0 applied, 1 hold/retry (`this+0x24`), 2 bad.
Apply effect (I, `0x61c630`): resolves s1 to a faction (`0x739180`) and the receiving client's own faction (`0x5fbea0`); if the stored relation differs, calls `0x5fbce0(factionA, factionB, relation/100.0, kind)`. Skipped when both are the same faction or the target is not a plain player-type faction (checks at `0x61c6xx`).
Gather (M, `0x5f9200`): per viewer (player id `*param_2`) the list is rebuilt by `0x616520` (cached per player in map `this+0x38`, refreshed when `now - last >` double at `0xfaebd8`; entries expire after double at `0x106b670` s); <= 0x400 rows per call, unchanged ones skipped. List source (M, `0x616520`): the viewer's faction record array at `+0x138` (12-byte entries: `u32 @0`, `u32 @4`, `u8 @8`), only entries with byte @8 != 0, paired with the global faction list (`0x1965e90`, stride 0x60: name @8, label @0x30). Wire u32 = entry u32 @0, wire u8 = low byte of entry u32 @4 (I: relation x100 and a relation-type enum; Kenshi struct names not verified).

**Open.**
- Meaning of `kind` 0..7 (I: Kenshi relation type/flag).
- Exact transformation of entry u32 @0 (apply divides by 100.0).

### owntask (kind 5) - the player's own body: work order at a machine
Vtable `0x12143c8`, v1, period 1 s. Status: **full** (row, intent, sender, gather; the server tick that advances phase/witness/reorders is not read).
Functions: gather `0x567070`, apply `0x567330`, remove `0x567620`, intent `0x5667a0` (2229 B), client tick (slot 19) `0x5678f0`, reply handler (slot 20) `0x567870`, reset (slot 21) `0x567730`, slot 11 `0x567060`, client sender `0x56edc0` (call at `0x56ef83`, `0x547b00(5, ...)`). Slot 26 = stub (no host upstream).
Purpose (**I**, log strings): a client player orders its own body to a machine/building task; the server registers, validates and mirrors the state back.

**Task codes** (name table `0x564c30`, **M**): `0x02` BUILD, `0x47` ADD_MATERIALS_TO_BUILDING, `0x57` OPERATE_MACHINERY, `0x5b` UNJAM_MACHINE, `0x5c` COLLECT_OUTPUT_RESOURCE, `0x5d` FILL_MACHINE, `0x5f` REPAIR, `0x60` DISMANTLE, `0x61` USE_TRAINING_DUMMY, `0x62` USE_BED, `0x71` EMPTY_MACHINE_OUTPUTS, `0x97` EMPTYING_MACHINE, `0x98` OPERATE_AUTOMATIC_MACHINERY; any other prints "task".
Allowed on the intent (group table `0x564b40`, **M**): group 1 = {`0x57, 0x5b, 0x5c, 0x5d, 0x71, 0x97, 0x98`}, group 2 = {`0x02, 0x47, 0x5f, 0x60`}, group 3 = {`0x61`}; anything else (incl. `0x62`) = group 0 = rejected. Each group has an enable byte at `this+8+group` (**I**: config); disabled or unknown -> notice `task <n> is not on the owntask list here` (counter `+0xd8`).
Phase names (`0x564fd0`, **M**): 1 waiting, 2 ordered, 3 held, 4 retry, 5 done. Witness bit names (`0x564da0`, **M**): 1 "goal", 2 "act", 4 "skill", 8 "op"; 0 = "none".

**Rows (server -> client).** Key type 1 (body), else ignored. Value, end exact (else nothing stored):

| field | type | limits | meaning (I) |
|---|---|---|---|
| task | u16 | not range-checked; printed via the name table | task code |
| phase | u8 | 1..5 (0 or >= 6 ignored) | order phase (names above) |
| witness | u8 | bit set above | which evidence the server saw |
| reorders | u8 | - | how often the order was re-issued |

Client stores `{task, phase, witness, reorders}` (4 x u32) in the map `this+0x228` keyed by body id; if the (task, phase) pair changed: counter `+0x270` and log `owntask: the server copy of body <id> (<task name> <task>) phase=<name> witness=<names> reorders=<n>`.
Remove (`0x567620`): log `owntask: the server let go of body <id>'s work`, mirror entry dropped. Slot 21 clears all client maps. Reply (slot 20): `ok == 0` -> counter `+0x26c` and (max 0x14 lines) log `owntask: the server refused our word - <msg>`.
Gather (**M**): for each body id of the viewer (vector at `param_2+0x18`) found in the server order map `this+0x20` (0xa8-byte records): key body:`id`, value = `u16 task` (rec+4), `u8 phase` (rec+0x6c, starts at 1), `u8` rec+0x70, `u8` rec+0x68. Order equals the apply order, so rec+0x70 = witness, rec+0x68 = reorders (**I**; both updated by server ticks, not decoded).

**Intent (client -> server).** Payload after the 5-byte header:

| op | fields (wire order) | effect / reply |
|---|---|---|
| (single form) | `u32 body_id`; `u8 set` | `set == 0`: **stop**; the server cancels the order of that body with reason "the owner stopped" (counter `+0x19`). Only the u8 is needed; no end-exact check on this path. |
| `set != 0` | `u32 body_id`; `u8 set`; place key (**type 2**: `u8 2` + 4 x u32, a,b,c = x,y,z in half-units, `d` ("fn", seat/function index) <= `0xff`, else malformed, counter `+0xf4`); `u16 task`; `u32 X`; `u8 witness`; `u8 by`; end exact | register or refresh an order. `X` = stored at rec+0xc, echoed to the server copy (**I**: per-order tag/serial, meaning not decoded). `witness` stored rec+0x28 (bit set above). `by` stored rec+0x2c: 1 -> logged "op", else "goal". |

Server checks in order (**M**; refusals go out as `u8 7` notices with `ok = 0` via `0x547e50` / `0x56b870`):
1. Body not in the sender's squad (vector `+0x18`/`+6` of the player struct) -> `that body is not in your squad` (`+0xd4`).
2. Task group allowed (above), else `task <n> is not on the owntask list here`.
3. Character exists on the server (`0x1965158`), not dead/KO (`0x48b300`, `0x44ef50`; counters `+0xe0/+0xe4`), and the machine position `(a*0.5, c*0.5)` lies within **600.0** (float `0x44160000` at `0xeb945c`; 2-D distance via `0x5650c0`), else `that machine is too far from your body on the server` (`+0x1d`).
4. An order for that body with the same task/key exists -> only refresh (rec+0x58 tick, rec+0x48 witness; `+0xcc`); a different one -> the old order is cancelled with reason `the owner changed the work`.
5. Order cap **0x200** records (else `+0xec`); a new 0xa8-byte record is built (player, task, X, a,b,c as floats = value*0.5, witness, by, tick, phase 1; `0x565e70` searches the target near (a*0.5, b*0.5, c*0.5, d), `0x565120` yields a target handle, **I**). Log `owntask: body <id> (player <p>) says <task name> (<task>) at fn <d> @(<x>,<z>) by <goal|op> - registered`.

Sender `0x56edc0` (**M**): `u32 body id`, `u8 set` (= `state[0] != 0`), then if set: place key (type 2, coords from state `+0x14/+0x18/+0x1c`, `d` = state `+0xc`), `u16 task` (state `+8`), `u32 X` (state `+0x10`), `u8` state `+0x30` (witness), `u8` state `+0x34` (by). Send failure: counter `+0x274`. Logs `owntask: our body <id> works "<name>" <task name> (<task>) fn <d> at (<x>,<z>) by <goal|op> witness=<names> - telling the server` / `... stopped working - telling the server`.
Client tick `0x5678f0` (**M**): accumulates frame time; every 500 ms (`500.0 / 0xeb867c`) asks the host for the controlled body (`0x1965150()`), resolves it (`0x1965158`); if it is a valid own body (`0x4515b0`, feature flag `0x15a67c3`, mode flags `0x1945ec0/ec1/ee9`) it samples the current task/target with `0x56ea70`, debounces (same sample on 2 consecutive polls, `this+0x220`) and sends when it changed or 5000 ms passed since the last send. It also sends a stop (set = 0, body id from `this+0x154`) when the controlled body changes while an order was active.

**Open.**
- `0x56ea70` (how the client reads the host's current task, place, X, witness, by) is not read.
- Meaning of `X`; `0x565e70`, `0x565120` not read.
- The server tick that advances phase, witness, reorders.

### machine (kind 6) - machines: state record, craft queue, work reports
Vtable `0x1225198`, **v2**, period 1 s. Status: **partial** (record bytes 1-3, 4, 6, 0xa.. and key `d` have no decoded meaning; writer filters not all read).
Functions: gather `0x631670`, apply = thunk `0x6342b0` (`lea r9,[rcx+0x16c]; jmp 0x64aec0`; body `0x64aec0`), remove `0x6342d0` -> `0x64a8f0`, intent `0x632990`. Record decoder `0x62cc50`, encoder `0x62c350`. Slot 26 = stub `0xd5dd0` (no host upstream).

**Rows (server -> client).** Key **type 2 (place)** only; anything else -> counter `+0x1f4`, return 2. The value is the whole rest of the blob after the key (`0x543e40` copies all remaining bytes) and is parsed by `0x62cc50`, which returns 1 only if `ok && pos == len` (epilogue `0x62d1ec`).

Machine record (wire order; decoder `0x62cc50`, encoder `0x62c350` give the same order, **M**):

| field | type | limits | meaning (I) |
|---|---|---|---|
| kind | u8 | 1..6 (0 or > 6 bad) | machine kind (table behind `0x72e210`, not decoded) |
| b1 | u8 | `(b1 & 0xfc) == 0` (0..3) | opaque machine state |
| b2 | u8 | - | opaque machine state |
| b3 | u8 | - | opaque machine state |
| w4 | u16 | - | opaque machine state (power/fuel/fill/ripeness per topic summary) |
| w6 | u16 | - | opaque machine state |
| n | u8 | <= 8 (encoder writes max 8) | count |
| list | n x u16 | - | opaque |
| if kind == 5: w | u16 | stored rec+0x1a | - |
| if kind == 4: four u16 | 4 x u16 | stored rec+0x1c, 0x1e, 0x20, 0x22 | - |
| if kind == 4: m | u8 | <= 0xa0 | count |
| if kind == 4: bytes | m x u8 | stored rec+0x25.. | - |
| if kind == 2: craft queue | below | | |

Kind 2 craft queue (decoder after the kind-4 block; encoder tail of `0x62c350`):

| field | type | limits | meaning (I) |
|---|---|---|---|
| flag | u8 | written as `flag != 0` | - |
| q | u16 | encoder clamps a u32 to `0xffff` | progress/count |
| count | u8 | <= 0x10 (encoder `min(size, 0x10)`) | entries |
| entries | count x { str16 A, str16 B, u16, u8 } | A: 1..0x40 chars, printable 0x20..0x7e, no leading/trailing space (inline check). B: passes `0x62cbe0` (empty, or <= 0x40 printable, no edge spaces). | A = recipe/item name, B = second name, u16 = amount, u8 = flag |

Queue entry object = 0x58 bytes: A@0, B@0x28, u16@0x50, u8@0x52.

Value trailer forms accepted by apply `0x64aec0` (**M**; use **I**). The record is first tried on the whole value (end exact). If that fails:
- `record | extra[L] | u16 L | 'O'` (last byte `0x4f`): needs `len >= 4`, `L + 3 < len`, `L <= 0x1770`; record = first `len-L-3` bytes (exact). The `extra` bytes are kept as a string and applied only for kind 2 as a serialized building state (server side produced by `0xbabce0` / `0xbb8710` in `0x62f9c0`, which appends `blob | u16 len | 'O'`; the client loads it with `0xbb9070` / `0xbb7580`).
- `record | 'R' | u32 rev` or `record | 'S' | u32 rev`: last 5 bytes = marker (`0x52` 'R' or `0x53` 'S') + u32 LE; `rev != 0`; record = first `len-5` bytes (exact). 'R' sets a flag passed on to `0x64bac0` (writer emits `'S' - (flag != 0)`; **I**: R = row for a cell with a cell-host author). `rev` = per-row revision counter (`this+0x450`, incremented, never 0).

Return: 2 = bad (key not place / decode fail / counter `+0x1f4`); asm returns 1 at `0x64ba1f` (deferred/hold paths) and 2 at `0x64ba8d`; other paths leave `eax` from the callee. Apply then finds the machine object with `0x72c030(x*.5, y*.5, z*.5, d)` and requires its kind (`0x72e210`) == record kind; unknown location -> counter `+0x1fc` and deferral via `0x64a8f0`.
Gather (`0x631670`, **M**, partial): per viewer, loops over its machines, key via `0x543430`, record via `0x731500` + `0x62c350` (kind 2 may append the 'O' blob via `0x62f9c0`); a second pass over map cells around the viewer emits cached records `record | 'S'/'R' | u32 rev`. At most **0x100** rows per gather. Ownership filters `0x62f5a0`, `0x62f7b0` not decoded.

**Intent (client -> server).** Payload after the header: `u8 op`, then:

| op | name (log string, M) | fields after op (wire order) | effect / reply |
|---|---|---|---|
| 1 | "power" | key(place); u8 on/off (0 = off); end exact | machine power |
| 2 | "craft-add" | key(place); str16 A (<=0x40); str16 B (<=0x40); end exact; A non-empty | queue entry added |
| 3 | "craft-remove" | key(place); u8 idx; str16 A (<=0x40); end exact | queue entry removed |
| 4 | "craft-repeat" | key(place); u8 flag; end exact | repeat flag |
| 5 | own work report | handler `0x63d1a0`, below | item granted into the bag/machine |
| 6 | research work report | handler `0x63eac0`, below | research granted |
| 7 | host-words report | handler `0x63f6f0`, below | cell host reports machine state |

Names for ops 1-4 come from `0x63aa20` (strings `0x12240f8` "power", `0x1224100` "craft-add", `0x1224110` "craft-remove", `0x1224120` "craft-repeat"); ops 5-7 from log strings. For ops 1-4 the key must be type 2 and A/B must pass `0x62cbe0`; failure -> kind-1 notice "malformed". Other refusal texts: "that machine is not yours and none of yours stands by it", "already", "the queue is unreadable", "the game refused", "hand reach".
Client senders of ops 1-7 not located.

**op 5 own work** (`0x63d1a0`, **M**): key(place); `u32 body_id`; `str16 item` (<= 0x40, printable, no edge spaces); `u16 count` (1..0x40); `u16 engine_born` (<= count); `u16 calls`; `f32 progress` (finite); end exact. Log: `machine: own work - player P body B at (key), item "name" xCOUNT (engine-born E, calls C, progress F): granted/clamped ... into the bag / into the machine`. Checks and refusal texts: own-work switch `0x15a6497` ("own work is off on the server"), body in the sender's squad ("that body is not in your squad"), "that body is down / not on the server", machine kind ("not a mine without inputs here"), distance ("too far from that machine"), item mismatch (`that machine makes "X", not "Y"`), "nowhere to put it - bag, backpack and the machine are full". Field names are from the log format (meaning **I**).
**op 6 research work** (`0x63eac0`, **M**): key(place); `u32 body_id`; `f32 a`; `f32 b`; `u16 calls`; then optional (only if bytes remain): `u8 argp1` (arg = argp1 - 1), `f32 extra`; end exact. Strings: "a bad research report", "machine: research work of player ... refused - ...", "granted"/"clamped" "research" "(arg N, calls C)". a/b = amounts (**I**).
**op 7 host words** (`0x63f6f0`, **M**): `u16 count` (<= 0x100, else "a bad host-words report"), then `count x { key(place); u16 L (<= 0x4b0); L bytes = machine value }`. The value is a record, optionally `+ 'S' + u32 rev`; no 'O'/'R' handling. No `pos == len` check after the loop. Server logs "machine: player P now authors the machine at (...)", "spoke before its seed was offered", "its word waits for the seed". Meaning (**I**): a cell host reports the machines it authors, judged against cell ownership (`0x103e40`, `0x9a5e40`).

**Open.**
- Meaning of record bytes b1, b2, b3, w4, w6, the u16 list, and of key `d`.
- Machine-kind table behind `0x72e210` (1..6).
- Apply bodies `0x64bac0`, `0x64a590`; the full gather filters.
- Client senders of the intent ops.

### work (kind 7) - which body works at which machine seat
Vtable `0x1237248`, v1, period 1 s. Status: **full** (row, remove, host upstream row shape; task names unknown).
Functions: gather `0x798ca0`, apply `0x79bd30`, remove `0x79bf50`, host upstream (slot 26) `0x799f70`. Slot 12 = stub `0x7ffa80` (no client intent).

**Rows (server -> client).** Key type 1 (body) with id != 0, else return 2 (counter `+0x118`). Value, end exact:

| field | type | limits | meaning (I) |
|---|---|---|---|
| place | key type 2 (`u8 2` + 4 x u32, read by `0x543f90`) | `d` (key+0x14, "seat") <= `0xff` | x,y,z of the machine in half-units; `d` = work seat/slot index |
| task | u16 | in {`0x57`, `0x98`, `0x61`}, else return 2 (counter `+0x11c`) | job type (`0x57` OPERATE_MACHINERY, `0x98` OPERATE_AUTOMATIC_MACHINERY, `0x61` USE_TRAINING_DUMMY by the owntask name table) |

Anything else -> return 2. Effect: `{place key, task}` stored in a map keyed by body id at `this+0xa8` (capacity **0x200** rows, then return 2 / counter `+0x120`); counter `+0x110` = changed rows, `+0x10c` = accepted rows. Return 0 = ok. Meaning: body `id` works at the machine at `place`.
Remove (`0x79bf50`): key type 1 -> erase that body (`0x7a21c0`, counter `+0x114`).
Gather (`0x798ca0`, **M** for order): row key = body key, value = place key (written by `0x543aa0` from the `0x543430` output) + `u16 task` (`0x5439f0`), task filtered with the same set. A second pass writes the same shape from cached host-reported rows (`piVar20[1]` = task code, key coords from the cache).

**Upstream (host -> server).** Slot 26 `0x799f70`, signature `(this, vector<u32> body_ids (in, filtered in place), vector<Row> out)`. For each host body id: resolve the Kenshi character (`0x1965158`), sample its position (`0x72ca20`), drop bodies standing within a radius of each other (float at `0x12c87ac`, squared compare), cap position samples at `0x18`. For each body with an active work task (read via `0xb9bab0`, `task` in the set above and positive) it emits **the server's row shape**: key body:`id` (type 1), value = place key (x,y,z = seat/machine position from the body's work record, `d` = seat u32) + `u16 task`. Bodies without such a task are dropped from the list (counters `+0x18/+0x10`). A debug-flag branch (`0x19496a6`) emits up to 3 synthetic rows with task `0x57` at sampled positions plus a float offset (`0xeba92c`) (**I**: test servers only). Chunk framing: see "Upstream chunk framing".

**Open.**
- Task code names are known only through the owntask name table (above); no work-specific naming.
- Meaning of seat index `d`.

### construct (kind 9) - build requests (place / pause / dismantle / deliver / progress / upgrade / repair)
Vtable `0x121eea8`, v1, period 5 s. Status: **full** (intent side; the topic has no rows).
Functions: kind `0x5ac1e0`, name `0x5ac1f0`, gather `0x7ffa80` (no-op), intent `0x5ac220` (5281 B), apply `0x5ac200` (`xor eax,eax; ret`, no apply), notice `0x5b0ec0` (2236 B), client per-frame tick `0x5b1780` (2586 B, sends ops 4 and 5), senders `0x547b00` (kind 9, see below).
Other slots (M): slot 13 `0x5ac210` writes two floats 5.0 (`0x40a00000`) and 60.0 (`0x42700000`) to out-params (I: period window, unused here); slot 21 `0x5b21a0` (71 B) shrinks the pending list at `0x1965668..0x1965670`; slot 32 `0x5b21f0` (6442 B) is the text status "spine construct status"; slot 33 `0x5bb190` (62480 B) not read.

**Rows (server -> client).** None. No gather, no apply. The server answers each intent only with a kind-1 notice.

**Intent (client -> server).** Payload after the 5-byte header: `u8 op` (1..7; any other op -> notice "unknown request" `0x11be190`, counter `0x1965a34`), then the fields below. After the fields the value must end exact and `name` must pass the name validator `0x571d50(s, 0x40, ";=")` (length 1..0x40, no leading/trailing space, every byte 0x20..0x7e, no `;`, no `=`); else notice "bad request" (`0x11c28c0`) and the op is not run. `x`, `y`, `z` are three f32 world coordinates (f32 reader `0x543bf0`: NaN or |v| > ~3.0e38 clears `ok`); the server finds the structure by (name, x, y, z) through `0x593ab0` (place key builder `0x543430`, rounds x,y,z * 2). `name` = blueprint / structure name.

| op | fields (wire order, types, limits) | effect / reply (I unless M) |
|---|---|---|
| 1 | str16<=0x40 `name`; f32 `x`; f32 `y`; f32 `z`; f32 `q0`; f32 `q1`; f32 `q2`; f32 `q3`; u32 `floor` (two u16; allowed -1..0x40); u8 `flag` (bool) | place a building. `q0..q3` = rotation quaternion (Ogre order, default 1,0,0,0). Server handler `0x592350`. `flag` is stored at record +0x98 (meaning not decoded) |
| 2 | str16 `name`; f32 `x`; f32 `y`; f32 `z`; u8 `paused` (1 = paused, 0 = resumed) | pause / resume a building site. Handler `0x5af080`; sets `site+0x9a = paused` (M) |
| 3 | str16 `name`; f32 `x`; f32 `y`; f32 `z` | dismantle / cancel. Handler `0x5aff10`; owner, clan leader or a ruin may do it |
| 4 | str16 `name` (site); f32 `x`; f32 `y`; f32 `z`; str16<=0x40 `item` | deliver one item into a site (removed from the squad body's bag). Handler `0x5ae2b0` |
| 5 | str16 `name`; f32 `x`; f32 `y`; f32 `z`; f32 `a` (progress; >= 0 and <= 1e6); f32 `b` (builder effort k; >= 0); u16 `n` (steps) | progress report: the host client of a player-hosted zone reports build progress. Handler `0x5aa210`. `n == 0 && b <= 0 && a > 0` = catch-up (the client's game already has it built) |
| 6 | str16 `name` (current); f32 `x`; f32 `y`; f32 `z`; str16 `target` (name to upgrade to) | upgrade. Handler `0x5ad6d0` |
| 7 | str16 `name`; f32 `x`; f32 `y`; f32 `z`; f32 `fraction` (of max durability to repair; <= 0 -> "nothing done"; clamped to 0.25, const `0x3e800000` at `0xeb6fc4`) | repair. Handler `0x5af5e0` |

Senders (all `0x547b00`, kind 9, op byte first; M): op 1 `0x5a4c40..0x5a4e85` (`mov r8b,1`); op 2 `0x59eec0` (call `0x59f004`, u8 inserted at the end); op 3 `0x59f040` (call `0x59f14c`); op 4 inside the client tick `0x5b1780` (call `0x5b1e26`); op 5 `0x5abd60` (call `0x5abfd3`; a scale `0x15a6660` is applied to the two floats; `n` clamped to 0xffff); op 6 around `0x5a0900` (call `0x5a0a12`); op 7 `0x59fe00` (call `0x5a01be`; value = ratio of two floats; rate-limited by `0x19487f0` and `0x1076ec0`). Each send returns the request id (= seq); `0x5a0280(seq, op, name, x)` records it in the pending list `0x1965668` (0x68 B per entry: seq @0, op @4, name @8, place @0x30..0x38).

Server-side checks and refusal texts per op (strings M, meaning I). Every refusal and success goes back as a notice:
- op 1 (`0x592350`): "unknown player", "bad blueprint name", "the admin list builds it", "your role in the faction may not build - ask the leader for a role that can" (role bit 8, `0x697250`), `this land belongs to the town "X" of another faction`, "bad place" (x or z outside [-1.0e6, 1.0e6] = `0x12c2598`, y outside [-1.0e5, 1.0e5] = `0x12c1b44`, floor < -1 or > 0x40), "bad rotation" (q0^2+q1^2+q2^2+q3^2 must be in (0.8, 1.2): `0xeb9b5c` = 0.8f, `0xebab0c` = 1.2f; stored normalised), "unknown blueprint" (lookup `0x5742b0`), "not a player blueprint" (property "is node" or "is foliage"), research check ("places X that the server's research of that faction does not enable - taken ...", refused if `spine construct researchstrict 1`), "campfire limit reached" (cap `spine construct firecap`), "someone already builds there", "already placed", "structure limit reached" (200000 records), "zone not loaded on the server - raised (and its place judged) when it loads". Also needs a squad body within range of the spot (distance <= `0x15a6538`). Success text "placed" (log `construct: <player> placed "<name>" (...) at (x,y,z) floor N faction "..." ...; progress ...; squad ...`).
- op 2: "no such structure", "not yours" (owner player compare at +0x50), "not standing on the server right now", record-only note when the zone is hosted by a player.
- op 3: "already gone", "not yours", "a finished building is dismantled by a worker", "N thing(s) stand inside (...) - take them down first", storage not empty without a key.
- op 4: "no such structure", "already built", "it needs no more of that", "too far from the site", `no "X" in the bag (...)`, "the site did not take it"; success "delivered".
- op 5: "no such structure", "not the host of that zone [, and no body of yours near the site]", "bad numbers" (NaN, negative, `a` > 1e6), "progress beyond what was delivered", "the builders near the site allow k ... per k" (rate cap vs `0x1948848`); catch-up refused if the structure stood once ("it stood once - a repair mends it, not a catch-up"). Success texts "built" / "complete".
- op 6: "upgrades are off on this server", "already upgraded", "finish building it first", "it does not upgrade to that", "not researched", "an upgraded one already stands there"; success "upgraded".
- op 7: "finish building it - building raises its durability", "it is in good repair", "nothing done", "your own character must be within 80 m of it", `repair needs one "X" in your character's bag`, "a ruin is not repaired - dismantle it", "this does not wear out"; success "repaired".

**Notices.** Every op ends with `0x547e50(player, kind 9, code, flag, text)`; **code = the client's own seq** from the intent header (global `0x194830c` is assigned from the 4 header bytes just before slot 12 is called in `0x540590`; M), flag 1 = success. The client notice handler `0x5b0ec0(code, flag, text)` looks the code up in the pending list and acts on the pending op (@4): op 1 flag 1 -> marks placed, flag 0 -> logs `construct: <name> - <text>`; ops 4, 2, 5, 7 similar retry / log handling (partly read).

**Open.**
- Slot 33 `0x5bb190` (62480 B) unread.
- Meaning of op-1 `flag` (record +0x98) and of op-5 `a`, `b`, `n` beyond the strings above; slot-13 floats; pending-list internals (`0x5a0280`).

---

### structure (kind 10) - placed structures (full records, 4 per-viewer variants, delta tell)
Vtable `0x121efc8`, v1, period 1.0 s (slot 4 `0x7fba50` loads 1.0f from `0xeba914`). Status: **full** (row); gather and delta machinery partly read.
Functions: gather `0x5b42a0` (825 B), apply `0x5b5110` (thunk: `mov r8b,1; jmp 0x5b4f40`; `0x5b4f40(key, reader, flag)`), apply without building (slot 18) `0x5b5130` (same decode, flag 0), remove `0x5b5150`, row decoder `0x578930`, row encoder `0x5780d0`, client applier `0x5a2e10` (7640 B). No intent handler (slot 12 = `0x7ffa80`).
Other slots (M): slot 8 `0x5b3b40`, slot 9 `0x5b4850`, slot 10 `0x5b45e0`, slot 11 `0x5b3c30`, slot 19 `0x5b5940` (deferred-removal retry list), slot 21 `0x5b5430`, slot 23 `0x5b5d90`, slot 24 `0x5b61c0`, slot 32 `0x5b64a0`, slot 33 `0x5ca5a0`.

**Rows (server -> client).** Key: **type 2 (place)**, no explicit type test; the apply recomputes the key from the decoded record and compares all six dwords (type byte, `+4,+8,+0xc,+0x10,+0x14`) with the row key. Expected key = `0x543430(x, y, z, d)` with x, y, z = the record's first three floats and `d` = FNV-1a-32 (basis `0x811c9dc5`, prime `0x1000193`) of the structure `name` (asm `0x5b5030..0x5b5063`). Place key words: `+4 = 0`, `+8 = floor(x*2+0.5)`, `+0xc = floor(y*2+0.5)`, `+0x10 = floor(z*2+0.5)`, `+0x14 = d` (|v| > ~1e9 -> 0). Mismatch -> counter `0x19655d8`, nothing applied; decode failure -> counter `0x19655dc`.
Value = one record, decoder `0x578930` (returns 1 only if all checks pass; asm `0x579000..0x57911b`). "fx" = s32 (read as u32) divided by 100.0 (const `0xebcd9c`). Name validator `0x571d50` with `";="` ("name ok").

| field | type | limits | meaning (I) |
|---|---|---|---|
| `name` | str16<=0x40 | name ok | structure / blueprint name (record +0) |
| `x` | f32 | abs <= 1.0e6 (`0x12c2598` / `0x12dfbcc`) | world x (rec +0x78) |
| `y` | f32 | abs <= 1.0e5 (`0x12c1b44` / `0x12dfbc8`) | world y (+0x7c) |
| `z` | f32 | abs <= 1.0e6 | world z (+0x80) |
| `q0`..`q3` | 4 x f32 | q0^2+q1^2+q2^2+q3^2 within [0.8, 1.2] (`0xeb9b5c`, `0xebab0c`); default 1,0,0,0 | rotation quaternion (+0x84,+0x88,+0x8c,+0x90) |
| `floor` | u32 (s32) | -1..0x40 | floor (+0x94) |
| `flags` | u8 | see flag bits below | bit0 -> +0x98, bit1 -> +0x99, bit2 -> +0x9a, bit3 -> +0x9c, bit4 -> +0x9b, bit5 -> +0x9d, bit6 -> +0x9e, bit7 -> +0xd0 |
| `s1` | str16<=0x40 | passed through `0x571e80` (I: sanitiser) | unknown (+0x28) |
| `owner` | str16<=0x20 | | owner player name (+0x50); compared with the viewer's name |
| `a` | fx | | unknown (+0xa0) |
| `b` | fx | | I: structure progress value (+0xa4; the construct progress judge uses it) |
| `n` | u8 | n <= 0x10 else bad | item count |
| n x item | { str16<=0x40 `name` (name ok), fx `required`, fx `delivered` } | | vector at +0xa8, element 0x30 B (name 0x28 B, `required` @0x28, `delivered` @0x2c); I: construct op 4 "needs no more of that" and ops 5/7 use sum(delivered)/sum(required) |
| `cur` | u8 | `cur <= max` required | durability current (+0xc8) |
| `max` | u8 | | durability maximum (+0xcc) |

Value must end exact (`ok && pos == len`). Floats: f32 reader `0x543bf0` (NaN or beyond +-3e38 -> not ok). Writer cross-check (M): encoder `0x5780d0` (called from `0x59dc90` for four variants) writes exactly this order; fx = `round(v*100)` clamped to +-1e9 (`0xc4653600` = -1e9), `cur` = clamp(rec+0xc8, 0, max), `max` = clamp(rec+0xcc, 0, 255), item count = min(size, 0x10).
Flag bits (M, decoder / encoder; meanings I): bit0 = rec+0x98 (set from construct op-1 `flag`); **bit1 = rec+0x99 "built / complete"** (construct "finish building it first", "already built"; log `complete=`); **bit2 = rec+0x9a "paused"** (construct op 2); bit3 = rec+0x9c; bit4 = rec+0x9b; bit5 = rec+0x9d; bit6 = rec+0x9e; **bit7 = rec+0xd0 "ruin"** (construct op 7 "a ruin is not repaired"). bit3 / bit5 / bit6 are the three **per-viewer** bits (see gather). The applier copies them to the client mirror: bit3 -> mirror +0x48 (log " (yours)"), bit5 -> +0x49, bit6 -> +0x4a, bit0 -> +0x1f1, bit1 -> +0x1f2, bit7 -> +0x1f0 (`0x5a2e10` prologue, M).

Effect on the client (`0x5a2e10`, M partial): keeps a mirror record per structure (map via `0x5dc5c0`). With flag 1 (slot 14): waits until the zone is loaded (`0xc1dc60 == 1`), looks for an existing structure at the spot (`0x59e770`), adopts it ("already stands here - adopted, not built twice") or builds it from the blueprint (`0x5742b0`; log `construct: built "X" at (...) - complete=... progress ... zone=...`), gives up after 3 tries ("was built here 3 times and is still not found at its spot - leaving it"); if the player's own game has something the server's record says is gone it sends construct op 3 (cancel) or a catch-up report (construct op 5). Slot 18 (flag 0) only stores the mirror, no building.
Remove (`0x5b5150`): key must exist in the mirror (`0x5e2740`); zone loaded -> structure taken down (log "(gone on the server) taken down here once its zone loaded"; slot 19 `0x5b5940` = deferred retry list, cap 0x100 entries); zone not loaded -> queued with a deadline.

**Gather (slot 5 `0x5b42a0`, M).** Per viewer: builds viewer info with `0x5b40a0` (viewer name from `*(u32*)param_2` via `0x582c20`; two lookup sets at info+0x28 (filled by `0x697790` when settings `0x15a6595` and `0x15a6593` are on) and info+0x48 (`0x6d49c0`, setting `0x15a6599`)), then takes the sorted index list of ALL structure records (`0x5b3f70`, sorted by key with `0x5ed630`; array `0x19656c8`, 0x2f8 B per record, key at rec+0x110: type byte then `+0x114..+0x124`). For each record it picks one of 4 cached value strings (`rec+0x170 + idx*0x28`, hash at `rec+0x290 + idx*4`; cache rebuilt by `0x59dc90` when `rec+0x164 != rec+0x168`, via `0x5780d0`); selector `0x5b4150` (asm `0x5b41a2..0x5b426f`):
- idx 0 = default (all three per-viewer bits clear);
- idx 1 = viewer is the record's owner (rec+0x50 == viewer name) -> bit3 set;
- idx 2 = owner (hashed by `0xce0460`) is in viewer set +0x28 (I: same faction / allies) -> bit5 set;
- idx 3 = only when idx would be 0 and the record's zone key (`0x6d31c0` of x,z) is in viewer set +0x48 (I: a town / zone the viewer hosts or sees) -> bit6 set.
Rows are appended to the tell list at `param_3+0x20` (entry = 0x18 B key copy + value ptr + hash); flag `param_3+0x41` is cleared if keys are not appended in ascending order. No row cap and no per-viewer culling seen (every structure goes to every viewer; only the 4 variants differ). After the rows, viewer state `0x5dc3c0(viewer)`: `[6]` = signature hash of the viewer relation sets (`0x5b4600`: `"\x01-"` or `"\x01L"` + each id of both sets), `[0xc]` = now (QPC seconds), `[0]`/`[1]` = floor(viewer.x / `0xebe4bc`) + 0x20 and the same for z (cell coordinates).
Delta machinery (partly read): slot 8 `0x5b3b40` returns a 64-bit version token (global structure change counter `0x194843c` mixed with a time bucket), slot 10 `0x5b45e0` returns `0x15a6548`, slot 9 `0x5b4850` (1763 B) picks full / delta mode per viewer (state values -1..3 from cell change and time limits), slot 11 `0x5b3c30` is a budgeted background sweep over the record list.
Author: server (from its structure records, including pieces placed through construct); the client never sends this topic; only construct intents change it.

**Open.**
- Meaning of `s1` (+0x28), `a` (+0xa0), flag bits 0 and 4; bit names are inferred only.
- Slots 9, 11, 21, 23, 24, 32, 33 not fully read; the caller of slot 18.

---

### bodies (kind 11) - no wire data
Vtable `0x1212f98`, v1, period 30 s. Status: **full** (nothing to decode).
No client apply (slot 14 = `0x935c40`, returns 2 = unsupported), gather slot 5 is the no-op stub (no rows), intent slot 12 stub, host upstream slot 26 = stub `0xd5dd0`. Slots 23/24 (`0x560d20`/`0x560df0`) are server-side helpers, not wire handlers. In this version the topic carries nothing of its own (I: local bookkeeping or placeholder; body state travels in other topics).

### buildings (kind 12) - town buildings the server knows near the viewer
Vtable `0x1213208`, v1, period 5.0 s (slot 4 `0x935c60` loads `0xeb512c` = 5.0f). Status: **full** (row); gather read at row level.
Functions: kind `0x561630`, name `0x561640` (string `0x11ba6f8`), gather `0x561650` (2400 B), apply `0x561fb0` (2037 B), remove `0x5627b0` (216 B). No intent handler (slot 12 = `0x7ffa80`).
Other slots (M): slot 0 `0x563ca0`, slot 21 `0x562890`, slot 23 `0x5628a0` (1735 B), slot 24 `0x562f70`, slot 32 `0x5630e0` (status text).

**Rows (server -> client).** Key: place key; the type byte is not tested. The apply reads `key+8,+0xc,+0x10` = x*2, y*2, z*2 (world position = value * 0.5, const `0xeba8bc`) and `key+0x14` = `d`. **`d` must equal FNV-1a-32 (basis `0x811c9dc5`, prime `0x1000193`) of the row's `name`**; else counter `this+0x28`, nothing applied. Key builder `0x543430(x,y,z, FNV-1a(name))`.
Value, decoded in this order (M). The four quaternion fields are not floats: s32 read as u32, divided by 10000.0 (const `0xeb7a54` = `0x461c4000`):

| field | type | limits | meaning (I) |
|---|---|---|---|
| `name` | str16<=0x40 | must be non-empty (apply); no charset check | building name |
| `q0` | s32/10000 | default 1.0 | rotation quaternion w (Ogre order) |
| `q1` | s32/10000 | default 0 | rotation x |
| `q2` | s32/10000 | default 0 | rotation y |
| `q3` | s32/10000 | default 0 | rotation z |
| `n` | u32 (two u16) | default 0xffffffff | variant / slot ordinal (stored at local +0x2c0, passed to the game record) |
| `flag` | u8 | stored as bool | "has sub-state / is ruined" |
| `variant` | str16<=0x40 | | passed through `0x561200` -> `0xcd6100` (lookup of a second named object in the game data at `*(0x195a898+0x4a8)`); sanitised copy for logs by `0x561500` (replaces `\|` and bytes outside 0x20..0x7e with `?`) |
| `extra` | str16<=0x40 | read, not used by the apply (M: only its read advances the reader) | unknown |

No exact-end check; only the reader `ok` flag (overrun) is tested. Writer cross-check (M): `0x561650` writes `name` (u16 len + <=0x40), `(int)(q*10000.0)` as two u16 for each of the 4 floats, u32 `n`, u8 `flag`, str16 `variant` (<=0x40), str16 `extra` (<=0x40, from the building's own string or a fallback): same order.
Apply flow (M): zone at (x,y,z) must be loaded (`0xc1dc60 == 1`, else counter `+0x2c`); an existing building of the same `name` within squared distance <= 4.0 (`0xeb99bc`; scan radius `0xebabdc`; `0x563610`) of the spot -> counter `+0x1c`, nothing built; template lookup by name `0x5611c0` -> game table at `0x195a898+0x20` (entry type @+0x50 must be 0), not found -> counter `+0x28`; place with `0x561440` -> `0x561240` (returns 0 -> counter `+0x24`); success -> counter `+0x20`, key inserted in the "built" set (`this+0x38`), and (first 8 times) the log line `buildings: built "<name>" of [<variant>] at (x,y,z) - the server has it, this world did not`.
Remove (`0x5627b0`): erases the key from the built-set; zone loaded -> finds the building at the key (`0x563970`) and removes it (`0x561480`; counter `+0x30`), else counter `+0x34`.

**Gather (slot 5 `0x561650`, M at row level).** Per viewer (position `param_2+0xc` = x, `param_2+0x14` = z): asks the game for buildings within radius 1800.0 (`0xeb983c` = `0x44e10000`) of the viewer, max 0x1000 results (`0x5610e0` -> `0xcd68c0(out, pos, radius, 0, 0x1000)`); for each: gets the building record (`0xb9bad0` or `0x5610c0` -> `0x560eb0`) with name / pos / quaternion / extra fields; skips ones already told through another path (`0xcbcfb0(name,pos)` true) and record-less ones (counters `+0x10`, `+0x14`, `+0x18`); sends a row when the building is within radius^2 `0x1305b48` (float const, value not read) of the viewer OR already in the viewer's known map (`0x563dd0` / `0x15cc00`). Rows are kept in ascending key order (flag `param_3+0x40` cleared otherwise). No fixed row cap except the 0x1000 query limit. Counter `+0xc` = rows written.
Author: server, from the world's town buildings it knows. No client intent.

**Open.**
- Constants `0x1305b48` and `0xebabdc` not read; slots 21, 23, 24, 32 not read.
- Meaning of `n`, `flag`, `extra`; what `0x561240` does inside the game.

---

### groups (kind 13) - player factions and groups: PvP rules, own faction state, roster, public cards, town/shop lists, faction chat
Vtable `0x1226218`, v6, period 5 s. Status: **partial** (rows fully decoded; field meanings of ids 2 n3/c/d/e, 3 member `c`, 4, 8, 11, 16-47 unknown; server op execution not read).
Functions: gather `0x655170` (per player), apply `0x65acb0`, remove `0x65aec0` (not decoded), intent `0x659b20`, senders `0x664320`, `0x66f900`, `0x65ce40`.

Local conventions (all M): `XID` = `PFID` or `PGID`; `PFID` = `anarchy-pf-` + 16 lowercase hex (len 27, validator `0x60f0b0`, prefix at `0x121f4e8`); `PGID` = `anarchy-pg-` + 16 lowercase hex (len 27, validator `0x696b30`, prefix at `0x121f4c0`); the apply additionally checks chars 11..26 are `0-9a-f` where it checks inline. "printable" = every byte in 0x20..0x7e. f32 here is the NaN/|v|>3e38-rejecting reader.

**Rows (server -> client).** Key type 5 (global, dispatch on id) or type 3 (name = card); any other key type is bad (return 2). Global ids 0, 12-15 and >= 48 are bad. Every accepted row ends with an "end exact" check; a failed row bumps `this+0x20` and returns 2; accepted rows bump `this+0x1c` / `this+0x210`.

| key | value layout (wire order) | limits | meaning (I) |
|---|---|---|---|
| global 1 | `u8 rule`, `u16 n1`, n1 x `str16<=0x60` (XID), `u16 n2`, n2 x `str16<=0x20` (non-empty, printable); end exact | rule < 2; n1, n2 <= 256 | rule = server PvP rule (`0x61f490` prints "pvp: the server's rule is " OPEN / WAR ONLY, plus count of n1 as "factions at war with me"); n1 = XIDs at war with me; n2 = nick list, purpose unknown. Writer rule = `0x19488d1 != 0`, lists via `0x698eb0` / `0x6986b0`, counts clamped 256 |
| global 2 | `str16<=0x60 G` (XID), `u8 state`, `u8 role`, `u8 n1`, n1 x {`str16<=0x60` PGID, `str16<=0x20` non-empty printable}, `u8 n2`, n2 x {`str16<=0x60` XID, `u8 k` in {1,2}}, `u8 n3`, n3 x {`str16<=0x60` XID, `u8 k` in {1,2}}, `u8 c`, `u32 d`, `u32 e`; end exact | state < 2, role < 3, n1 <= 16, n2 <= 64, n3 <= 64, d < 0x400; writer clamps c ("1 if >= 256") | my faction state: state 1 = member ("You are in ... now"), role 2 = leader ("You lead ... now"; role 1 suppresses offer messages); n1 = invitations {PGID, inviter nick} ("... invites you to ... - type /faction accept"); n2 = incoming offers (k 1 "offers an alliance", k 2 "offers peace"); n3 = second XID+kind list (outgoing offers or current relations, unknown); c, d, e unknown. Writer sends the row only if a pointer (`lStack_648`) is non-zero |
| global 3 | `str16<=0x60` PGID, `u8 n`, n x member {`str16<=0x20` name (non-empty, printable), `u32 a`, `u8 r`, `u8 c`, `u8 m`, m x `u32`}; end exact | n <= 64, r <= 2, m <= 32; PGID strict (len 27 + hex); string max 0x60 proven by `0x66844d mov r8d,0x60` | my group's roster, sent only when member: a = lookup key into server map `this+0x78`, r = role (same range as id-2 role), m x u32 from `0x6210f0(entry+0x20)` (maybe character ids), c unknown (writer clamps "1 if >= 256"). Writer written only when id-2 state == 1 |
| global 4 | `u8 n`, n x `str16<=0x20`; end exact | n <= 64 | stored vector at `this+0x4c8`; candidate = foe list (ops 19/20 take a nick); weak. Writer list from `0x69b760` / `0x69bd30(this+0x628)`, clamp 64 |
| global 5 | `u8 n`, n x {`u8 a`, `u32 b`, `str16<=0x18` (non-empty)}; end exact | n <= 12 | unknown |
| global 6 | `u16 n`, n x {`str16<=0x20` (non-empty), `u8 flag`, `str16<=0x40`, `u32 v`}; end exact | n <= 256, flag <= 1 | faction relation list (log "... left ... - now on your enemy list" is in this branch) |
| global 7 | `u8 n`, n x **area**; end exact | n <= 16 | claimed areas, see below |
| global 8 | `u8 nc`, nc x column {`str16<=0x20` (non-empty, printable), `u8 b` (stored as bool)}, `u8 nr`, nr x row {`str16<=0x60` (non-empty, printable), `u16 v0`, nc x `u16 v`}; end exact | nc <= 64, nr <= 64; value = v - 20000 (writer writes v + 20000) | matrix nc x nr of signed values (-20000..45535), stored at `this+0x328/0x348/0x368/0x388/0x3a8`; candidate = role/permission or relation table, unconfirmed |
| global 9 | `u8 n`, n x {`str16<=0x20` (non-empty, printable), `u32 t`}; end exact | n <= 64 | members' last-seen: name from id-3 roster; t = 0 if online flag (`+0x50`) set, else (now - last)/60 minutes (`_time64`) if 0 < last <= now, else 0xffffffff. Map string -> u32 at `this+0x3c8` |
| global 10 | `u8 f`; end exact | f <= 1 | server `pgroup ffire` rule -> `0x61fbb0(1, f == 1)`: prints "1 - no fighting inside a group or between allies" / "0 - fights inside a group and between allies are let through". Writer `0x15a6471 != 0` |
| global 11 | `u32 h`, `u8 n`, n x {`u32 a`, `str16<=0x20` (printable, may be empty), `u8 k`, `u32 b`, `str16<=0x40` (printable)}; end exact | n <= 20, k < 4 | h -> `this+0x4ec`, flag `this+0x4e8` = 1, vector (0x68 B elements) at `this+0x4f0`; candidate = treasury/log entries; weak. Writer clamps h, b >= 0; only if `cStack_3f8 != 0` |
| global 16+i, i = 0..15 | **chunk A**: `str16<=0x60 S` (non-empty, printable), `u16 n`, n x record A; end exact | n <= 256 | chunks of records keyed by `@x,y` strings (towns / plots, ops 21-28; weak). Stored in map `this+0x2e0`, key i = id - 16. Writer: element stride 0x168, id = index + 0x10 |
| global 32+i, i = 0..15 | **chunk B**: `str16<=0x60 S` (non-empty, printable), `u8 n`, n x {`str16<=0x60` (non-empty, printable), `u8 m`, m x record B}; end exact | n <= 64, m <= 20 | per-chunk lists of priced/quantified named items (shop/storage listings, ops 29-31; weak). Map `this+0x300`, key i = id - 32. Writer only when the entry's second list is non-empty, id = index + 0x20 |
| name (type 3) | **card**; end exact | see card | public card of a faction (isGroup 0) or group (isGroup 1). Writer `0x654600` (called from gather) same order |

Apply and writer order agree for all rows above (writer blocks in `0x655170`).

**Area** (id 7 element; apply post-checks: string bytes printable, one float in (0, 50000) via `0xeb5f44`):
1. `str16<=0x60` A; 2. `str16<=0x40` B
3. `f32 x`, `f32 y`, `f32 z` (struct +0x68/+0x6c/+0x70)
4. `str16<=0x20` C
5. `u8 flags`: bit0 = struct +0x74, bit1 = struct +0x75
6. `str16<=0x60` D; 7. `str16<=0x40` E
8. `u16 a`, `u16 b`, `u16 c` (writer clamps 0xffff)
9. `u16 m` (<= 0x1000), m x {3 x `u16` = coordinate - base + 0x8000}; writer keeps only points with |delta| < 30000; base = a per-area int (x uses a separate base)
10. `u8 q` (<= 8), q x {`str16<=0x10`, `str16<=0x40`}
Meaning (I): a faction's claimed territory (outline points around a centre) with labelled sub-entries.

**Record A** (chunk A element; writer stride 0xf8):
1. `str16<=0x60 K` (non-empty, printable), parsed as `...@x,y...` (x = text between `@` and `,`, y = text after `,`, `atof`), then `floor(v / 100.0)` (`0xebcd9c` = 100.0) -> fx, fy
2. `u8 p` (< 16); 3. `u8 q` (< 3)
4. `str16<=0x40` (printable, may be empty)
5. `u8 flags`: bit0 = has outline, bit1 = has value V, bit2 = src +0xc0, bit3 = src +0xc8
6. if bit0: `u32 w` (writer clamps >= 0), `u16 days` (<= 365), `u8 np` (<= 16), np x {`u16 x' = px - fx + 0x8000`, `u16 y' = py - fy + 0x8000`}
7. if bit1: `u16 V` = ceil(float) clamped 0..65535 (`0x12c1c18` = 65535.0)
8. `u8 flags2`: bit0 = src +0xc9, bit1 = src +0xd4, bit2 = src +0xd5, bit3 = (src +0xdc == 1), bit4 = src +0xca
9. `u8 s` (<= 100; writer clamps 255); 10. `u8 t` (writer clamps 255)
11. `u16 W` = ceil(float) clamped 0..65535
12. `u16 c` (<= 1000; writer clamps 0xffff)
13. `u32 e1`; 14. `u32 e2` (writer clamps 0..0xffffffff)
Writer stops a chunk at 256 records or when it would pass 15000 bytes (counter `this+0x320`).

**Record B** (chunk B element; writer stride 0x90): `u32 a`, `str16<=0x20`, `str16<=0x40` (max proven by `0x65e543 mov r8d,0x40`), `u16 b` (writer clamps 0xffff), `u32 c` (writer clamps >= 0), `str16<=0x40`; strings printable. Same 15000-byte cap per chunk.

**Card** (name row; writer source element stride 0xf0 in the server list at gather `this+0x58`):
1. `str16<=0x60 ID`: must pass `0x60f0b0` or `0x696b30` (XID); key id = FNV-1a32 of ID (`0x5433f0` writer side, inline hash on apply side; no salt, other key words 0)
2. `str16<=0x40 NAME` (non-empty, printable)
3. `u8 isGroup` (< 2): must be 1 exactly when ID is a PGID
4. `str16<=0x20` (non-empty, printable) (I: a nick, leader?)
5. `u16 cap` (<= 64); 6. `u16 count` (<= cap)
7. `u8 nrel` (writer clamps 255; no apply cap found), nrel x {`str16<=0x60` XID, `u8 k` in {1,2}} (I: relations, same codes as id 2)
8. `str16<=0x40` (printable, may be empty) (I: description)
9. `u8 n2` (<= 64), n2 x {`str16<=0x20` (non-empty, printable), `str16<=0x18` (printable)}
Gather sends at most 0x200 cards per pass, only for entries linked to the player (own XID, id-2 invitation/offer lists, relation lists; set built in gather).

**Intent (client -> server, kind 0xd, handler `0x659b20`).** Payload after the 5-byte header: `u8 op`, then:
| op | fields | effect / reply |
|---|---|---|
| 100 (0x64) | `str16<=100 text`; end exact | faction chat via `0x65a530` (strings "[Faction] ", "groups: faction chat of ... to ... member(s)", "you are not in a faction - rename yours or join one to use faction chat") |
| any other (1..31) | `str16<=0x400 arg`; end exact (else reply "bad request" via `0x547e50`) | executed by `0x699030(state, op, arg, out)`; op 31 goes through `0x6ed190` first; log `groups: player <n> <opname> "<arg>" -> ok / REFUSED (...) - ...` |

Op names (M, table `0x1227ea0`, 32 pointers): 0 `?`, 1 create, 2 invite, 3 accept, 4 decline, 5 leave, 6 kick, 7 leader, 8 rename, 9 ally, 10 war, 11 peace, 12 disband, 13 setrole, 14 roleadd, 15 rolename, 16 roleperm, 17 rolemove, 18 roledel, 19 foeadd, 20 foedel, 21 towncreate, 22 townname, 23 townmayor, 24 towndel, 25 townset, 26 plotset, 27 townland, 28 townrent, 29 storage, 30 treasury, 31 shopbuy. Arg required (`0x696e80`): ops 1, 2, 6-11, 13-30; not required: 3, 4, 5, 12, 31.
Senders: `0x664320` (`/faction` command parser: name -> op via `0x696d60`, scans table entries 1..31; arg printable, <= 0x400; `u8 op` + `str16<=0x400`, `0x547b00` reliable, kind from the topic's vtable getter; usage strings "faction: plain latin letters, up to 64", " <faction name>", " <nick>", " <faction or nick>"); `0x66f900` (for `/faction say`, called from `0x664320`: `u8 0x64` + `str16<=100`, `0x547b00` kind 0xd, call at `0x66fa57`); `0x65ce40` (called from `0xb60880`: `u8 0x1f` shopbuy + `str16<=0x400`, `0x547b00` kind 0xd, call at `0x65cf6d`).

**Open.**
- Meanings of id-2 n3/c/d/e, id-3 member `c` and its u32 list, ids 4, 5, 8, 11, 16-47 fields (candidates above only).
- Card `nrel` has no apply cap (writer clamps 255).
- Writer side of ids 16-31 / 32-47: no cap of 16 chunks seen in gather; a 17th chunk would land on id 32 and be misread; source list cap not checked.
- Remove slot `0x65aec0` not decoded.
- Server op execution `0x699030` (checks per op) not read.
- Callers of `0xb60880` (shopbuy sender; its caller `0xb61a00`) not followed.

---

### bodyfaction (kind 14) - faction of each other player's body
Vtable `0x12254e8`, v1, period 4 s. Status: **full** (squad-vector element layout not decoded).
Functions: gather `0x651130`, value builder `0x652900` (cached via `0x652ba0`), apply `0x6516a0` (slot 14: `xor r9d,r9d; jmp 0x652d40`) and `0x6516b0` (slot 18: `mov r9b,1; jmp 0x652d40`) -> body `0x652d40(this, key*, reader*, bool again)`, remove-row slot 15 `0x6516c0`, faction setter `0x61e0e0`. No intent, no upstream (slots 12/26 stubs).

**Rows (server -> client).** Key (M, first test `*key != 1` in `0x652d40`): type 1 `body:`, `u32 id` = body entity id; other type -> `this+0x3c`++, return 2.
| field | type | limits | meaning |
|---|---|---|---|
| faction name | str16<=0x60 | length 1..0x60, chars 0x20..0x7e | key of the Kenshi faction the body belongs to (`0x61e0e0` is called with it) |
| faction label | str16<=0x40 | length 1..0x40, chars 0x20..0x7e | shown in log `bodyfaction: body <id> moved "<old>" -> "<label>"` (strings `0x12254c0`, `0x12254b0`, `0x11d837c`) |

End exact, else `this+0x3c`, return 2. `again` = 1 (slot 18) only changes the log suffix " (the engine had put it back)" (`0x1225490`) (I: re-apply after Kenshi reverted the body's faction).
Apply flow (M, `0x652d40`): hook `*0x1965150()` == key id -> skip (`this+0x44`, return 2; I: re-entrancy guard / local avatar). Body lookup via hook `*0x1965158(id)`; none -> `this+0x28`, return 1 (hold). Then `0x61e0e0(body, name)` returns a code 0..8: 0 faction set (`0x23c90` setFaction), 1 already in that faction, 2 body already in the receiving player's own faction, 3 target faction invalid/flagged, 4 target unknown but known-pending (`0x60f150` true), 5 unknown faction, 6 apply not allowed now (`0x60f070` false), 7 bad name or no body, 8 lookup/set failed. Topic return (switch table `0x6536b4`): codes 0,1,2 -> 0 (ok); 4,6 -> 1 (hold); 3,5,7,other -> 2 (bad). Counters: code 0 `this+0x18` (`+0x1c` if again), 1 `+0x20`, 2 `+0x24`, 4 `+0x2c`, 6 `+0x30`, 3 `+0x34`, 5 `+0x38`, 7 `+0x3c`, other `+0x40`. Log rate-limited to 0x20 lines (`this+0x5c`).
Gather (M, `0x651130`, player id `*param_2`): collects body ids of other players' squads (vector with 0x38-byte stride: owner id @0, id vector @0x18..0x20; viewer's own skipped), at most 0x200 rows per call (overflow `this+0x14`), skips bodies whose faction name equals the viewer's own faction string (`0x616150`, counter `this+0x10`); key `{type 1, id}`, value from cache row `+0x30`.

**Open.**
- Layout of the 0x38-byte squad vector elements (hook table `0x1965140`, getter at `+0x18`).
- Meaning of the bool out of `0x61d410` (row valid when set).

### research (kind 15) - research state, list, per-name rows
Vtable `0x1233728`, v1, period 1 s. Status: **partial** (apply, intent and sender decoded; gather `0x75faf0` not read).
Functions: gather `0x75faf0` (not read), apply = thunk `0x7613e0` -> body `0x764da0` (3988 B), remove `0x7613f0`, intent `0x760f20`, client tick (slot 19) `0x7617a0`, reply handler (slot 20) `0x761ec0`, reset (slot 21) `0x761540`, client sender `0x764790` (call at `0x764a43`, `0x547b00(15, ...)`). Slot 26 = stub (no host upstream).
Helpers (**M**): name validity `0x75f1e0` = length 1..0x60, first and last byte not `' '`, every byte `0x20..0x7e`. Row discriminator: FNV-1a 32 (offset `0x811c9dc5`, prime `0x1000193`) of `"res:" + name` (`0x75f250`, string `0x12333b0`) or `"wm:" + name` (`0x75f280`, string `0x12333b8`).

**Rows (server -> client).** Three key shapes (apply `0x764da0`, **M**):

**(1) Key type 5, id 1 - research state.** Value, end exact:

| field | type | limits | meaning (I) |
|---|---|---|---|
| A | u8 | <= 0x40 | small counter (queue capacity/slots?) |
| B | u8 | <= 0x40 | small counter |
| count | u8 | <= 0x20 (else counter `+0x240`) | entries |
| entries | count x { str16 name (<= 0x60, valid name), u16 v (<= 10000) } | bad name or v > 10000 rejects the row | current research queue with 0..10000 progress |

Stored: vector at `this+0x48`, A at `this+0x98`, B at `this+0x9c`, flag `this+0x88 = 1`; `0x7642a0` stamps a QueryPerformanceCounter time (`this+0xa8`; `this+0xb0` when A/B changed); counter `+0x234`.

**(2) Key type 5, id 2 - research list.** `u16 count` (<= 0x200, larger -> counter `+0x240`), `count x str16 name` (<= 0x60, valid), end exact. Stored as a set at `this+0x68` (**I**: available/known research names); counter `+0x234`.

**(3) Key type 3 (name key; id = FNV-1a hash), two forms told apart by the hash:**
- **"wm:" form.** Value = `u8 'M'` (`0x4d`), `str16 name` (1..0x60, valid), `u16 count` (<= 0x100), `count x str16 item` (<= 0x60, printable, no edge spaces), end exact. Key id must equal FNV("wm:" + name) with the other key u32s zero; a critical section (`0x179cd88`, lazy-init by `0x75f2b0`) guards the store. Accepted: the list goes into the global map `0x1966f98` under `name` (replace), `name` recorded in the `this+0x2c0` map for the key; counter `+0x2ec`. (**I**: a named list of strings, e.g. prerequisites or contents of `name`.)
- **"res:" form** (default; also taken when an 'M'-leading value hashes as "res:", since `0x4d` would be the low byte of a str16 length 77, the hash decides). Value = `str16 name` (1..0x60, valid), `u8 st` (**1..3**), end exact. Key id must equal FNV("res:" + name). Stored in the map `this+0x28` (key = name key; value {name @+0x30, st u32 @+0x58}); map cap **0x800** entries (else `+0x240`); counters `+0x234` ok, `+0x23c` bad hash, `+0x238` other bad; `0x7642a0(this, 1)` stamps the sync time. (**I**: `st` = per-research state, e.g. unknown/learning/known; client log words "add", "remove", "order", "learn".)

Other key types -> counter `+0x238`.
Remove (`0x7613f0`): type 3 -> erase from `this+0x28` (`0x766350`) and the name from the global map; type 5 id 1 -> clears state (`this+0x88 = 0`, vector cleared); type 5 id 2 -> clears the list; then the sync stamp is refreshed.

**Intent (client -> server).** Payload after the header: `u8 op`, then (log words from slot 20, **M**; semantics **I**):

| op | fields after op | meaning |
|---|---|---|
| 1 | `str16 name` (<= 0x60) | "add" to the research queue |
| 2 | `str16 name` | "remove" |
| 3 | `u8 count` (1..0x20; 0 or > 0x20 malformed), `count x str16 name` (<= 0x60) | "order": replace the queue order with the list |
| 4 | `str16 name` | "learn" |

Every name must pass the name check; the payload must end exactly; else notice `bad` (3 bytes, counter `+0x22c`). Authorization (**M**): ops 1, 2, 3 need the player's faction role to have flag `0x10` (`0x697250(role from 0x613fc0(player), 0x10)`); op 4 is not checked. Denied -> notice `your role in the faction may not choose research - ask the leader` (`+0x230`). Accepted -> counter `+0x228` and, if `0x1949194`, the request {player, seq, op, names} is queued for the executor `0x7492b0`. The ok/refuse reply returns as the generic notice with the intent seq.
Sender `0x764790` (**M**): `u8 op`; op 3: `u8 min(count, 0x20)` + `str16` per name; other ops: one `str16` from the first vector element. Client tick `0x7617a0` drains a request ring (`0x1966bf0..`, count `0x1966c08`) into `0x764790`; the order flow re-sends "order" when the local list differs from the server's (`0x75f4c0` compare, `this+0xf0`). In-flight map `this+0xd8` (key = intent seq): ops expire after the constant `0xff3610`; `0x764790` inserts {op, names, send time} and sets a pause `this+0xc0`. If the state row is not loaded yet the UI says `Research is still loading from the server - try again in a moment` (string `0x1233410`, counters `+0x25c/+0x8c`). Reply (slot 20): `ok == 0` -> counter `+0x254`, log `research: the server refused <op word> <name> - <msg>`; `ok` -> `+0x250`.

**Open.**
- Gather `0x75faf0` not read (only the apply order is measured).
- Meaning of A, B, `v`, `st`, the "wm:" list contents; consumers of the global map `0x1966f98`.
- Executor `0x7492b0`; the exact op 4 ("learn") rule.

### slave (kind 16) - slavery state per body, cage/stocks place flags, escape orders
Vtable `0x1235828`, v2 (slot 3 = `0x935c40`), period 1 s. Status: **partial** (verb effects on the Kenshi side and the exact client byte order of cmd 2 not decoded).
Functions: gather `0x770420`, apply `0x770fe0`, remove `0x771190`, intent `0x7727a0` (cmd 2 body `0x772c30`), notice helper `0x7759d0`, senders `0x771710` (cmd 1), `0x775c70` (cmd 2, call site `0x7767e0`); both send kind 0x10 taken from vtable slot 1, not an immediate. Slot 26 stub.

**Rows (server -> client).** Key type 1 (body, id != 0) or type 2 (place, d == 8); any other key or d -> bad. Sizes, order, limits M; meanings I.
Body row (parser `0x76e290`, writer `0x76e130`):
| field | type | limits | meaning (I) |
|---|---|---|---|
| A | u8 | < 4 | slavery state 0..3 |
| B | u8 | any | flag bits; bit 4 (0x10) = set by the server on the viewer's current body when a test holds ("viewer may act") |
| C | u32 | - | timer / value |
| text | str16<=0x40 | every char 0x20..0x7e (check `0x76e240`) | owner / faction name |
End exact. Stored under body id.

Place row (key type 2, d == 8):
| field | type | limits | meaning (I) |
|---|---|---|---|
| a | u8 | < 2 | building flag (struct +0x20) |
| b | u8 | < 2 | building flag (struct +0x21) |
| c | u16 | server clamps an int to 0..65535 | value from the building struct (+0x438 block, read by `0x76ff40`) |
End exact. Stored per place key. Remove: key type 1 erases the body entry, type 2 the place entry.
Gather (M): per viewer the bodies of the server's slave map (`this+0xe0`, std::map by body id), up to 0xff foreign rows (the viewer's own bodies always included, truncation counted at `+0xac`); place rows from Kenshi buildings near the viewer whose function == 8 (`0xb9ba80`), key built by `0x543430` with d = 8.

**Intent (client -> server).** Payload after the header: `u8 cmd`, `u32 body id`, then:
| cmd | fields | effect / reply |
|---|---|---|
| 1 | none, end exact; `0x543830(player, body id)` must be true (the player owns that body) | "clicked away: drop the escape order": 15 s timer (GetTickCount64 + 15000, record via `0x780b50`); log "slave: body N (player P) clicked away - its escape order is dropped"; with the host-world check (`0x15a62e6`) and the body in a cage the click is ignored ("clicked inside the cage - a caged body cannot walk, the escape goes on"). Sender `0x771710` writes `01`, body id as 2 x u16 |
| 2 | `u16 verb`, `u8 mode` (1..4); mode 1: `u32 target body id`; modes 2..4: a key (normally a place key); end exact. `verb` must be in the table at `0x10546c0` = {0x4c, 0x6e, 0x6f, 0xb9, 0xba, 0xc9, 0xcf, 0x11d, 0x11e}; mode 3 only with verbs 0x4c / 0x11d / 0x11e; mode 4 only with 0x4c; the leading body id must be non-zero and equal the player's own body (`player+4`) | order on a target (handler `0x772c30`, 5141 bytes). Sender `0x775c70` writes `u8 2`, own body u32, mode-1 path `u8 1, u16, u16`, mode >= 2 path a place key (`0x543430` / `0x543aa0`) |
Refusal notices (M strings in `0x772c30`): "the torture machine holds you - another player has to set you free (Set free / Break out) or pick you up", "nobody sits in that cage on the server", "the server has no such cage there", "this cage stands in the host's world and the server has no lock to roll - pick its lock (Pick Lock), then Set free", "the server has no such door there", "that door is not locked on the server", "the server has no lockable storage there", "that storage is not locked on the server", "the target is too far from the body on the server", "the game refused the order", "that body is not in a cage on the server", "the target is not on the server", "no such body on the server". Success strings "the lock was picked / cut / broken" (verbs 0x4c / 0x11d / 0x11e, I from the string chosen per verb).

**Open.**
- Meaning of the verb ids (I: Kenshi job ids; 0x6e/0x6f tied to "set free", 0xcf to the cage lock roll) and the Kenshi calls behind them (hook pointers `0x4af580`, `0x1b2c60`, `0x774050`).
- Exact client byte order of cmd 2 beyond what the server reader requires.

### bounty (kind 17) - bounties on bodies
Vtable `0x12363a8`, v1, period 1 s. Status: **partial** (roles of A, B, C not pinned down).
Functions: gather `0x78cce0`, apply `0x78d220`, remove `0x78d330`, value parser `0x78b680`, value writer `0x78b4d0`, describe slot 24 `0x78d670`, fill source `0x78a650` (Kenshi hook, not followed). No intent (slot 12 stub `0x7ffa80`), slot 26 stub.

**Rows (server -> client).** Key type 1 (body) with id != 0, else bad. Order and limits M; meanings I.
| field | type | limits | meaning (I) |
|---|---|---|---|
| count | u8 | <= 16 (0x10) | number of bounty entries; 0 = empty list |
| name | str16<=0x40 | every char 0x20..0x7e; entries strictly ascending by name (memcmp, then length) | faction (string id); console `spine bounty add <who> <faction sid\|name> <amount>` |
| A | u32 | - | decimal number, printed as `name:A` by describe |
| B | u32 | - | id / hash, printed in hex |
| flag | u8 | <= 1 | printed `:claimed` when 1 |
| C | u32 | >= 0 as signed | not printed; amount? |
`name .. C` repeat `count` times; end exact. A name not above its predecessor stops the loop; the partial row is normally rejected by the end check, but if the offender was the last entry the row is taken without it (I, control flow). Client element = 0x48 bytes (name 0x20, A/B at +0x28/+0x2c, flag +0x30, C +0x34, +0x38 = 8-byte local timestamp not on the wire).
Remove: key type 1 erases that body's list.
Gather (M): walks the server bounty map (keyed by body id), max 0x100 rows; a body is included if it is among the viewer's own body ids or within `max(Kenshi-hook float, const 0xeb7a7c)` of the viewer (x/z plane), optionally filtered by a hook at `0x1965180`. Writer order = apply order.

**Open.**
- Which of A / B / C is the cat amount; console command in slot 33 `0x78e820` not read.

### limbs (kind 18 = 0x12) - the four limbs (prosthetics) of each body
Vtable `0x12383e8`, v1, period 2 s. Status: **partial** (row, intent, client path and upstream decoded; modes 0/1/3 and fields a..f unnamed).
Functions: gather `0x7a5460`, apply = thunk `0x7a7ee0` (`xor r9d,r9d; jmp 0x7adf00`; body `0x7adf00`, 4660 B; decoder `0x7a4ae0`), remove `0x7a7f00`, intent `0x7a8370`, host upstream (slot 26) `0x7a5ce0` (545 B), client pump (slot 19) `0x7a88e0` (4359 B), reply handler (slot 20) `0x7a99f0`, client sender call at `0x7a9070` (`0x547b00(0x12, ...)`), value encoder `0x7a4320` (cached per body by `0x7af140` / `0x7af300`).

**Rows (server -> client).** Key type 1 (body), else counter `+0x8c`, nothing applied. Value = **4 limbs, no count byte** (i = 0..3), end exact (decoder `0x7a4ae0` returns 1 only if `ok && pos == len`, epilogue `0x7a4c8f..0x7a4c9d`; failure -> counter `+0x8c`). Limb index (from intent log table `0x1048fc8`): 0 "la" (left arm), 1 "ra", 2 "ll", 3 "rl". Per limb:

| field | type | limits | meaning (I) |
|---|---|---|---|
| mode | u8 | 0..3 (> 3 aborts the whole decode) | 2 = a named replacement limb is attached; 0/1/3 not named (I: natural / missing / other; 1 and 3 are "danger" states gated by a flag) |
| if mode == 2: name | str16 | <= 0x5f; every byte >= 0x20 else decode fails | prosthetic item name |
| if mode == 2: slot | u8 | default `0xff` when not present | slot |
| if mode == 2: a | u16 | - | limb value (tenths, I) |
| if mode == 2: b | s16 | value > 0x7fff has 0x10000 subtracted | limb value (tenths, I) |
| if mode == 2: c | s16 | same | limb value (tenths, I) |
| if mode == 2: d | u16 | - | limb value (tenths, I) |
| if mode == 2: e | u16 | - | limb value (tenths, I) |
| if mode == 2: f | u16 | - | limb value (tenths, I) |

Encoder (`0x7a4320`, **M**): same order. a..f are floats at struct offsets `+0x6c, +0x70, +0x74, +0x78, +0x7c, +0x80`, written as `floor(f*10 + 0.5)` (doubles `0x1076ec8` = 10.0, `0x10b5cf0` = 0.5), clamped to [-32768, 32767]; a, d, e, f clamped >= 0 first, b and c stay signed. `slot` = int at `+0x64` (-1, or feature flag `0x15a6a3b` off -> `0xff`). Non-2 modes are written as `mode` only; a mode outside 0..3 is written as `0xff`.
Apply effect (**M**, partial): resolve the character via `0x1965158(id)`, hash the 4 limb records (FNV-1a over mode bytes, name bytes) to skip identical rows, pack `sum((mode_i & 3) << 2i)` and the 4 names, then call `0xbaf340(char, modes, names, 0)` to set the limbs. Counters `+0x5c` rows, `+0x84/+0xdc/+0xd4` applied variants. Log (max 0x18 lines) `limbs: body N limbs laid from the server's word [ - and they STILL differ (see invfix limbcopy/limbown)] [ (hold)]`. Rows for bodies the client cannot place yet are held and retried.
Remove (`0x7a7f00`): key type 1 -> clears that body's cached state.
Gather (`0x7a5460`, **M**): collects the viewer's bodies (host callbacks `0x1965140/60`), at most **0x100** rows per gather (`> 0xff` -> counter `+0x58`, stop); row key body:`id` -> the cached limb string from `0x7af140` / `0x7af300`, only if its state byte `*pcVar6 == 3` (state 1 -> counter `+0x90`, skipped).

**Intent (client -> server).** Payload after the header (handler `0x7a8370`):

| op | fields (wire order) | effect / reply |
|---|---|---|
| (single form) | `u32 body_id`; `u8 limb` (0..3 else malformed); `u8 act` (1 or 2); `str16 A` (<= 0x5f); `str16 B` (<= 0x5f); optional `u8 q` (only if bytes remain; default `0xff`; forced `0xff` if `0x15a67c4 == 0`; q > 100 mapped to a `0xfe/0xff` value); end exact | act 1 requires non-empty A (the limb item put on); act 2 may have empty A (I: remove/clear). Failure -> notice "the intent does not read" (`+0x194`); body not in sender's squad (`0x543830`) -> "that body is not in your squad". Then engine call `0x4467a0(player, body, limb, act == 1, A, ..., q)` (failure counters `+0x18c/+0x188`). Server log `limbs: body N <la\|ra\|ll\|rl> - the player's hand had "B" there before` (B = item the client says it replaced, I). The result goes back via `0x547e50(player, 0x12, ...)`. |

Sender (client): the pump `0x7a88e0` (slot 19, called every client tick, **I**: by the Spine) sends at `0x7a9070`. Payload bytes (**M**, match the handler): `u32 body_id`, `u8 limb`, `u8 act` (1 if fit flag else 2), `u16 min(len,0x5f)` + A bytes, `u16 min(len,0x5f)` + B bytes, `u8 q`. q rule: q < 0 or q > 100 -> (flag == 0: `0xff`; else q == -1: `0xfe`, other out of range: `0xff`); otherwise q unchanged.
Client path (**M** unless marked):
- **Hook.** Detour `0x7a3a20` (installer `0x7afd40`, signature scan `0xcd9930(0x117, 0x645030, 0x645690)`, trampoline at `0x19496f8`) on the Kenshi function named by the string at `0x1238370` "limbs: could not hook MedicalSystem::fitProsthetic". Args: `rcx` = MedicalSystem*, `edx` = limb index (> 3 passes through), `r8` = item, `r9b` = flag (nonzero passes through, counter `0x194970c`). It resolves the owning character (`0x7a3640`) and limb slot (`0x7a3730`), always calls the original; if the fit succeeded and the character is the player's own (`0x7a3680`), it calls `0x7a37e0`, which pushes an event record (0xe8 B) into a lock-protected ring: spin lock `0x1949720`, head `0x1949724`, count `0x1949728`, 32 slots x 0xe8 at `0x179cf30` (full -> counter `0x194971c`). Event record: `+0` u32 body id, `+8` u64 handle, `+0xc` u32 fit flag (nonzero = a named item is fitted), `+0x10` u32 limb, `+0x18` string A (0x60 B buffer, item put on), `+0x78` string B (0x60 B, item there before), `+0xd8` s32 q (condition 0..100 of the new item, else -1; via item vtable `+0x2c0` then `+0x2b8`), `+0xe0` u64 tick (GetTickCount64). De-dup table of 8 (handle, timestamp) pairs at `0x1949740/0x1949780`.
- **Pump `0x7a88e0`.** First `0x7a4020` (danger-gate check for the own body; logs "limbs: danger gate switched off/safe again - the prosthetics window is open" and "danger gate - the prosthetics window of the own body is locked (<mask>), limb mask N (spine limbs danger)"; state in `0x19496f5`). Drains up to 0x20 events per tick. Per event: `this+0x108 == 0` (no server) -> drop (`+0x160`); `this+0x198 == 0` -> drop (`+0x19c`); older than 3001 ms (`0xbb9`) -> drop (`+0x164`); else it becomes a 0x80-byte pending record in vector `this+0x110` (id u32 `+0`, handle u64 `+8`, limb u32 `+0x10`, act flag u8 `+0x14`, A string `+0x18`, B string `+0x40`, q s32 `+0x68`, send tick u64 `+0x70`, aux u32 `+0x78`). Each pending record is sent; the send result is the intent sequence id (**I**; 0 = not sent). Nonzero -> counter `+0x158`, record moved to the in-flight map `this+0x130` (key = seq id, value 0x80 B, last-sent tick `+0x150`), log (max 0x18) `limbs: body <id> <la|ra|ll|rl> - the player's hand, intent #<seq> fit "<A>"[ (was "<B>")] / unfit`. Zero -> keep for retry while younger than 3001 ms, else drop (`+0x15c`) and clear its dedup entry. In-flight records older than 5001 ms (`0x1389`) are dropped (`+0x174`) and `0x7a8170` clears their lock state (via `0x7a3520`).
- **Reply `0x7a99f0` (slot 20)** `(this, u32 seq, u8 ok, string msg)`: finds the in-flight record by `seq` (map `this+0x138`). `ok == 0` -> counter `+0x170`, log (max 0x18) `limbs: the server refused our hand on body <id> <limb name> - <msg> (the server's word will put it back)`; nothing else changes locally (the next row overwrites). `ok != 0` -> counter `+0x16c`, the body's lock record is touched (maps `this+0xd0` / `this+0x1c8`, tick stored), the in-flight record erased (`0x7b0820`), `this+0x150` = tick. The matching notice message is written by `0x547e50`: `u8 1, u16 0, u16 0` header, then `u8 7, u16 topic (0x12), u32 seq (u16 lo, u16 hi), u8 ok, u16 len (<= 200), text`, sent via the host callback `0x19424a8(handle, peer, 1, blob)`. The same writer is the server's reply path of the slot-12 handler.

**Upstream (host -> server).** Slot 26 `0x7a5ce0`, signature `(this, vector<u32> &body_ids (in/out), vector<Row> &out)`, `this+0x278` counts calls. If `0x1965158` (body id -> character) is null the id list is cleared. Per body id: resolve the character; read its 4 limbs with `0x7a2f60` (alias `0x7a3160`): game getter `0xcd9930(0x82, ...)` gives the MedicalSystem, then per limb i fills a **0x84-byte record** at `aiStack_258 + i*0x84`: `+0 s32 mode` (`0xcd9930(0x11c, 0x6434d0, 0x643b30)(medicalSystem, i)`), `+4 name[0x60]` (only if mode 2; item at vtable `+0x18`, <= 0x5f), `+0x64 s32 q` (-1 or item condition 0..100 via item vtable `+0x2c0/+0x2b8`), `+0x68 u8 slot flag`, `+0x6c..+0x80` six floats = a..f in order, from the limb object's `+0x54, +0x40, +0x44, +0x48, +0x4c, +0x50` (objects at medicalSystem `+0x90` la, `+0x98` ra, `+0x80` ll, `+0x88` rl). Failure to resolve or read -> id dropped, counter `+0x280`.
- If the flag `0x19496d7` ("spine limbs danger", **I**) is on: every limb with mode 1 or 3 is rewritten to mode 0 before encoding (counter `+0x284`).
- A row is emitted only if **at least one** of the 4 limbs has mode != 0 (all-natural bodies are not reported). Row = key body:`id` (type 1) + value from `0x7a4320` (= the server row value); counter `+0x27c`.
- The id list is shortened (`0x5dbe50`) to the resolved ids. `aux` is not set here.
Chunk framing: see "Upstream chunk framing".

**Open.**
- Names of modes 0/1/3; meaning of floats a..f (limb object fields).
- Dedup/lock maps `this+0xd0`, `this+0x1c8`; who calls slot 19 (I: Spine tick); exact return value of `0x547b00` (decompile drops it).

### splint (kind 19 = 0x13) - per-part splint amounts of each body
Vtable `0x12389b8`, v1, period 2 s. Status: **partial** (row, upstream, remove decoded; judge slot 28 only summarised).
Functions: gather `0x7b35a0`, apply = thunk `0x7b3c20` -> body `0x7b7d40` (hold flag 0; a second call with hold flag 1 sits at `0x7b3ca9`, **I**: retry of held rows), remove `0x7b3cb0`, host upstream (slot 26) `0x7b3d60`, judge (slot 28) `0x7b3fa0` (4451 B), accept (slot 29) `0x7b5110`. Slot 12 = stub `0x7ffa80` (no client intent); slots 19/20 stubs. Row encoder `0x7b7b00`, cached value builder `0x7b80d0` (-> `0x7b2d10`).

**Rows (server -> client).** Key type 1 (body), else counter `+0x8c`, return. Value, end exact (else `+0x8c`):

| field | type | limits | meaning (I) |
|---|---|---|---|
| count | u8 | <= 0x10 (else decode fails) | entries |
| per entry: name | str16 (u16 len + bytes) | <= 0x40 (longer rejects the row); NUL-terminated into a 0x48-byte slot | the part's game-data name |
| per entry: value | u16 (via `0x9f8d0`), stored as u32 | - | splint amount, **tenths** (log prints `v/10 "." v%10`) |

Encoder `0x7b7b00` (**M**): `u8 count`, per entry `u16 len (<= 0x40), name, u16 value` (value as 2 x u8, lo then hi); entries **sorted by name** (insertion sort `0x7b79c0`, strcmp order) before writing. The layout is shared with wear.
Apply effect (`0x7b3030` via thunk `0x7b3310`, **M**): resolve the character via `0x1965158(id)`; get its medical system (host getter at Kenshi RVA `0xcd360`, found by signature scan `0xcd9930`); enumerate its parts (count: host fn `0x2a9f90`, get(i): `0x2a9fa0`; parts for which host fn `0x643410` returns true are skipped). Per part (name = `part->data->name` std::string at data+0x58, first 0x40 chars): new value = the matching entry's value, **0 if no entry matches**; current = `floor(part+0x4c * 10.0 + 0.5)`, 0 if <= 0, capped at 65535.0 (`0x12c1bc0`). If different: `part+0x4c = value / 10.0f` (`0xeb681c` = 10.0) and the host refresh `0x6435c0` is called on the part. Result < 0 (no medical system): counter `+0x90`; 0: `+0x7c` (nothing changed); > 0: `+0x78` (`+0x80` if held variant), the body id is inserted into the set at `this+0x98`, and (max 0x10 lines) log `splint: body <id> - <n> part(s) took the server's patch[ (hold)]` followed by ` <name>=<v/10>.<v%10>` per entry. (**I**: `part+0x4c` = splint amount, from the judge strings.)
Remove (`0x7b3cb0`, **M**): key type 1 and body in set `this+0x98` -> if `0x7b6420` says the body is protected (`0x19497e8 != 0` and `0x9a3fe0(character)`; **I**: the player's own body) counter `+0x1c` and nothing; else apply an empty patch list (all parts -> 0; counter `+0x84` if parts changed).
Gather (`0x7b35a0`, **M**): viewer's bodies via `0x1965140/60`, `0x15a6690` = cache mode; key body:`id`, value = cached string from `0x7b80d0` (reads parts via `0x7b2d10`, encodes via `0x7b7b00`), non-empty values only, at most **0x100** rows per gather (counter `+0x70`), skips rows the output vector already holds.

**Upstream (host -> server).** Slot 26 `0x7b3d60`, signature `(this, vector<u32> &body_ids (in, filtered in place), vector<Row> &out)`, counters `+8` calls, `+0xc` rows, `+0x10` unresolved. Per body id: resolve the character; `0x7b2d30` -> `0x7b2b80` reads up to 0x10 parts whose value (tenths of `part+0x4c`) != 0 into 0x48-byte entries (name <= 0x40, value at `+0x44`); parts for which `0xcd77a0(part)` is true are skipped. If `0x19496f7` is on: additionally `0x7b2f30` -> `0x7b2d70` reads up to 0x20 parts into **0x54-byte entries** (name; `+0x44` tenths of `part+0x4c`; `+0x48` = `part+0x40` as int bits; `+0x4c` = `part+0x44`; `+0x50` u8 = `part+0x32`), and `0x7b6af0` merges the first entry whose `+0x50` byte is 0 into the main list with value `tenths + 300` (replace by name, else append if < 0x10; counter `+0x14`). A row is emitted when the final entry count != 0: key body:`id` (type 1), value = `0x7b7b00` output (identical to the server row value). Chunk framing: see "Upstream chunk framing".

**Judge (slot 28 `0x7b3fa0`, server side of a host description; summarised, partly M from strings).** Args `(this, key, old-state wrapper, new-state wrapper, out string)`; wrappers are `{u8 form @0, string}`: form 2 = patch list in the row value format (decoded by `0x7b6470`), form 1 = no patches. Key type must be 1. Needs `0x15a62cf` on, else counter `+0x24`. Compares the host's patches with the server body's parts (`0x7b2f30`) and rejects with text (strings **M**):
- `"<id> is not on the server or does not read - nothing to judge by"` / `"the description does not read"`
- `": no such (non-prosthetic) part on the server's body"`
- `": a splint does not go on a vital part (applySplint)"` (part `+0x32` flag)
- `": above 6.0 a splint needs a splint kit, and none is within 100 of the patient on the server"` (value > 60 tenths, kit search via `0x7b6640`)
- `": a splint comes off only as the part heals (the tick clips it to 50 - (flesh - stun), here X < Y)"` (decrease > 0.5 below ...; threshold constant 40.0 at `0xeba92c`)
- `": patch P + (flesh - stun F) = Z > 50: the game clips a splint below that"`
Constant 10.0 at `0xeb681c`.
Slot 29 `0x7b5110` (flag `0x15a62d0`): logs `splint: body N - the host's word [laid on | did NOT lay on] the server's body[ too]` and lays a form-2/1 description on the server's character via `0x7b3310` (**I**: accept step after the judge).

**Open.**
- Full branch order of the judge; the `+300` rule (= +30.0) in the upstream merge; what `0x7b6640` returns.
- Who calls slots 28/29 (**I**: the Spine upstream consumer).

### mods (kind 20) - server mod list, content hashes, game-data check
Vtable `0x124b878`, v1 (slot 3 = `0x78ccd0`), period 5 s. Status: **partial** (text feeding the list serial, game-data bucket logic, kick target not decoded).
Functions: gather `0x8c7ff0`, apply `0x8cae70`, remove `0x8cb320`, intent `0x8c83e0` (ops 1-3; op 4 in `0x8ca8a0`), notice `0x8cb6a0`, senders `0x8dad00` (report, call `0x8db2c1`), `0x8cb4d0` (answer, client tick slot 19, call `0x8cb66b`), all reliable `0x547b00`, kind 0x14. Server tick slot 11 `0x8c7ed0`, kick tick `0x8d1280`, list builder `0x8d4cf0`. Slot 26 stub.
Purpose (I): the server publishes its mod list (names, optional SHA-256 per mod, an allow list, a game-data policy); each client compares its own `data\mods.cfg` and reports the difference; the server answers with a notice and, in mode `refuse`, kicks the player after a grace time; an optional second stage compares a game-data fingerprint by question/answer rounds. Server mode `this+0xc` (0 off, 1 warn, 2 refuse), data mode `this+0x618` (same values, config `modcheck_data`); config keys `modcheck=`, `modcheck_data=`, `allow=`, `skip=` (strings `0x8ce660`).

**Rows (server -> client).** All rows use key type 5 (global) with key bytes 8..0x17 zero, else apply returns bad. Every reader returns `ok && pos == len` (end exact). "name" = str16<=0x80, 1..0x80 bytes, every byte 0x20..0x7e (checks `0x8d65f0`, `0x8d6860`, `0x8d7ab0`). "raw 32" = exactly 32 bytes (reader `0x543e40`). `serial` = u32, the list revision `this+0x58`, an FNV-1a32 over a text (loop in `0x8d4cf0`, basis `0x811c9dc5`, prime `0x1000193`; exact text I); the client keeps it at `this+0x318` and ignores rows with another serial.
Gather runs only when `this+0xc != 0 && this+0x89 != 0 && this+0x4b9 != 0`. Row order: global 0, globals 1..N (N <= 9, pre-built blocks at `this+0x38`, 0x28-byte elements), global 0x40, then per viewer (map `this+0x598`) global 0x41 and 0x42 when their blocks are non-empty. Values are pre-serialised in `0x8d4cf0`.

Global 0 = content table (reader `0x8d7790`, writer `0x8d4cf0`; client stores at `+0x5d0..`):
| field | type | limits | meaning (I) |
|---|---|---|---|
| serial | u32 | - | |
| n | u16 | <= 0x190 (400) | mods in the server list, load order |
| per mod: t | u8 | 0 or 1, else bad | 1 = a hash follows |
| per mod: hash | raw 32 (only if t == 1) | - | SHA-256 of the mod's content |
Usable only if n equals the assembled list length and serial matches (`0x8d7720`).

Global 1 = list header (reader `0x8d65f0`, writer `0x8d4cf0`; assembled by `0x8d6a80`):
| field | type | limits | meaning (I) |
|---|---|---|---|
| mode | u8 | < 3 | server mode |
| serial | u32 | - | |
| mod count | u16 | <= 400 | |
| names checksum | u32 | - | FNV-1a32 over all names joined with "\n" (M, `0x8c2690`); mismatch -> message `inconsistent (count or hash)` |
| parts | u8 | < 9; `mod count == 0` iff `parts == 0` | number of list parts |
| m | u16 | <= 0x40 | allowed-name count (`allow=` list) |
| m x name | name | - | |

Globals 2..9 = list part (id - 2 = part index; reader `0x8d6860`; client map `+0x2f0`):
| field | type | limits | meaning (I) |
|---|---|---|---|
| serial | u32 | - | |
| part index | u8 | == id - 2 | |
| start | u16 | - | index of the first name |
| cnt | u16 | >= 1; `start + cnt` < 0x191 | |
| cnt x name | name | - | |
Server fills a part up to 0x3800 bytes (`2 + name length` per name), at most 8 parts; if the list does not fit it logs `mods: the server's list does not fit 8 record parts - clients will not check it`. Client reports `not contiguous` when starts do not line up.

Global 0x40 = allow / data policy (reader `0x8d7ab0`, writer `0x8d4cf0`; client `+0x6b0`, then `0x8d7e10`):
| field | type | limits | meaning (I) |
|---|---|---|---|
| serial | u32 | - | |
| version | u8 | must be 1 | |
| data mode | u8 | < 3 | server `modcheck_data` |
| const | u8 | not checked (server writes 1) | |
| data print present | u8 | < 2 | from `singleton+0x280` (tick `0x8c7ed0`) |
| k | u16 | <= 0x40 | pinned allowed mods |
| per mod: name | name | - | mod the server lets in on top of its own |
| per mod: h | u8 | 1..8 | number of pinned hashes |
| per mod: h x hash | raw 32 | - | pinned content hashes |

Global 0x41 = game-data question (viewer-specific; reader `0x8d82e0`, writer `0x8d2b20`; applied by `0x8d84d0` only if client `+0x310` is set):
| field | type | limits | meaning (I) |
|---|---|---|---|
| serial | u32 | - | |
| question id | u16 | server counter `+0x7c`, wraps 65535 -> 1 | |
| type | u8 | < 0x72 | record type index |
| flags | u8 | only bits 0 and 1 (server: `rec+0x100 != 0 \| (rec+0x101 != 0 ? 2 : 0)`) | meaning I |
| c | u8 | <= 0x20 | |
| c x str16<=0x400 | str16 | - | range edges or search words |
The end check is in the combined return value of `0x8d82e0` (`pos == len`, disassembly `0x8d8488..0x8d849b`). Client answers with op 4, at most 15 answers per serial, then logs `mods: the server keeps asking about game data (...) - not answering any more`.

Global 0x42 = game-data verdict (one-shot, viewer-specific; reader `0x8d8840`, writer `0x8d4260`, reached via `0x8d1860` / `0x8d2e60`; not stored, shown in chat and log):
| field | type | limits | meaning (I) |
|---|---|---|---|
| serial | u32 | must equal client `+0x318` | |
| verdict | u8 | 1..3 | 1 matches (`Your game data matches the server.`), 2 differs (`GAME DATA DIFFERS`), 3 not checked (`Game data not checked: ...`) |
| data mode | u8 | 0..2 | 2 gives the chat message a higher severity (`0x8dab40(..., (b==2)+1, ...)`) |
| short text | str16<=0x80 | - | |
| long text | str16<=0x134 | client cuts to 300 | logged `mods: the server's game-data verdict - <text>` |
Only verdict 2 shows a chat message (`GAME DATA DIFFERS: <short text> - see chat`), again only when the FNV-1a32 of the long text or the mode changes (`+0x6f8`).
Remove (`0x8cb320`, key type 5): id 0 clears the content table, 1 the list (`0x8d7410`), 0x40 the policy, 0x41 the pending-question flag (`+0x6e8`), 2..9 that part (map `+0x2f0`); 0x42 and others ignored.
Client effect: rows 0 / 1 / 2..9 / 0x40 run `0x8d6a80` / `0x8d7e10`, which assemble the list and compare it with the local `data\mods.cfg` (results in vectors `+0x328`, `+0x368` extras, `+0x3a8` missing, `+0x3e8` other version, `+0x420` allowed-other-version); tick `0x8cb4d0` sends the report when `this+0x465` is set and the rate timer `this+0x468` is due, up to 4 attempts (`this+0x480 < 4`), waiting for the reply (`this+0x470` = seq, then retry).

**Intent (client -> server).** Payload after the header: `u8 op`. Reply = notice `0x547e50(player, 0x14, code = client seq, flag, text)`.
Ops 1-3 = mod report (reader in `0x8c83e0`, sender `0x8dad00`). Wire order:
| # | field | type | limits | meaning (I) |
|---|---|---|---|---|
| 1 | op | u8 | 1..3 | client sends 3 when it holds the 0x40 policy of the current serial (`+0x6b0` set and `+0x6b4 == +0x318`), else 2 if the content table is usable (`0x8d7720`), else 1 (older server, names only) |
| 2 | flags | u8 | 0 or 1; bit 0 = client could not read its `data\mods.cfg`; if 1 every count below must be 0 | |
| 3 | serial | u32 | must equal `this+0x58` and `this+0x30 == 0`, else notice flag 1 "not checked yet: the server's mod list changed - the new one is on its way" | |
| 4 | extra | u16 | <= 400 | client mods not in the server list |
| 5 | order | u8 | <= 1 | 1 = same mods, other load order |
| 6 | ni, then ni x u16 missing idx | u16, u16[] | ni <= 400; strictly increasing, < list size, not in the server skip set (`this+0x10`, lookup `0x106230` must be 0), else malformed | indices of server mods the client lacks |
| 7 | (op >= 2) nd, then nd x u16 idx | u16, u16[] | nd <= 400; strictly increasing, < list size, not in skip set, not in the missing set (`0x105c00`), server must hold a hash (`this+0x510[idx] == 1`) | mods present with different content |
| 8 | (op >= 2) unverified | u16 | <= 400 | own mod files the client could not hash |
| 9 | (op == 3) alt | u16 | <= 400 | allowed mods present only in a version not pinned |
| 10 | (op == 3) pinsUnverified | u16 | <= 400 | pinned allowed mods the client could not verify |
| 11 | (op == 3) game-data print (reader `0x8d15d0`, writer `0x8db360`): `u8 m` (<= 3) | u8 | 0 = no print, 2 = other fingerprint rules, 3 = not asked / no policy; for m != 1 the payload ends here | |
| 11b | (m == 1) `u8 const` (must be 1), `u64 root` (4 x u16 LE, `0x8c7400`), `u16 n` (<= 0x72), n x {`u8 type` (<= 0x71, strictly increasing), `u32 count` (1..10,000,000), `u64 hash`} | | end exact, then `0x8e8140(types, entries, 0) == root` or bad | self-consistency of the print; client builds it from per-type differences of cumulative counts (`+0x68`) and hashes (`+0x48`) |
End must be exact, else the server logs `<player> sent a malformed mod report` and replies a notice `malformed mod report` (flag not decoded; `0x8d6000`). An op outside 1..4 takes the same malformed path.
Server logic after a good report (M strings/branches): marks the player reported (`this+0x590[player] = 1`), re-reads the config (`0x8ce660`). Mode off -> flag 1 `the mod check is off on this server`; `this+0x89 == 0` -> flag 1 `not checked: the server could not read its own mod list`; flags bit 0 -> log `could not read its data\mods.cfg - not checked`; all of ni, nd, extra, alt zero -> flag 1 `mods match the server (N): ...` and `0x8d1560` clears the pending kick time; else flag 0 `mods differ from the server: <extra> extra, <missing> missing [names], <other> other version [..], load order differs, ...` (summary `0x8c5ef0` / `0x8c5ad0`; keys `missing [`, `other-version [`, `order-differs`, `content-unverified=`, `allowed-but-not-pinned-version=`, `pins-unverified=`). For op 3 the print is judged by `0x8d1860`, its text appended, and it may start a question/answer exchange (rows 0x41 / op 4).
Verdict effect: on failure with `this+0xc == 2` the player record (map `this+0x590`: +0x10 double deadline, +0x18 reason kind, +0x20 summary, +0x48 name) gets, only if no deadline is pending, `deadline = now + 10.0 s` for op 1 and `now + 3.0 s` for ops 2/3 (doubles at `0x1076ec8` / `0xeb5100`), text ` - this server lets in only its own mods: you will be disconnected` (warn mode: ` - you may play, things may look or work differently`). Tick `0x8d1280`: for each record with `0 < deadline <= now` it clears the deadline and, if the effective mode (`+0x18 ? this+0x618 : this+0xc`) is still 2, calls the host function pointer `[0x1942580](ctx 0x1942128, player id, 0xc, reason = record +0x20)` (kick) and logs `mods: <name> disconnected (modcheck=refuse): <summary>`. A client that never reports is only logged (`0x8d4620`), not kicked.

Op 4 = game-data answer (handler `0x8ca8a0`; built in `0x8d84d0`, stored at client `+0x710`, sent by `0x8cb4d0` once `+0x6e8` is set):
| field | type | limits | meaning (I) |
|---|---|---|---|
| op | u8 | 4 | |
| serial | u32 | == `this+0x58` == record `+0x94`, `this+0x30 == 0` | |
| question id | u16 | == record `+0x9c` | |
| n | u8 | <= 0x22 | |
| n x key | u32 | - | |
| n x value | u64 (4 x u16) | - | |
End exact. Accepted only if the player has a pending question (record `+0x90 == 1`), else dropped silently. A good answer is compared with the server's data print (`0x8e8ae0`): no difference and round limit not reached -> next question (`0x8d2b20`), else `0x8d2e60` builds the 0x42 verdict (strings `looking for the first record that differs`, `record types that differ`, `[stringID`). An unanswered question is re-sent up to 3 times (`0x8d4450`), then verdict 3 `your game did not answer`. No notice for a good or dropped answer.
Notice (slot 20 `0x8cb6a0`, M): code 0 counted unknown (`+0x1d0`); `code == this+0x6f4` (op-4 answer in flight) logs `mods: the server refused a game-data answer - <text>`; `code == this+0x470` (report in flight) clears it, counts ok/refused by flag, stores the text at `this+0x488`, logs `mods: the server's verdict - [DIFFER: ]<text>` (`DIFFER: ` when flag 0); other codes counted unknown.

**Open.**
- Text that feeds the serial `this+0x58`; how header checksum `+0x118` is built.
- Meaning of the constants `version 1` / `const 1` in row 0x40 and of the 0x41 `flags` bits; bucket logic (`0x8e8340` client, `0x8e8ae0` server, evaluation `0x8d1860`, 4657 bytes).
- Whether op 3 is sent without a usable content table (client writes the op >= 2 fields only `if cVar3`, server reads them for every op >= 2; I: policy only comes with the content table).
- Notice flag of `malformed mod report`; target of kick pointer `[0x1942580]`; slots 21, 23, 24, 32, 33 (`0x8cb4c0`, `0x8cb8a0`, `0x8cb8e0`, `0x8cbc90`, `0x8cbd60`; console `spine mods ...`, I).

### stores (kind 21) - container layouts, zone flags, container uploads
Vtable `0x1242268`, v1 (slot 3 = `0x78ccd0`), period 2 s. Status: **partial** (item-text grammar, Kenshi-side container finders and most of the gather's candidate selection not decoded).
Functions: gather `0x83d3a0` (12998 bytes), apply `0x8422e0`, remove `0x842300`, intent `0x840cc0` (5658 bytes; census op in `0x8474f0`), notice `0x8424d0`, layout parser `0x83ccc0`, layout writer `0x83c8c0`, senders `0x846460` (op 1, call `0x846585`), `0x84c0c0` (op 2, call `0x84c199`), `0x848fe0` (op 3, call `0x8490a7`, packets pre-built by `0x849430`), kind 0x15. Slot 26 stub `0xd5dd0`. (Ghidra lists apply with size 8; the decompile under that header is the real body.)

**Container layout block** (used in rows and in both intent ops 1 and 3):
| field | type | limits | meaning (I) |
|---|---|---|---|
| X | u16 | gather passes `max(record+0xc, 0)` or the literal 3; census stores `u16 - 1` | variant / class index passed to the store finders, mixed into the layout hash |
| n | u16 | <= 0x200 (512) | |
| n x str16<=0xa0 | str16 | - | string table (client global vector `0x1969640`, 0x28-byte std::string; filled by writer callback `0x83c750`) |
| item text | str16<=15000 | gather cap 15000, census packets cap 0x1e46 (7750), writer fails the row beyond the cap | serialised pieces, parsed by `0xbb9070` with callback `0x83c850` into 0x100-byte pieces (writer `0xbb8710`) |

**Rows (server -> client).** Key type 2 or 3; type 1 is rejected on the client; other types bad.
- Key type 2 (place): d = `0x53484c46 "SHLF"` or `0x54524446 "TRDF"` -> client finder `0x449d80` (fixtures, I); any other d (server writes `0x53544f52 "STOR"`) -> finder `0x449de0`. `0x4655524e "FURN"` appears only in the census code. Value = container layout block, end exact, else bad.
  Client effect (M control flow): per place a record (layout hash xor `X * 0x9e3779b1`, first-seen tick, attempt counters, cool-down 30000 ms) in map `this+0x180`; skipped while the place is excluded (`0xbadf10`, `0xbeec70`, `0x8466d0`, `0x84a810`); finds the live Kenshi container at (x, y, z); if its inventory hash (`0xbabe30`) equals the received layout nothing changes; else relays the pieces (`0xbb7580` apply, `0x84b440` diff/relay) and after 3 failed tries (`+0xa4`) settles on what the machine holds. Logs "stores: <key> took the server's word" (+ "PARTIAL", "<n> piece(s), relaid <m>", "(hold)") or "stores: <key> would not become the server's word after N tries - settled on what this machine holds" (first 0x28 logs).
- Key type 3 (name) = zone row; `key.id` must equal FNV-1a32 over the 4 zone bytes started with (byte0 xor `0x53545a4e "STZN"`), else bad:
| field | type | limits | meaning (I) |
|---|---|---|---|
| tag | u8 | != 0; when 1 the value must end right after the next two fields | |
| zone | u32 | `((cx+0x20) << 16) \| ((cz+0x20) & 0xffff)`, cell = `floor(world / [0xebe4bc])` | zone id |
| flags | u8 | - | zone state |
Server writes `u8 1`, zone as 2 x u16, `flags = 0x840670(zone)`: bit 0 (1) if `0x14ce60(x,z) == 1`, bit 2 (4) if it returns 2, bit 1 (2) if `0x1ff9f0(x,z,0) == 2` (I: "described by the server's book / left to players / undescribed"). Stored in a mutex-protected map (`this+0x208` zone -> flags + tick; reverse map `this+0x228`).
Remove (`0x842300`): key type 3 erases the zone entry (maps `this+0x208/+0x228`, critical section `this+0x1c0`); other keys erase the place entry from `this+0x180`.
Gather (M partial): up to 6 viewer reference points (own bodies plus other bodies within squared distance `[0xeb7a54]`), up to 0xa0 candidate containers per gather from `this+0x120/+0x140/+0x160`, tag SHLF / STOR / TRDF by candidate kind, layout from the live container (`0xb87dc0`) through `0x83c8c0(.., X, .., 15000)`, a row failing the cap is skipped; zone rows for the viewer's 3 x 3 zone cells.

**Intent (client -> server).** Payload after the header: `u8 op`. Reply = notice `0x547e50(player, 0x15, code = client seq, flag 1 ok / 0 refused, text)`; the client slot 20 (`0x8424d0`) matches the code against its pending lists (0x28-byte entries at `this+0x1a0`, first u32 = code, tick at `+0x20`; census queue `this+0x248/+0x268`) and logs "stores: layout #N REFUSED - text" / "stores: census packet #N of zone Z REFUSED - text".
| op | fields (wire order) | effect / reply |
|---|---|---|
| 1 | key (type 1 body = lying body / personal chest, or type 2 place = storage; other -> notice "not a storage or a lying body"), then container layout block, end exact (else notice "a broken layout") | set the layout of a container. Server finds the container (place `0x44a000` / `0x44ab30` with the player's own body ids as candidates; body `0x44caf0`), compares with its copy via `0xbd2670` (success only when all mismatch counters are zero); notice flag 1 or flag 0 with "the server's copy differs", "the book's record differs", "no account for this personal chest", "not a storage or a lying body", "a broken layout"; log "stores: player P layout of <key> N piece(s) REFUSED: ..." (first 200). I: the client uploads its rearrangement, accepted only if legal |
| 2 | none (payload exactly `02`) | request a retell: server calls `0x548160(0x15, player)` (re-sends the topic), notice flag 1 (I). Sender `0x84c0c0` writes `02`, rate-limited by globals `0x198d704` / `0x198b43d` |
| 3 | `u32 zone` (2 x u16), `u8 flag` (zone completeness, I), `u16 count` (<= 0x18, else bad), count x entry {`f32 x`, `f32 y`, `f32 z` (u32 each, range ±3.0e38, else ok cleared), `u16 v` (stored `v - 1`; `0x449410` validates the kind; stored 2 and 13 = storage "STOR", 3 rejected, others furniture "FURN"), `str16<=0xa0 name A`, `str16<=0xa0 name B`, `u16` (entry +0x60), `u8` (entry +0x64), container layout block}; end exact else notice "a broken census" | census of a zone's containers (handler `0x8474f0`, 6795 bytes, entries 0x280 bytes on the server). Checks (notice texts): "a broken census", "too many census packets a minute" (> 0x3f per player per rolling minute), "one census of a zone in 10 minutes" (stamp `this+0x2e0`: next packet within 60000 ms of the zone's last, a new census of the same zone after 599999 ms), "too many censuses a minute" (>= 4 new zones per minute), "the journal refused the census"; per container: inside the announced zone, <= 1000 pieces, caps `[0x15a623c]` (containers) and `[0x15a6240]` (pieces), Kenshi-side check `0x8b87a0`. Logs "stores: census of player P zone Z: N container(s), M item(s) recorded, K skipped (had records / over the caps / invalid / not written / left to players)" then "the zone is described now" / "the zone stays undescribed" |
Senders: op 1 `0x846460(this, kind, id, x, y, z, layout string)` writes `01`, then a key (kind 3 -> place key by `0x543430` with d = `0x53544f52 "STOR"`; else a type-1 body key from the id argument), then the pre-serialised layout; refused locally if the payload would exceed 0x1fd7 bytes; adds a pending record (code = returned seq, kind, X, floats, GetTickCount64) to `this+0x1a0`. Op 3 `0x848fe0` sends packets that `0x849430` pre-builds into `this+0x248`, one per >= 300 ms: `03`, zone (4 bytes), flag, u16 count, per entry 3 x u32 floats, `u16 (+0x1c) + 1`, 2 x str16, u16, u8, layout string (cap 0x1e46); scans zones near the player's bodies every 5000 ms, one zone per 20000 ms. I: the census tells the server which containers exist in zones it has no record of.

**Open.**
- Item-text grammar (`0xbb8710` / `0xbb9070`); Kenshi-side finders `0x449d80` / `0x449de0` / `0x44a000` / `0x44caf0`; exact checks of `0xbd2670`; bit meaning of the zone flags beyond the call targets `0x14ce60` / `0x1ff9f0`.

### requests (kind 22) - player-to-player requests and bouts
Vtable `0x1243b78`, v2 (slot 3 = `0x935c40`), period 1 s, kind 0x16 (slot 1 `0x8546f0`). Status: **partial** (inner create/answer logic, slots 21 / 32 not decoded).
Functions: gather `0x854720` (3331 bytes), apply `0x855f50` (dispatcher: key type must be 5; id 1 -> `0x8632f0`, 2 -> `0x8639c0`, 3 -> `0x8645c0`; other id bad), remove `0x855f90` (callee `0x864e90`), intent `0x855a20`, notice `0x856900`, senders `0x859950` (op 1), `0x859e00` (op 4), `0x85a6f0` (op 2), `0x85ac70` (op 3), all reliable `0x547b00`. Slot 26 stub; slot 11 `0x855430`, slot 19 `0x8565e0` (client tick: pending-map timeouts).
Request kinds (u8, same table in `0x8632f0`, `0x859950`, `0x85d430`): 1 pick up, 2 sparring, 3 duel to first hit, 4 duel to knockout, 5 trade. Server state (I): request records 0xa8 bytes at `this+8..0x10`, bout records 0x128 bytes at `this+0x28..0x30`; each viewer sees at most 6 entries per list.

**Rows (server -> client).** Key type 5 (global); ids 1, 2, 3. Writer order in `0x854720` = reader order (M).
Global 1 = requests waiting for the viewer (writer filter: record `+0xc` == viewer and status `+0x70` == 0):
| field | type | limits | meaning (I) |
|---|---|---|---|
| n | u8 | <= 6 | |
| per entry: request id | u32 | - | |
| per entry: kind | u8 | - | 1..5 |
| per entry: asker body id | u32 | - | record `+0x10` |
| per entry: asker name | str16<=0x20 | - | record `+0x18` |
| per entry: seconds left | u16 | capped 65000, 0 when expired | deadline double `+0x68` minus now |
End exact. Client logs new ids `<name> asks: <kind text> - top right: Accept / Decline` (map `+0x1f8` of announced ids), list at `this+0xe0`.
Global 2 = requests the viewer sent (record `+8` == viewer):
| field | type | limits | meaning (I) |
|---|---|---|---|
| n | u8 | <= 6 | |
| per entry: request id | u32 | - | |
| per entry: kind | u8 | - | |
| per entry: target body id | u32 | - | record `+0x14` |
| per entry: target name | str16<=0x20 | - | record `+0x40` |
| per entry: status | u8 | - | 0 pending, 1 accepted, 2 declined, 3 no answer, 4 accepted but failed (from log texts in `0x8639c0`) |
| per entry: seconds left | u16 | only while status 0, else 0 | |
| per entry: status text | str16<=0x30 | - | record `+0x80` |
End exact. Client list at `this+0x100`, dedup/time map `+0x238`.
Global 3 = the viewer's current bout (writer: the record where the viewer is side `+8` or `+0xc`, preferring a running one (state 1), else the highest id):
| field | type | limits | meaning (I) |
|---|---|---|---|
| bout id | u32 | - | |
| state | u8 | apply accepts 1 (running) or 2 (over) | record `+0x84` |
| kind | u8 | apply accepts 2, 3, 4 | record `+4`; the three bout kinds (sparring, first hit, knockout) |
| opponent body id | u32 | - | record `+0x10` or `+0x14`, whichever is the other side |
| opponent name | str16<=0x20 | - | |
| seconds left | u16 | state 1 only | to deadline `+0x70` |
| result | u8 | <= 3 | state 2: 0 -> 3 when no winner recorded, else 1 viewer won / 2 lost |
| message | str16<=0x60 | - | record `+0x90` |
End exact. Client stores at `+0x120..+0x160`; a new id/state logs `Duel/Sparring with <name> - fight!` or `(no damage)` texts.
Remove (`0x855f90`): global ids 1/2/3 clear the matching client list / bout.

**Intent (client -> server).** Payload after the header: `u8 op`. Reply = notice `0x547e50(player, 0x16, code = client seq, flag, text)`; client slot 20 `0x856900` counts ok/refused (`+0x320` / `+0x324`), matches the code to the pending map (`+0x260`), keeps the text (<= 0x78 bytes, `+0x1c8`), logs `request <id> ...REFUSED - <text>`. Unknown op -> "unknown request op".
| op | fields | effect / reply (I unless M) |
|---|---|---|
| 1 | `u8 kind` (1..5, else "no such request kind"), `u32 target body id`, end exact ("a broken request") | create a request (`0x85c340`). Checks (strings): you have a body, not your own, "that body is not another player's character", both in the world, in range ("too far - come within N m"), neither in a bout, "get up first" if the asker is down; kind 5: "your game does not know trading - update the mod", neither already trading; limits on pending requests (<= 6 per viewer: "requests waiting", "already has", "requests out already"). Success text `asked ... (body N)`; the target sees it in global 1; the request runs out after 10 s. Sender `0x859950(topic, kind, u32 body id, msg)` writes `[01][kind][u32 id]` and adds a pending entry (map `this+0x258`, seq) |
| 2 | `u32 request id`, `u8 accept` (0/1), end exact ("a broken answer") | answer (`0x85d430`): "no request #N waits for you (it may have run out)"; accept 0 -> status 2; accept 1: kind 1 opens a 30 s window to pick the asker's body up, kinds 2-4 start a bout (global 3), kind 5 calls `0x7d8fe0(player A, body A, name, player B)` = creates a ptrade record (call `0x85db81`; "could not start the trade: ...", "trading is off on this server"). Message to the asker appears in global 2. Sender `0x85a6f0` |
| 3 | none, must end right after the op byte (else "a broken stop") | stop my bout (`0x85ecb0`): "you are not in a bout"; `Sparring over - stopped by <name>` / "gave up" / "the bout is over". Sender `0x85ac70` ("asked the server to end the bout") |
| 4 | `u32 request id`, end exact ("a broken cancel") | withdraw my own request (`0x85c090`): "withdrawn by", "no pending request #N". Sender `0x859e00` (also used for "stopped walking to") |

**Open.**
- Gather filter details beyond the order; inner create / answer logic (`0x85c340`, `0x85d430`: only strings and the ptrade call read); slots 21 `0x8560f0` and 32 `0x856b90` (console commands, I).

### vote (kind 23) - "skip the pause" vote
Vtable `0x1243e18`, v1, period 0.5 s. Status: **partial** (row field meanings).
Functions: apply `0x868000`, intent `0x867c00`.

**Rows (server -> client).** Key `global:1`.
| field | type | limits | meaning (I) |
|---|---|---|---|
| a | u32 | - | stored at `0x1949e78` (I: vote round, the value an intent must echo) |
| b | u16 | - | |
| c | u16 | - | |
| flag | u8 | - | stored up to `0x1949e84` |
Strings "players voted to skip" (I: b, c = vote counts).

**Intent (client -> server).** Payload after the header (`0x867c00`, M; no end-exact check seen):
| op | fields | effect / reply |
|---|---|---|
| 0 | `u32 round` | take the vote back: player removed from the voter set (`this+0x28`); notice flag 1 "vote taken back" |
| 1 | `u32 round` | vote: player added to the voter set; notice flag 1 "voted"; log "vote: <player> votes / takes back the vote ..." |
| 2 | none | player added to the seen set (`this+8`, counter `this+0x54`); no notice (I: "I am here") |
Ops 0/1 are taken only while the server is in vote phase (`0x195ac78 == 3`), past a time threshold (double `0xeb5100`) and when `round` equals the current round (`0x195ab48` and `this+0x48`); else notice flag 0 "the vote is closed - the next wave is already coming". Any other op is ignored.

**Open.**
- Row fields b, c, flag; client sender not located.

### wallet (kind 24) - the faction wallet (cats)
Vtable `0x1244fe8`, v1, period 1 s. Status: **partial** (op 1 item fields and server effects `0x86ede0` not decoded).
Functions: apply `0x8705c0`, intent `0x86d460` (op 2 `0x86e300`, op 3 `0x86dde0`, op 1 effect `0x86ede0`).

**Rows (server -> client).** Key `global:1`.
| field | type | limits | meaning (I) |
|---|---|---|---|
| balance | u32 | >= 0 as signed | shown as "N cats" |
| second | u32 | - | kept at `0x1949eec` |

**Intent (client -> server).** Payload after the header: `u8 op` (M, `0x86d460`). Every op needs the wallet switch `0x1949e90` on; replies are notices with code = intent seq.
| op | fields (wire order) | effect / reply |
|---|---|---|
| 1 | `u8 sub` (must be 1 or 3); sub 3: 3 x `f32` (position); else `u32 id`; then 4 x `str16<=0x60` (the first 1..0x60 bytes, printable, no tab), `u8`, `u8`, `u8`, `u16 count` (!= 0); end exact | money transfer through `0x86ede0` (I from its strings "that is not money", "took", "too far": hand over or take a money item from a body (sub 1) or a ground position (sub 3)); flag 1 "+N cats" / refusal with flag 0; malformed -> "bad request" |
| 2 | `u32 amount` (signed, -200000..200000), `u32 speaker body id`; end exact | dialog cat reward (`0x86e300`); refusals "dialog rewards are off on this server" (`0x15a6228` off), "too much for one dialog line", "nobody to talk to", "the speaker is too far", "this prisoner was already handed in" |
| 3 | `str16<=0x60 name` (1..0x60, printable), `f32 change`, `u32 speaker body id`; end exact | dialog relation change (`0x86dde0`, log "wallet: dialog relations ..."); refusals as op 2 plus "too much relation change from dialogs in 10 minutes"; success "queued" |
| other | - | "unknown request" |

**Open.**
- Op 1 item descriptor (the 4 strings and 3 bytes), the effect function `0x86ede0` (6 KB), meaning of row field `second`; client senders not located.

### trade (kind 25) - NPC trader windows: stock rows, buy/sale deals, trader homes
Vtable `0x1248778`, v1 (slot 3 = `0x78ccd0`), period 0.5 s, kind 0x19. Status: **partial** (alternative deal paths, price functions, op 6/8 field A, host-book validator not decoded).
Functions: gather `0x8850a0` (helpers `0x886400`, `0x885a50`), apply `0x897d30`, remove `0x8999f0`, intent `0x886e30` (timing wrapper) -> handler `0x8870a0` (11069 bytes), notice `0x899260`, client tick slot 19 `0x898800`, layout parser `0x87a930`, layout writer `0x87a710`, senders `0x87e8b0` (op 1), `0x87e800` (op 4), `0x87e970` (op 5), `0x87f2e0` (ops 6/8), `0x8804d0` (ops 2/3/7); all `0x547b00(0x19, buffer)`, buffers `[op][u32 trader id]...`. Slot 26 stub. (Ghidra lists apply with size 11; the decompile is the real body.) The wrapper logs "trade: slow intent from player N - X ms:" with per-stage times when the handler exceeds `[0x15a6218]` ms.
Client state (M): feature flag `0x1949f57` (rows are applied only while set), trader being traded with `0x194a054` (u32), received rows `0x194a094`, expected `0x194a090`.

**Layout block** (trade form: parser `0x87a930`, writer `0x87a710`): the stores container layout block **without X**: `u16 n` (<= 0x200), n x str16<=0xa0 (client vector `0x19697b0`), `str16<=15000` item text (parsed by `0xbb9070`, callback `0x87a6a0`, 0x100-byte pieces).

**Rows (server -> client).** Three forms by key.
Key type 5, id `0x54524448 "TRDH"` (trade flags; no end check; server writes constant 2 at the head of the gather, only when `[0x15a6c5a]` and `0x879630(viewer)` hold):
| field | type | limits | meaning (I) |
|---|---|---|---|
| b | u8 | - | 0: `0x194a11e = 0`, `0x1949f56 = 0`; 1: `0x194a11e = 1`, `0x1949f56 = 0`; >= 2: both 1. "trade available / the server holds the stock" |
Key type 5, id `0x5452444d "TRDM"` = trader manifest (written by the gather tail; secondary writers `0x886400` / `0x885a50` emit the same shape for other trader kinds; no end check visible):
| field | type | limits | meaning (I) |
|---|---|---|---|
| trader id | u32 | must equal `0x194a054`, else row ignored | |
| B | u32 | compared with the trader's current value read by `0x87a460`; if different and `0x87e6e0` false, set by `0x87a490` | trader's cash in cats (I, strong: same getter/setter the deal code uses for `money +/- price`, clamped 0..2,000,000,000) |
| count | u16 | - | rows the server sent for this trader; when the client's received count `>=` it, `0x194a081 = 1` (complete) |
| home known | u8 | nonzero -> the floats are the trader's server home | |
| x, y, z | f32 each | range ±3.0e38 | server home; if different from the trader's home (`0x879f00` / `0x879db0`) the client logs "trade: trader N got its server home at (x, z)" (+ " had another home - ") and sets `0x194a09a` |
Any other key (server: place key with d = `0x54524446 "TRDF"`, or a type-1 body key) = trader stock row (gather `0x8850a0`, `0x886400`, `0x885a50`):
| field | type | limits | meaning (I) |
|---|---|---|---|
| trader id | u32 | must equal `0x194a054` | |
| kind | u8 | 1 = stock on a body (key type 1, id = next field); other = at a world place | |
| id | u32 | kind 1: body id; kind 0: written as 0 by `0x886400` | |
| x, y, z | f32 each | range ±3.0e38 | matches a place within xz distance `[0x12c1c4c]` and y distance `[0xeb978c]` of a known trader place |
| layout | block (above) | end exact, else bad | |
Effect: finds the trader's container by (kind, id) or by position (`0x87a3e0` lists the trader's containers), or for kind 0 creates / locates a place (`0x87c210`); applies the layout (`0xbb7580`) with a 30000 ms cool-down after a failed relay and at most 0x200 pending records.
Remove (`0x8999f0`): only key type 5 id TRDH: clears `0x194a11e` and `0x1949f56`; every other key ignored.
Gather (M partial): per viewer, for each known trader within `[0xeb54bc]` or recently seen (< 900000 ms / < 190000 ms): stock rows (`0x886400` bodies/places and kind 0, `0x885a50` other kind), then one TRDM with the row count, TRDH once; stale rows dropped (`0x8a4b70`).

**Intent (client -> server).** Payload after the header: `u8 op`, `u32 trader id`, then op-specific. Reply = notice `0x547e50(player, 0x19, code = client seq, flag, text)`. Unknown op -> "unknown request"; any bad read / validation -> "bad request".
| op | fields after the trader id | effect / reply |
|---|---|---|
| 1 open | none, end exact | server resolves the trader (host hook `[0x1965158](id)` live body, or trader book `0x883470`), checks distance (<= `[0xeb54bc]`) and that it is not busy; logs "trade: open of player P with trader T"; flag 1 text "open". Refusals: "the trader is far, fighting or unknown", "the trader is busy with another customer", "this trader has no stock on the server", "no such trader near you", "no body on the server and no trader book of that record - ...", "the book's trader is ...", "... units away or its place is unknown". Sender `0x87e8b0` `[1][id]` (host-callable window open `0x880200`; seq stored in `0x194a09c`) |
| 2 buy | `u32 U`, `str16<=0x60 S1..S4`, `u8 b1`, `u8 b2`, `u8 b3`, `u16 Q`, `u32 R` | buy (`0x889e50`); end exact. S1 non-empty, 1..0x60 chars, S1..S4 chars 0x20..0x7e, `Q != 0`, `R >= 0` signed. `U` = the paying body id (I, strong); `U == 0` -> the player's current body (`player[1]`) |
| 3 sale | same as op 2 | sale (`0x88abd0`) |
| 7 sale with bag | as op 2, then `str16<=0x60 S5`, `u16 P1`, `u16 P2` | sale (`0x88abd0`); sets `0x1949f84 = P1`, `0x1949f88 = P2`, `0x15ab298 = S5` first; if S5 non-empty `0xbcc4a0(trader body, S5, P1, P2)` (named-bag placement, I) runs before the sale. Client falls back to op 3 after "unknown request" |
| 4 close | none, end exact | drops the player's trade record (`0x8a5120`, `0x87aaa0`); flag 1 text "closed". Sender `0x87e800` `[4][u32 0x194a054]`, from `0x8990a0` when `0x194a054 != 0 && [0x194a099]` |
| 5 places | `u8 n` (<= 0x30), n x {`f32 x, f32 y, f32 z`} (each u32 via `0x543bf0`, range ±3.0e38); end exact | client tells the server the storage places of a trader's home. Silent (no notice) refusal if the payload is bad, no open record (map `0x8a5120`, record trader id at `+0x20`), id mismatch, or trader book `0x883470(id)` fails; ignored if `0xb7b8f0(book key, &text)` is true (I: server already has a home layout); else `0x1bbc40(book entry, 3n floats, &reason)` (host-book validator, 10684 bytes): true = accepted and a restock follows (I), false logs (first 0x14) `trade: player P named n storage place(s) of trader T's home - not taken: <reason>`. Reasons seen in `0x1bbc40`: "a place is far from the trader (", "the book's own shelf is claimed by another trader's book (", "the property book has no NPC shelf at the book's shelf (", "the home there lacks a shelf of the book (", "nothing new (", "the book refused the home". Sender `0x87e970`: from slot 19 (at `0x898f42`), once per window (`0x194a0bd`), when the manifest is complete (`0x194a081`) and the server home unknown (`0x194a09a == 0`); places = the trader's containers (`0x87a3e0`) with kind field 0, max 0x30; dedupe map `0x1969a40` |
| 6 / 8 home | `u32 A`, `f32 x, f32 y, f32 z`, `str16<=0x60 name` (printable, else bad), `u8 n` (<= 0x30), n x {`f32 x, f32 y, f32 z`; **op 8 also** `str16<=0x60 place name` (printable, else cleared), `u8 kind` (255 = none)}; end exact | host reports a trader's home (host-authored): handler calls `0x1bf1f0(player, id, A, ...)` (20361 bytes) to validate that the host may name this home; failure logs "trade: host #N named the home of trader T (K place(s)) - not taken: <reason>" (first 0x28, then every 200th). No notice. Sender `0x87f2e0(tick ms)`: op 8 when `[0x1949f56] && [0x15a61e7]`, else op 6; per tick (slot 19, at `0x898982`) every >= 2000 ms, only if `0x194a11e`, kind 0x19 enabled and host mode `(*[0x19420a8])() == 2`; (trader id, A) pairs from `0x8a730c0`; kind = `0x87beb0(place)` clamped 0..254; resend suppressed by an FNV hash of the layout (map `0x19699d8`, 0x2000 entries, resend after 599999 ms) |
Deal rules (ops 2/3/7; M from the decompile only, not cross-checked in disassembly). Failures in the shared read/validation -> flag 0 "bad request". After the read: gate `0x889be0` (disassembled): (1) the player's open-trade record for this trader exists (id at `+0x20`, stamps `+0x28`) else "the trade window is not open - talk to the trader again"; (2) trader resolved as in op 1 else "the trader is not here"; (3) if `U != 0`, `U` must be in the player's squad vector else "that body is not in your squad"; (4) trader-to-body distance (`0x8834d0` / `0x87bd00`) <= `[0xeb7204]` else "too far from the trader". Item descriptor = S1 (item name) @0, S2 @0x28, S3 @0x50, S4 @0x78 (std::string), b1..b3 @0xa0..0xa2 (0xa3 bytes), passed to the matcher `0xbccc70` / `0xbcc2a0` (b2 compared with 100, b3 with 6; I: condition percent, grade). Reply flag 1 with the deal text, or flag 0 with the reason; after either outcome `0x543830(player, U)` own -> `0x44cfd0(U)` (bag touched); on refusal also `0x44d1d0(player, U)` (re-send the bag) and `0x548160(kind, player)` (re-send rows), log `trade: <buy|sale> of player P - Q x "S1" with trader T REFUSED: <reason>` (first 0xc8, then every 0x32nd).
- Buy (`0x889e50`, 3443 bytes): needs `[0x1949e90]` and the player's wallet entry (`0x8768b0`) else "the wallet is not ready"; trader kind result 2 or no live body -> alternative paths `0x88d530` / `0x893d40` (not decoded); "the shop's zone is loading - try again". Item found among the trader's containers (up to 320; best rank < 2) else `the trader has no "<S1>" any more`; unit price `0x884050(trader, item, 0)` < 0 -> "the price did not read". Charge: `computed = 0x87b410(...)` (inputs unit price, Q, per-(player,trader,item) "rung" from `0x87b630` = `value - 0x14`, default 0x28a = 650, `0x87adc0`); `cap = max(Q*unit*3 + 1, computed)`; `R < computed` -> charge `computed`, else `min(R, cap)`. Charge > wallet (`0x86a430`) -> `not enough money (<charge> cats, you have <balance> cats)`. Move `0xbd0540(...)` returns `m` moved: 0 -> `the item did not move (<container text>)`, `m < Q` -> price pro rata (`R*m/Q`, same clamp). Then `0x87b9c0` records the purchase, wallet debit `0x8768d0(wallet, -charge)`, trader cash `+= charge` (clamped 2e9), `0x884690(trader)`. Log `trade: <player> bought <m> x "<S1>" from trader <T> for <charge> cats (asked <R>, fair <Q*unit>, rung <n>)`; reply `-<charge> cats`.
- Sale (`0x88abd0`, 3452 bytes): same wallet / zone gates (alternative paths `0x890810` / `0x895c10` not decoded); item must be on the seller's body (`0xbccc70`) else `you have no "<S1>" on that body`; unit price `0x884050(trader, item, 1)` (< 0 -> 0); rung default 0x578 = 1400 (`value + 0x14`, capped 0x577); `floor = Q` if unit > 0 else 0; `computed = 0x87b450(...)` raised to at least `floor`; `R < floor` -> pay `floor`, else `min(R, computed)`. Wallet + pay > 2,000,000,000 -> "your wallet is full"; trader cash (`0x87a460`) < pay -> `the trader has not enough money (<cash> cats)`; no container accepts the item (`0xbd0540` over each) -> "the trader has nowhere to put it"; partial moves as in buy. Success: wallet credit, trader cash `-= pay`; log `... sold <m> x "<S1>" to trader <T> for <pay> cats (asked <R>, fair <Q*unit>, rung <n>)`; reply `+<pay> cats`.
Sender `0x8804d0(op, U, descriptor*, Q, R, bag*, P1, P2)` writes `[op][u32 0x194a054][u32 U][str16 S1..S4][u8 b1][u8 b2][u8 b3][u16 Q][u32 R]` and, if `bag* != 0`, `[str16 S5][u16 P1][u16 P2]`; op forced to 7 when `bag* != 0`; Q 0 -> 1, capped 0xffff; strings cut to 0x60; P1/P2 capped 0xffff. Callers: host-callable `0x880f40` (op = `3 - (buyflag != 0)`; P1/P2 from its record `+0xe8/+0xec`; no bag once `[0x1949f55]` says the server rejected op 7), slot 20 (resend as op 3), console slot 33 `0x89a950` (`spine trade buy|sale <trader id> <item> [b1] [b2] [b3]`, U = 0, Q = 1, R = 0).
Notice texts, flag 1: `open`, `closed`, `-N cats` (buy), `+N cats` (sale). Flag 0: `unknown request`, `bad request`, op 1 reasons, gate and deal reasons above. Ops 5, 6, 8: no notice.
Client notice handler (slot 20 `0x899260`, M): sets `0x194a099 = 1` when `code == 0x194a09c` (open answered); pending-deal map `0x1969798` (key seq; `+0x20` U, `+0x28` descriptor copy, `+0xd0` Q, `+0xd4` R, `+0xe8/+0xec` P1/P2) is read then erased in every case. Flag 1: nothing more (deal text not parsed; wallet change arrives by rows). Flag 0 with text `unknown request` and a pending deal: `0x1949f55 = 1`, log "trade: the server does not know a sale with the item's place - the same sale goes the old way (spine trade sellat)", re-send via `0x8804d0(3, U, descriptor, Q, R)` (no bag) under a new seq. Other flag 0: log `trade: the server refused - <text>`; for a deal code with the window open, host function ordinal 0x113 (`0xcd9b10(0x113,...)`, guard `0x195a918`; I: rolls the deal back in the UI) and UI message `Trade refused: <text>`; for the open seq: "the trader is busy with another customer" -> UI `The trader is busy with another customer - wait a moment`, other texts -> `This trader cannot trade with you right now (<text>)` (only when `[0x15a6217]`).

**Open.**
- Op 6 / 8 field `A` and the validator `0x1bf1f0` (reason strings: "v2 hosthome is off", "no trader book of that record", "not the zone's host now (epoch ...)", "the building is far from the record", "the towns' layout gives that building to ...", "(v2 layouthome)", "(v2 furnnew 0)").
- Alternative deal paths `0x88d530` / `0x893d40` (buy), `0x890810` / `0x895c10` (sale); price functions `0x87b410` / `0x87b450` / `0x87adc0` / `0x884050` / `0x87b630`; item move `0xbd0540`; matcher field meanings (S2..S4, b1..b3); `0xbcc4a0`.
- Host function ordinal 0x113; host-callable entries `0x880200` / `0x880f40` (callers outside this DLL's decompile).

### dialog (kind 26) - dialogue packages of other players' bodies
Vtable `0x1239378`, v1, period 5 s. Status: **full** (Kenshi-side calls not resolved).
Functions: gather `0x7c0f80`, apply `0x7c1520`, package-list builders `0x7c0dd0` -> `0x7c0bd0`, package lookup `0x7c0df0`. Slot 15 is a stub (`0x7ffa80`): rows are never removed on the client. No intent, no upstream.

**Rows (server -> client).** Key (M, `*key != 1` -> `this+0x30`++, return 2): type 1 `body:`, `u32 id` = body entity id.
| field | type | limits | meaning |
|---|---|---|---|
| n | u8 | <= 8 (else `this+0x30`, return 2) | writer emits `count & 0xff` of the body's package list |
| package[i], i < n | n x str16<=0x40 | | dialogue package names (writer: elements of a 0x28-byte-stride string vector from `0x7c0dd0`/`0x7c0bd0`) |

End exact (`pos == len`), else `this+0x30`, return 2.
Apply flow (M): body lookup via hook `*0x1965158(id)`; none (or hook absent) -> `this+0x2c`, return 1 (hold). Body for which `0x4517b0(body)` is true -> skip (`this+0x24`, return 0; I: a body the local player controls). Per-body-id cache of applied names (map `this+0x48`, flushed above 0x1000 entries); only new names are applied (`this+0x38`/`+0x20` when nothing new). For each new name: package lookup `0x7c0df0(name)`, then a Kenshi function found by signature (`0xcd9b10(0x111,...)`) is called as add-package(character, package, ...). Log (<= 16 lines, `this+0x3c`): `dialog: body <id> got its dialogue package(s) <names>` (strings `0x12391c0`, `0x12391a0`). Returns (disasm eax at `0x7c1b43`/`0x7c1b63`/`0x7c1b8b`): 0 ok/ignored, 1 hold, 2 bad.
Gather (M, `0x7c0f80`; only if `*(param_2+8) != 0`): iterates viewer-visible body ids (`0x650ec0` with radius/limit at `0xeb7a7c`; skips the viewer's own squads from hook table `0x1965140`), builds each body's package-name list, skips bodies without packages, at most 0x100 rows per call (overflow `this+0x10`).

**Open.**
- What `0x7c0bd0` reads from the body (I: the character's dialogue package list).
- The Kenshi function behind signature 0x111; semantics of `0x4517b0`.

### stealth (kind 27) - per-body "being noticed" level
Vtable `0x1239688`, v1, period 0.25 s. Status: **full** (rows; no intent).
Functions: apply `0x7c4130`.

**Rows (server -> client).** Key `body:<id>`.
| field | type | limits | meaning (I) |
|---|---|---|---|
| level | u8 | < 3 | noticed level |
End exact; anything else counts as bad.

### locks (kind 28) - lock state and slot lists at world positions; pick begin / finish
Vtable `0x123f718`, v1, period 1 s. Status: **partial** (op 2 branch details, cooldown bookkeeping, slot-record source not decoded).
Functions: gather `0x826560`, apply `0x829c10` (listed at 5-byte thunk `0x8285b0`), remove `0x8285c0`, intent `0x827ba0`, row writers `0x8295a0` (lock row), `0x829780` (slot list row), notice helper `0x829a80`, sender `0x824b60` (call `0x824c99`, kind 0x1c), timer driver `0x8283b0`. Slots 20 and 26 stubs. (The decompile under the thunk headers is the real body.)

**Rows (server -> client).** All rows use key type 2 (place) = x, y, z; d selects the row kind; any other key type is bad.
Lock row, d = `0x4c4f434b "LOCK"` (writer `0x8295a0`, same order as apply):
| field | type | limits | meaning (I) |
|---|---|---|---|
| flags | u8 | bits below | |
| text1 | str16<=200 | only if `flags & 0x0c` | owner note / description text |
| text2 | str16<=800 | only if `flags & 0x10`, read inside the `flags & 0x0c` branch | second text |
End exact. Flags: bit 0 = lock flag A (building struct `+0x20`, set via `0x82a5e0`), bit 1 = flag B (`+0x21`), bit 2 (0x04) text present, bit 3 (0x08) text present with the second record flag, bit 4 (0x10) second text present, bit 5 (0x20) authoritative: when set (or no text) the client applies A/B to the Kenshi object found at (x, y, z) (`0x449e70`), otherwise it only stores the text. Applying for A = 1 is rate-limited: per-object timestamp in map `0x1969508` blocks a re-apply within 4000 ms. Stored by `0xc3ea30(key, flags, text1, text2)`.
Slot list row, d = `0x534c4f47 "SLOG"` (writer `0x829780`):
| field | type | limits | meaning (I) |
|---|---|---|---|
| count | u8 | <= 0x18 (24) | |
| per entry: id | u32 | - | |
| per entry: short | str16<=0x20 | - | |
| per entry: long | str16<=0x80 | - | |
| per entry: value | u16 | writer clamps 0..0xffff | |
| per entry: bool | u8 | - | |
End exact. Client element 0x60 bytes; stored by `0xc3ec50(key, vector)`. Server writes it only for places within the squared distance `[0x12c87ac]` of the viewer and only if the "has slots" flag (`param_6[1]`) is set; vector from `0xc37c80` (per-place map `0x196f9b8`).
Remove (`0x8285c0`, key place): d == SLOG clears the slot-list part, any other d clears the lock part; the entry is deleted when both are empty (SRW lock `0x19560d8`).
Gather (M): per viewer over the server lock records and places in range; flags = `(B ? 2 : 0) | (A ? 1 : 0) | 0x20` for locks read from a live Kenshi building (`0x82a5a0` reads building `+0x438`: `[+0x20]`, `[+0x21]`, int `[+0]`); without a live building only the text flags are written. Meaning (I): door / storage / cage lock state at a world position (A locked, B second state).

**Intent (client -> server).** Payload after the header, end exact (else bad):
| field | type | limits | meaning (I) |
|---|---|---|---|
| op | u8 | 1 or 2 | 1 = begin picking, 2 = pick finished |
| target kind | u8 | 1, 2, 3, 5 | names from `0x8248d0`: 1 a door, 2 a storage, 3 a cage, 5 its own shackles |
| x, y, z | f32 each (u32 reinterpreted) | between `[0x12c1bd8]` and `[0xeb5e6c]` (about ±3.0e38), not NaN; out of range clears ok | lock position |
| body id | u32, only if kind == 5 | must be one of the viewer's own body ids (`player+0x18..0x20`); sender takes it from `0x1949d58` | |
Sender `0x824b60(op, kind, x, y, z)` writes exactly this, then kind 0x1c; op 2 is sent from the timer driver `0x8283b0` (queue `0x1969548`, 32-byte records, due time at `+0x18`).
Effect (M control flow): op 1 stamps a record (GetTickCount64, body id) for that lock in map `0x19694e8` (re-stamps if the old stamp is > 599999 ms old or belongs to another body; pruned by `0x82b000` above 0x100 entries) and starts the pick on the server side (`0x8257a0`, `0x27bc10`). Op 2 needs the stamp to exist and to be 1000..600000 ms old, else notice "the pick was not seen on the server"; then unlocks (`0x825830`) and for doors calls `0x27baf0`; logs "locks: player P picked a door/a storage/a cage/its own shackles at (x, z) with body B - unlocked" (first 40). Refusals: "the server has no lock there", "nobody of yours stands at that lock on the server", "the pick was not seen on the server". Target lookup per kind: 1 `0x825540` / `0x27b2c0`, 2 `0x825c90`, 3 `0x1b2540`, 5 `0x7802f0`.

**Open.**
- Op 2 branch and cooldown bookkeeping in detail; slot-record source `0xc37c80` / `0xc37390`; meaning of the slot-record fields.

### serverinfo (kind 29) - server performance stats, MT status, attack slots
Vtable `0x123f938`, v1, period 5 s. Status: **full** (feeding counters unnamed).
Functions: gather `0x82e7a0`, apply `0x82ecb0`, attack-slot setter `0x82e530`, console `0x82ef20` ("serverinfo slots ..."), clear slots 15/21 `0x82eee0`/`0x82ef00` (not decoded further). No intent, no upstream.

**Rows (server -> client).** Three singletons keyed `global:<n>` (writer uses type 5; the client apply tests only the u32 at key+4, not the type): n == 3 -> attack-slots layout; n == 2 -> MT layout; any other n (the writer only uses 1) -> stats layout. Each apply: `this+8`++; wrong end/short read -> `this+0xc`++, return 2; success returns 0. All layouts end exact (`pos == len`).
`global:1` stats (8 bytes):
| field | type | limits | meaning (I from display code `0xa84f2..0xa8750`) |
|---|---|---|---|
| a | u16 | | zones (writer `min(0x1941f04, 0xffff)`); stored `0x1969568` |
| b | u32 | | NPC count (`0x1941f00`); stored `0x196956c` |
| c | u16 | 0..65535 | server FPS x 10 (writer `float*10.0+0.5`, saturating); stored `0x1969570`, time `0x1969578` |

Display (`0xa84f2`): `   |   server: <a> zones, <b> NPC, <c/10+0.5> fps`, only while the record is younger than 0x4e20 ms. Writer skips all rows of the gather when fps <= 0 and b == 0.
`global:2` MT (4 bytes):
| field | type | limits | meaning |
|---|---|---|---|
| state | u8 | 0 off, 1 on, 2 warming up, 3 OFF (thread fault) | stored `0x1969580` |
| threads | u8 | 0..255 | worker count + 1 clamped (writer `0x1901024 + 1`); stored `0x1969584` |
| third | u8 | | `0x1941dfc & 0xff`, stored `0x1969588`, no reader found (I: unused/spare) |
| pad | u8 | read and dropped | 0 |

Display `, MT: on (<threads> threads)` / `warming up` / `OFF (thread fault)` (strings `0x11b84a8..0x11b84d8`); time stored `0x1969590`.
`global:3` attack slots (3 bytes):
| field | type | limits | meaning |
|---|---|---|---|
| version | u8 | must be 1, else bad (2) | |
| level | u8 | 1..5 | "how many enemies may hit one body at once": `0x82e530` sets `0x1949d90 = level` and applies it via `0xc0b910`/`0xc0b990` (getter/setter, names I); log strings `0x123f890`, `0x123f828` |
| pad | u8 | read and dropped | 0 |

Writer emits this row only if the current local value read by `0xc0b910` is valid and in 1..5.

**Open.**
- What feeds `0x1941f04`, `0x1941f00`, `0x1941ef4`, `0x1941dfc`, `0x1901024`, `0x1901050`, `0x1901080` (server runtime counters written elsewhere).

### spawn (kind 30) - first-place choice of a joining player, /suicide, /unstuck
Vtable `0x1241188`, v1, period 1 s. Status: **full** (placement loop not decoded).
Functions: gather `0x835d20`, apply `0x8376a0` (requires `*key == 5`, id not checked), clear slots 15/21 `0x837bc0`, intent `0x836210` (slot 12), server tick `0x836960` (slot 11), client tick `0x837c00` (slot 19), senders (kind 0x1e via `0x547b00`) at `0x8339e7`, `0x8354cc`, `0x83a0c4`.

**Rows (server -> client).** Key `global:1` (writer: type 5, id 1).
| field | type | limits | meaning |
|---|---|---|---|
| flag | u8 | | stored `0x1949dec`, sets `0x1949db3 = 1`; writer `0x83b220(player) != 0` (I: player already placed / may not choose) |
| n | u8 | apply: n <= 0x10 (loop also stops after 16) | writer: number of table entries matching the viewer's mask (<= 9) |
| entry[i], i < n | n x { u8 index, str16<=0x30 name, f32 x, f32 z } | floats raw LE 4 bytes, validated to +-3.0e38 (`0x12c1bd8`/`0xeb5e6c`); out of range clears reader ok -> bad | writer sends `index` = position 0..8 in the static 9-entry table at `0x100feb0` (stride 0x18: `char* name`, `f32 x @+8`, `f32 y @+0xc` (not sent), `f32 z @+0x10`, `u32 mask @+0x14`) |

End exact (`pos == len`), else `this+0xc`++, return 2 (at `0x837b7a`); success `xor eax,eax`.
Table (M, `0x100feb0`): 0 "Black Desert City" mask 1 (x=8570.0 z=-13039.0); 1 "Squin" 2; 2 "Admag" 2; 3 "Stoat" 4; 4 "Clownsteady" 4; 5 "Blister Hill" 8; 6 "Western Hive" 0x10; 7 "Hive Village" 0x10; 8 "Mongrel" 0xff (always matches). Coordinates are Kenshi world floats.
Gather filter (M): the viewer's body -> `0x832fa0` + `0x831f30` give a race bit mask; entry included if `mask & entry.mask != 0` or `entry.mask == 0xff`. Gather skips when `0x1949db0 == 0` (after trying `0x8324f0` once), `0x195cc08 != 0`, or the viewer's `param_2[1]` (body id) is 0; it also needs the viewer's name list from `0x613fc0`/`0x674970`.

**Intent (client -> server).** Handler `0x836210` (player u32 @`param_2[0]`, body id u32 @`param_2[1]`); first payload byte = op.
| op | fields after op | effect / reply |
|---|---|---|
| 0..8 | none; exact end, else `0x1949dc4`++ and drop | "choose first place": index into the table above (I from log strings). Needs body id != 0 and an entry in the player's name list (`0x613fc0`/`0x674970`); refused if the player already has a pending entry (`0x83b220 != 0`). If the viewer's race mask does not match the entry mask (and mask != 0xff): log `spawn: player <id> asked <place> - not for this race (<race>) - refused`. Else creates a pending record (`0x83ac30`, keyed by account name) {player u32, body u32, index u32, tick @+0x20/+0x28 = GetTickCount64, state @+0x38 = 0}, calls `0x467730(x,y,z of the entry)`, logs `spawn: player <id> (<name>) starts in <place> - the server loads the town and looks for free ground`. |
| 0xf0 | str16<=0x40 account name; exact end | "/suicide" (handler `0x838910`). Name must equal the sender's account name (`0x613fc0`), else `/suicide: that is not your account name - nothing happened`; no living character (`0x47d6a0` false): `/suicide: you have no living character right now`; else the character is killed (`0x83b160`), log `... took their own life (/suicide)`. Reply text goes to the player as a notice via function pointer `0x19425d0`. |
| 0xf1 | f32 x, f32 y, f32 z (raw LE 4 bytes each, NaN rejects); exact end | "/unstuck" (handler `0x838c90`). Refusals (notice to the player): no character, "choose your first place first", "not in a fight", "not while you are down", "not while you are held in a cage, a bed or a seat", "once a minute - wait N s" (limiter 60000 ms per account in map `0x19695e0`, trimmed above 0x200 entries). Client position trusted only if within 420 units (176400.0 = 420^2 at `0x1312398`) of the server's own position of the body and y in (-1000, 20000) (`0x12c2808`/`0x12c1c88`); otherwise searches from the server position. Free-ground search `0x8330b0` (up to radius 6 steps); failure -> `/unstuck: no free ground nearby - try a few steps away`; success -> teleports the body via `0x4680b0(body,x,y,z)`, log `... /unstuck - moved <d> units`. |

Any other op (9..0xef, 0xf2..) = bad (`0x1949dc4`++).
Senders (M by `mov ecx,0x1e` + call, each builds a v1 blob: op byte then payload):
- `0x8339e7` (in function around `0x8339a5`): op = low byte of dword `0x15a6244` (chosen place index; -1 = none, no send); sent only while dword `0x1949dec` (row flag) is 0. On success stores GetTickCount64 in `0x1949e10` (client waits 4000 ms before re-asking, slot 19 `0x837c00`).
- `0x8354cc` (function `0x835330`): op 0xf0 + str16 of the local account name (<= 0x40); send failure shows `NOT SENT - NOT CONNECTED`, else `ASKED THE SERVER - YOUR CHARACTER DIES`. UI helper `0x8352e0` resets client state.
- `0x83a0c4` (function `0x839c60`): op 0xf1 + 3 x f32 (as two u16 each); the client probes 6 candidate positions with the free-ground test `0x8330b0` (first success wins, else the fallback); not connected -> `/unstuck: not sent - not connected`.

Client tick `0x837c00`: calls the above while the first-place window is open; if the server took the first place ("the server took our first place - the window is closed", `0x1240b70`) the window closes.
Server tick `0x836960`: walks the pending-record map (`0x1969608`). Per record: body no longer exists (`0x1965158(body id)` null or `0x833070` fails) -> `spawn: <name> - the trip to <place> gave up (no body) - they choose again`, record erased; older than 90000 ms -> notice `THE TRIP TO <place> DID NOT WORK - YOU START WHERE YOU STAND` and `where the character stands - the trip did not take`; otherwise looks for free ground at the place (strings ` - free ground in `, ` - no free ground found from afar in ... - moving to the town centre first`, `the town centre (no free spot found)`) and moves the character (`0x8398f0`).

**Open.**
- Placement loop details of the server tick.
- Meaning of the row flag; mask source `0x831f30`.

### weather (kind 31) - weather effect sets and puts (acid rain)
Vtable `0x12539e8`, v2, period 0.5 s. Status: **partial** (field meanings).
Functions: apply `0x936730`, effect-set decoder `0x9368e0` (Ghidra drops the reader argument at the call).

**Rows (server -> client).** The key's u32 id selects the form (key type not checked).
Effect set, ids `0x1000000..0x1ffffff` (`0x9368e0`):
| field | type | limits | meaning (I) |
|---|---|---|---|
| idx | u16 | <= 0xfff | |
| name | str16<=0x30 | non-empty | effect set name |
| 5 x v | f32 | ±3.0e38 | |
| on | u8 | any (stored as bool) | |
End exact.

Effect put, ids `0x10000..0x10fff`:
| field | type | limits | meaning (I) |
|---|---|---|---|
| a | u16 | - | |
| b | u32 | - | |
| name | str16<=0x40 | non-empty | effect name |
End exact.

**Open.**
- Field meanings.

### cellhost (kind 32) - zone-host assignment, body ownership with event/order echo, host intents
Vtable `0x1260ee8`, v6, period -. Status: **partial** (row and intent layouts decoded; producers of name/player strings, ops 6/8/9 meanings and slots 21/24 open).
Functions: gather `0x9561f0`, apply `0x959810`, remove `0x95b660` (not decoded), intent `0x9583a0`, slot 19 `0x95e9d0` (client sender, lossy `0x547ce0` with kind 0x20), slot 21 `0x95b880`, slot 24 `0x962bd0` (both not decoded), senders `0x95e9d0` and the per-op sites below.

Layouts and limits M (decoders, cross-checked against the writers named); meanings I. All decoders work on a byte string copied out of the reader (`0x543e40`); "u32" = two LE u16; "end exact" = consumed == length. Failed row -> counter `0x194baac`. f32 in this topic = `0x9f940` (two u16, rejects NaN/Inf only).

**Rows (server -> client).** The apply copies the rest of the value into a byte string, then parses by key type and id (key type IS checked): global 5, player 4, name 3, body 1.

| key | decoder | value layout (wire order) | limits | meaning (I) / writer |
|---|---|---|---|---|
| global id -1 (0xffffffff) | inline | `u8 flags`; length exactly 1 | bit0 set, bits 4-7 clear | bit1 = avatar wounds in a host's cell are laid by the HOST (else by the server, "G3"), bit2 = "PvP too". Stored `0x194b850` / `0x194b60a` |
| global -2 | `0x93eab0` | `u8 n`, n x `u32`; end exact (1+4n) | 1 <= n <= 32 | zone ids whose authority is moving between hosts; log "authority over N zone(s) near us is moving between hosts (first <zone>) - Loading area...". Writer `0x93e9d0` (key id 0xfffffffe) |
| global -3 | inline | `u8 n`, n x {`u32`, `u32`}; length exactly 1+8n | n <= 0x40 | pair list (zone -> host, I) |
| global -4 | inline | `u8 n`, n x {`u32`, `u32`}; length exactly 1+8n | n <= 0x40 | second pair list |
| global -5 | inline | 1 byte, non-zero; length exactly 1 | | `0x209070(true)` |
| global -6 | inline | `u8 n`, n x `u32`; length exactly 1+4n | 1 <= n <= 16 | avatar ids whose fight was refused: log "the server refused my fight with avatar N"; map `0x196bdb8`, timers +6000 ms |
| global, any other id | `0x938930` | `u32 zoneId`, `u32 epoch`; end exact (8 B) | epoch != 0; zoneId == key id (apply) | log "this client now HOSTS zone N (epoch E)". Writer `0x9388a0` (gather `0x9561f0`) |
| body (1), id = body handle | `0x938ec0` (output struct 2180 B) | `u32 epoch`, `u32 zoneId`, `u8 n`, n x **event** (43 B), then optional **order tail** (22 B) | epoch != 0; n <= 8; total length exactly 9+43n or 31+43n (with tail) | server tells a host it owns the body: "body <key> is OURS now (zone, epoch) - its mind is on, the server's snapshots no longer drive it". Events logged "got seq= v= a= epoch= acquired=" and queued per body if seq > last seen (map `0x196c110`). Order tail kept per body if seq newer (`0x196bb18`), then `0x942fd0` matches it against the last 4 local orders (same mode, target, position within epsilon, < 4000 ms) = server's echo of a move(1) / attack(2) order. Writers `0x938a70` (no tail) / `0x938cf0` (with tail, writes count 0 when no events) |
| player (4) | `0x93d7b0` | `u32 id`, `u16 len`, len bytes (string), then optional rest = `u8 n`, n x event [+ order tail] | id != 0; len <= 0x1000; rest parsed by `0x938ec0` after prefixing 8 bytes (epoch 1, zone 0) | row whose id is our own client id (`*0x19429a8`) must carry an empty string (else bad); others update a per-key {id, string} entry and queue events (max 256). String meaning unknown. Writer `0x93d6a0` (called from `0x98eaf0`): u32, u16 len (0 if > 0x1000), bytes, then `0x938a70` output minus its 8-byte head |
| name (3) | `0x93db00` | `u32 id`, `u16 len`, len bytes; end exact | id != 0 and == key id; 2 <= len <= 0x1000 | string stored per id (skipped for own id), hashed with `0x5433f0`; meaning unknown. Gather `0x98e640` copies a prebuilt string from map `0x196b548` |

**Event** (43 B, same shape as the squads 'H' element; stored stride 0x30): `u32 seq` (!= 0), `u32 a` (!= 0), `u32 c`, 6 x `f32` (NaN/Inf rejected), `u8 k` (<= 3), `u16`, `u32`. Any bad element fails the row. Writer `0x938a70` writes the last <= 8 events from struct offsets 0, 4, 8, 0xc..0x20, 0x24 (u8), 0x28 (u16), 0x2c (u32). Meaning (I): hit/damage events relayed to the owning host.
**Order tail** (22 B): `u8 magic` = 0xa5, `u32 seq` (!= 0), `u8 mode` (1 or 2), `u32 target`, `f32 x`, `f32 y`, `f32 z`. Writer `0x938cf0`.

**Intent (client -> server, kind 0x20, handler `0x9583a0`).** The handler copies the whole payload after the 5-byte header into a byte string; byte 0 = op. Unknown op -> counter `0x194b744`.

| op | decoder | fields after op (wire order) | limits | client encoder / send site | effect (I) |
|---|---|---|---|---|---|
| 2 | `0x939c60` | `u16 n`, n x {`u32 body`, `u32 epoch`, 8 x `f32`, `u8 c`, `u8 flags` (only bit0 allowed), if bit0: {`u8`, `u8`, 4 x `i16`, `u8`, `u8`}}; end exact | n <= 160; total in [3+42n, 3+54n] | encoder `0x939750` (i16s clamped to +-0x7fff, batched from a start index); lossy sends at `0x95f84f` and `0x962a40` (both in `0x95e9d0`) | host body snapshots: f32[0], f32[2] = x, z in the step check "judge refused body N - stepped X units in Y ms from (..) to (..)"; `0x937c30` ownership result 1/2/3 counted; epoch 0 and body == sender's avatar = own-avatar report |
| 3 | `0x93bba0` (via `0x983ee0`) | combat pack, below | each list n <= 255 | encoder `0x93a750`, reliable at `0x971824` | log "a combat pack of N bytes ..." (cross-host fights) |
| 4 | `0x93ddd0` (via `0x9724a0`) | `u8 flags` (only bit0), `u16 fps10`, `u16 worstMs`, `u16 busy`, `u16`, `u16`; total exactly 12 B | fps10 <= 10000, busy <= 1000 | encoder `0x93dc90`, reliable at `0x969731` | "<p> reports as a G4 client (<fps10/10> fps, worst frame <worstMs> ms, main thread busy <busy/10>%) - willing to host / will NOT host"; bit0 = willing |
| 5 | inline `0x957220` | `u16 n`, n x {`u32 avatar`, `u8 m`, m x {`u8 stat`, `f32 gain`}}; no exact check | n <= 64, m <= 40; gain > 0, compared with 10.0 (`0xeb681c`) and further bounds; NaN/Inf rejected | inline (`mov r8b,5`), reliable at `0x96da91` | skill gains of avatars in the host's zone ("skill gain over the N-level bound refused - host # reports avatar A stat S ...", "unled skill gain") |
| 6 | `0x93e0d0` | `u8 n`, n x `u32`; end exact (2+4n) | n <= 64 | encoder `0x93dfb0`, reliable at `0x95e78a` | id list per player, timestamped (maps `0x196b338`, `0x196c170`); meaning unknown |
| 7 | inline | `u16 task`; payload exactly 3 B | | encoder `0x93e940`, reliable at `0x9a27e5` | "the guest acted (task N) - its body is its own until it clicks attack" (`0x9a24e0`) |
| 8 | `0x93e3d0` | `u8 n`, n x `u32`; end exact (2+4n) | n <= 64 | encoder `0x93e2b0`, reliable at `0x95e540` | id list stored in the player state (+0x98), flag +0xb8; meaning unknown |
| 9 | `0x93e5b0` | `u8 ver`, `u8 flags` (bits 0,1,2), `u16`, `u16`, `u32`, 5 x `u16`, `u32`, `u16`, `u8`, `u16`, `u16`, `u32` (36 B incl. op) | ver != 0; ver 1 -> length exactly 36; ver > 1 -> length >= 36 (extra ignored) | sender not found (candidate: reliable send at `0x95eab0` in `0x95e9d0`, buffer from `0x2097b0`, not decoded) | stored via `0x206520`; meaning unknown |

**Combat pack (op 3)**, decoder `0x93bba0`, order (list labels = failure strings, M; field meanings I from labels and logs "body N of P fights it across the hosts' border", "it fights body N of the fight of P - one fight, one engine"):
1. `u32 packId`
2. fights: `u16 n` (<= 255) x {`u32 a` (!= 0), `u32 b`, `u8 k` (<= 1), `u32 v`}; a bad item is dropped alone
3. anims: `u16 n` (<= 255) x {`u32 a` (!= 0), `u32 b`, `u8`, `u8` (<= 4), `u8` (<= 2), `u16`, `u16`, `str8<=0x4f`, `str8<=0x27`}
4. strikes: `u16 n` (<= 255) x {`u32 a` (!= 0), `u32 b` (!= 0, != a), `u32 seq` (!= 0), `u8 k` (<= 1), `u32`, 6 x `f32`, `u8` (<= 3), `u16`, `u32`, `u16`, `u16`, `u8` (<= 6), `u32`, `u32`} (61 B)
5. vitals: `u16 n` (<= 255) x {`u32 a` (!= 0), `u32 b` (!= 0), `str16<=0x400`}
6. gives: `u16 n` (<= 255) x {`u32 a` (!= 0), `u32 b` (!= 0), `u8 has` (<= 1), if has: {5 x `f32`, `u8`}}
7. wounds (only if bytes remain): `u16 n` (1..255) x {`u8 kind` (1..3), `u32 f1` (!= 0), `u32 f2`, `u32 f3`, `u8`, `u32`, `u32 f6` (!= 0), `u32`, 6 x `f32`, `u8` (<= 3), `u16`, `u32`, `u8` (<= 6), `u16`, `u16`, `u32`, `f32` in [0,1], `f32` in [0,600], `u8 m` (<= 8), m x {`str8<=0x40` (non-empty), `f32`, `f32` (each in [0,10000])}}; kind 2 -> f2 == 0 and f3 != 0, else f2 != 0 and f2 != f1
8. end exact (label "ok")
Strikes and wounds field order comes from a 6.8 KB inlined decoder with no writer cross-check (encoder `0x93a750` not read): medium confidence.

**Open.**
- Producer of the name-row bytes (map `0x196b548`) and the meaning of player/name strings.
- Sender of op 9; meanings of ops 6, 8, 9 fields.
- Remove slot `0x95b660`, slots 21 `0x95b880` and 24 `0x962bd0` not decoded; gather `0x9561f0` only cross-checked via writers `0x9388a0`, `0x93e9d0`, `0x938a70`, `0x938cf0`, `0x93d6a0`.
- Notice slot not decoded.

---

### squads (kind 33) - squad settings, member state and body records for rebuilding squads; 16 host intents
Vtable `0x126c7e8`, v4, period -. Status: **full** for rows with a decoded writer ('S', 'B', 'G', 'F', 'N', 'M' and the settings tags); 'C' and 'L' writers (prebuilt bytes) and remove/notice slots not traced.
Functions: gather `0xa678a0`, apply `0xa6a9a0`, remove `0xa6fa10` (not decoded), intent `0xa62370` (timing wrapper -> real handler `0xb315a0`), notice `0xa6f8f0` (not decoded), senders `0x547b00` / `0x547ce0` with kind 0x21 at the sites below. Upstream: none (slot 26 stub).

Layout order M; meanings I (from log strings). Local abbreviations: `pos` = `f32 x`, `f32 y`, `f32 z` (f32 reader `0x543bf0`, plausibility `0xa5e960`); `squadId,idx` = `u32 squadId`, `u8 memberIdx`.

**Rows (server -> client).** Key type 3 (name); key id = row-id hash `0xa3d590(squadId, sub, tag)` (FNV-1a32 over 12 bytes {u32 a, u32 b, u32 tag}, 0 -> 1). The apply does not test the key type: it reads the value prefix and tries tags in the order below until `hash(squadId, sub, tag) == key id`; fallback is 'C'. If the client is the host (`0x1942128 != 0`) the apply returns at once. Failing rows bump `0x194ce3c`. Every value ends with an "end exact" check.

Every value starts with: `u32 squadId`, `u8 sub` (member index; must be 0 for squad-level records). Special squadId `0x504f5055` "UPOP" = global settings records.

| tag | sub | value after prefix (wire order) | limits / effect |
|---|---|---|---|
| 'S' 0x53 | member idx | `u32 a`, `u32 b`, `f32 x`, `f32 y`, `f32 z`, `u8 flags` | pos plausible; stored under hash(squadId, sub, 'C') as 6 dwords. Writer `0xa629c0`: a = squad +0x1d4, b = squad +0x1d8 (compared as float elsewhere, maybe f32), xyz = member place (quantised), flags bits 1/2/4/8/0x10 from config flags. I: member live-place state |
| 'P' 0x50 | 0, UPOP | `u32 v`, `u16 n` | n < 0x41; stored `0x194c0ac` / `0x194c0a8` |
| 'Q' 0x51 | 0, UPOP | `u32 v`, `u16 n` | n < 0x41 or n == 0xffff (off); `0x194bfe4..e8` |
| 'T' 0x54 | 0, UPOP | `u8 v` | v < 3; `0x194c000` |
| 'Y' 0x59 | 0, UPOP | `u8 v` | v < 2; `0x194c128` |
| 'V' 0x56 | 0, UPOP | `u8 v` | v < 9; `0x194cb24` |
| 'U' 0x55 | 0, UPOP | `u16 count` (<= 0x800), count x {`str16<=0x80`, `u8 k`} | element kept only if k < 3 and string non-empty; whole list handed to `0x532790` |
| 'W' 0x57 | 0, UPOP | `f32 a`, `f32 b`, `f32 c`, `f32 d` | each in [0, 10] (`0xeb681c` = 10.0); -> `0x26d2f0` |
| 'B' 0x42 | 0 | `f32 x`, `f32 y`, `f32 z`, `str16<=0x80` | pos plausible; map `0x196c458`. Writer in `0xa678a0`: squad +0x43c..0x444, string +0x448, row hash cached. I: squad base/home place + name |
| 'G' 0x47 | 0 | `f32 x`, `f32 z` (second float unconfirmed), `str16<=0x80` (non-empty) | x checked by `0xa5e960`; map `0xa73980`. Writer: floats from squad +0x4ac / +0x4b0, string from `0xa2c720(squad+0x88)`. I: squad goal/destination label |
| 'F' 0x46 | 0 | `u8 mode`, `u32 v` | mode 1 or 2; map `0x196c748` keyed by squadId. Writer: squad +0x4b8 (u8), +0x4bc (u32) |
| 'N' 0x4e | 0 | 3 x `str16<=0x80`, `f32`, `f32`, `f32` (third written as u32 by the server, read via the f32 reader) | first float pos check; applied only if a pending entry for this key matches (`0x196c9e8`), -> `0xa2bd70`. Writer: gather tail (strings on squad +0xa8 / +0x58 / +0x80, floats +0x120 / +0x124 / +0x128) |
| 'H' 0x48 | member idx | `u8 count` (<= 8), count x {`u32`, `u32`, `u32`, 6 x `f32`, `u8`, `u16`, `u32`} (43 B each) | per element the first u32 is a monotonic counter (kept if > stored, reset if > stored + 100000); queued (`0xa748d0`, max 0x200). Same shape as the cellhost event. Writer: prebuilt bytes from map `0x196cc50`. I: per-member hit/event history |
| 'M' 0x4d | member idx | `str16<=0x1000` (non-empty) | stored under hash(.., 'C'); passed to `0xb32a60` on the live body. Writer `0xa6a5a0` (member +0x170 string) |
| 'L' 0x4c | member idx | `str16<=15000` | stored under hash(.., 'C') in `0x196d070`; compared with record +0xd8 string, triggers re-apply `0xa4c920` / `0xa4c800`. Writer: prebuilt per-member bytes (squad +0x170 vector, stride 0x28). I: member loadout/inventory blob |
| 'C' 0x43 (fallback) | member idx | body record, below | key must equal hash(squadId, sub, 'C') and `template` non-empty |

**'C' body record** (rebuilds a squad member), wire order:
1. `u8 r0`
2. `str16<=0x200` s1
3. `str16<=0x200` s2
4. `str16<=0x200` s3: if it starts with `"anarchy-cit|"` (12 chars, `0x1260ff8`) -> `0xb60250(squadId, s3)` (I: citizen/shop marker)
5. pos (plausible)
6. `str16<=0x200` name (I: logged as `squads: edge - "<name>"`, `squads: record body "<name>" of squad N`)
7. `str16<=0x200` template (must be non-empty); 8. `str16<=0x200` race; both go to `0x4d2670` (errors "unknown template", `template "..." is not a character`, `race "..." is not a race record`)
9. `f32 v9`
10. `str16<=12000` blob A; 11. `str16<=0x1000` blob B; 12. `str16<=0x1000` blob C. FNV-1a(A) chained with `0x5433c0` over B, C, template = change hash compared with record +0x54 (I: appearance/state blobs, rebuild when changed)
13. `u32 v13`
14. `str16<=0x200` s14
15. `u8 v15` (1 forces "rebuild allowed")
16. `str16<=0x200` s16 (checked by `0xa380d0` when v15 != 1)
Then end exact. Writer: prebuilt per-member string (squad +0x150 vector, stride 0x28); producer not traced.

**Intent (client -> server, kind 0x21, handler `0xb315a0`).** Payload after the 5-byte header starts with `u8 op`; unknown op -> counter `0x194c3f0`. All ints LE; "exact" = handler checks pos == len. Op byte is inserted at payload position 0 by the sender (M).

| op | handler | fields after op (wire order) | limits | meaning (I, from log strings) |
|---|---|---|---|---|
| 1 | inline `0xb315a0` | `u16 n`, n x {squadId,idx, `u32 v`, pos, `u8 flags`, `str16<=0x1000`} | n <= 0x200 and <= sender's squad size + 8; trailing bytes = malformed; string skipped (`0x543db0`) when cfg `0x15a5eae` | host's live state of record members (shape of row 'S' + text) -> `0xb2ac70` |
| 2 | `0xb2e640` | `u32 h`; list A: `u16 n` (<= 0x200) x {squadId,idx, `u32 target`, `u8 k` (<= 1), `u32 v`}; list B: `u16 n` (<= 0x200) x {`u32`, `u8`, `u32`, `u8`, `u8`, `u8`, `u16`, `u16`, `str16<=0x4f`, `str16<=0x27`}; list C: `u16 n` (<= 0x200) x {squadId,idx, `u32`, `u8`, `u32`, `u16`, `u16`, `u8 k` (<= 6)}; exact | | cross-host fights ("it attacks the avatar of host #", "fights it across the hosts' border", `cellhost brawl`) |
| 3 | `0x27c360` | `u32 body` (decompiler types it float), `u8 crime`, `str16<=0x200`; exact | crime in 2..31 with bit set in mask 0x43c (2, 3, 4, 5, 10), else "not a crime a host may claim" | host claims a crime against a player body (law faction text; "no witness", rate limit 30/min, 10 s repeat) |
| 4 | `0xae8400` | squadId,idx, `u32 v`; exact | cooldown 250 ms | `avattack`: player clicked attack on a record member ("too far", lease to a host) |
| 5 | `0xae6e90` | squadId,idx, `u32 a`, `u32 b`, 6 x `f32` (inline byte reads, same NaN/3e38 rule), `u8`, `u16`, `u32`; exact | | `guesthit`: a guest's hit on a record member (same shape as row 'H' element) |
| 6 | `0xab2370` | `u32 squadId`, `u8 a`, `u8 b`, 3 x `str16<=0x80`, 4 x `f32`, 4 x `str16<=0x1000`; exact | 0 < b <= 0x20, a < b | "g6 native": host reports a native squad born by template (members as record) |
| 7 | `0xb30430` | `u16 n` (<= 0x200) x {squadId,idx, `u32 handle`, `u16 heading` (angle = v/65535*2pi - pi), `u8 has`, if has: {`u8`, `u8`, 4 x (`u16` - 0x8000), `u8`, `u8`}} | no exact check | member facing/motion state (lossy) |
| 8 / 11 | `0xab53f0` (flag 0 / 1 = "foreign") | `u32 squadId`, `u8 a`, `u8 b`, 3 x `str16<=0x80`, pos, 3 x `str16<=0x80`, 4 x `f32`, 4 x `str16<=0x1000`; exact | 0 < b <= 0x20, b > stored count | "population": host offers a squad (nest/island) for a zone it holds |
| 9 | `0xb2fa90` | `u16 n` (<= 0x40) x {squadId,idx, `u32`, `u32`, pos, `u16`, `u16`, `u16`, `u8`, `u8`} | pos plausible (`0xa5e960`) | member movement/place report (lossy) |
| 10 | `0xac1a10` | `u32 squadId`, `u8 a`, `u8 b`, 3 x `str16<=0x80`, pos, `str16<=0x80`, `u8 c`, 3 x `str16<=0x80`, 4 x `f32`, 4 x `str16<=0x1000`, optional tail if bytes remain {pos, `str16<=0x80`}; exact | 0 < b <= 0x20, a < b, c < 2, a string non-empty | "townpop": host offers a town squad (strings checked for prefix "anarchy-p") |
| 12 | `0xabe320` | `str16<=0x80`, pos, 3 x `str16<=0x80`, `u8`, `u8` (skipped), pos; exact | both pos plausible; strings 1, 3, 4 non-empty and < 0x80; string 3 must be a gamedata record (`0x4d2590`) | "adopt": a host asks to adopt a squad ("no town", "no foreign", "no full") |
| 13 | `0xac1060` | `u32 squadId`, `u8 flags` (bits 1, 2), `str16<=0x80`, `str16<=0x80` (must equal squad name at +0xa8 if non-empty); exact | squad state 3 and its host == sender | host's engine declared the squad dead ("its leader died") |
| 14 | `0xb30ad0` | `u16 n` (<= 0x200) x {squadId,idx, `u32 handle`, `u8 mode` (< 2), pos, `u16 v`} | mode 1: must lie within 60 units (dist^2 <= 3600) of the body | member mode/place marker |
| 15 | `0xb31100` | `u16 n` (<= 16) x {squadId,idx, `u32 handle`, `u8` (skipped), `str16<=0xd0`} | token bucket (20) per player | member speech/text line -> `0x296390` |
| 16 | `0xae4ab0` | squadId,idx, `u32 v`, `u8 flag`; exact | cooldown 250 ms | `aidtake`: first aid on a record member ("the medic holds the zone") |

Senders (kind 0x21, `0x547b00` reliable / `0x547ce0` lossy; M): op 1 `0xb334f0`; op 2 `0xb39200`; op 3 `0x27bcb0`; op 5 `0xb3cbf0`; ops 6 / 8 / 10 `0xa1c1d0` (chooses 6, 8 or 10); op 7 `0xb39200` (lossy); op 9 `0xb36d90` (lossy); op 10 also `0xa54c90`; op 12 `0xa335b0`; op 13 `0xa85cd0`; op 14 `0xb38340`; op 15 `0xb38a00`. Most ops are sent only from a cell host (checks "host #", g6 switch `0x194be62`).

**Open.**
- 'C' and 'L' row bytes are prebuilt elsewhere (squad +0x150 / +0x170 vectors); producer not traced, so writer cross-check exists only for 'S', 'B', 'G', 'F', 'N', 'M'.
- Remove slot `0xa6fa10` and notice slot `0xa6f8f0` not decoded.
- No sender found for ops 4, 11, 16.

### base (kind 39) - authored zones, machine rows, lead rows, guest orders, hands, claims
Vtable `0x11c0a18`, v2, period 1.0 s. Status: **partial** (inner machine-config blob layout, guest-order executor and several consumers not decoded).
Functions: gather `0xf0290` (5533 B; global rows ids 1, 2, 4, 8 and machine rows), gather lead part `0xef670` (global ids 5, 6, 7 and lead rows), apply `0xf39e0` (5654 B), remove `0xf5360`, intent `0xf1830` (8613 B), notice `0xf6050`, client tick (slot 19) `0xf5bd0`, senders `0x547b00` (kind 0x27) from `0xea1d0`, `0xea560`, `0xeb5e0`, `0xec570`, `0xee420`, `0xf5bd0`, `0x104440`.
Gating on the server side (M, gather): the whole topic only when `0x15a6cdc != 0`; rows marked [lead] only for a viewer in the lead set `0x195d968` and `0x15a6d3c != 0` (`0xef670` prologue). Primitives used: key reader `0x543f90` (0x18 B: `u8 type`, then `u32 id` or for type 2 `u32 a,b,c,d`), key writer `0x543aa0`, raw-bytes reader `0x543e40(reader, out, n)` (fails if pos+n > len), f32 reader `0x543bf0` (NaN or beyond +-3.0e38 clears `ok`), place key builder `0x543430(x,y,z,d)`.

**Rows (server -> client).** Apply `0xf39e0(this, key*, reader*)` returns 0 ok / 2 bad (M: epilogue `mov eax,2` at `0xf4fbf`, `xor eax,eax` at `0xf3c38`). Dispatch on the key:
- **key type 5 (global), id in {1,2,4,5,6,7,8}** -> global rows. Other type-5 ids (3, 9, ...) and all other types except 2 -> return 2.
- **key type 2 (place) with `d` (key+0x14) == `0x424c4544` "BLED"** (bytes `44 45 4c 42`) -> lead row.
- **key type 2 with `d` == `0x424d4143` "BMAC"** (bytes `43 41 4d 42`) -> machine row. Any other `d` -> 2.
For a place key `d` is a tag, not a building ordinal: both gather sides build the key with `0x543430(x,y,z, 0x424d4143)` (call at `0xdaf10`; that the machine map `0x195e020` is filled there is I) and the lead row key is the machine's stored key copied with `+0x14 = 0x424c4544` (`0xef670`). The tag names "lead" / "machine" are a reading of the ASCII (I).
All rows: any failed read or non-exact end -> return 2, nothing applied (stores happen only after the check).

*Global rows (key type 5, id).* Value fields in wire order; all end exact.

| id | value | limits | apply effect (M) | gather source (M) |
|---|---|---|---|---|
| 1 | u8 `n`; n x u32 `zone` | n <= 0x20 (`n > 0x20` -> bad) | replaces the "zones I author" list (`0xe8ec0`: vector `0x195dec8`, count `0x1943cbc`, under CS `0x1666000`); logs `base: this client is the author of zone(s) [..] - machines there run here and report to the server, buildings there grow here` (strings `0x11bdca0`/`0x11bdc50`, first 0x3c changes) | `0xf0290`: zones whose leader (map `0x196be28`, zone -> player id) == viewer id, or (setting `0x15a6d2c` on and viewer id != 0) the viewer's own zone vector at `this+0x40`; max 0x20 |
| 2 | u8 `n`; n x u32 `zone` | n <= 0x20 | replaces the "zones near me run by another host" list (`0xe95c0`: `0x195de88`, count `0x1943cc0`); log `base: zone(s) [..] near me are run by another host - my own builders there grow the building here and report to the server` (`0x11bdd40`/`0x11bdcd0`) | `0xf0290`: zones in the 3x3 cell block around the viewer (cell = floor(coord / `0xebe4bc`) + 0x20; zone u32 = `(cz<<16) \| (cx & 0xffff)`) that have a leader other than the viewer (setting `0x15a6cde`); max 0x20 |
| 4 | u8 `n`; n x { u32 `id`; f32 `x`; f32 `y`; f32 `z`; i8 `a` (signed byte, stored as int); u8 `op`; u8 `c` (stored as bool `c != 0`); str16<=0x40 `s1`; str16<=0x40 `s2` } | n <= 0x10 (`n > 0x10` -> bad); no range / name checks on the fields | replaces the guest order list (vector `0x195d8c8`, counter `0x195d8b0 += n`), then `0xe99d0` runs each order (below) | `0xf0290` (setting `0x15a6d34`): records of the order list `0x195d9b0` (0xb0 B each) with `rec+0xc == viewer id`, max 0x10; fields from rec: +0 u32, +0x18/+0x1c/+0x20 f32, +0x24 u8, +0x28 u8 (`op`), then `op == 3 ? min(u32 @+0x30, 0xff) : (bool @+0x2c)` as u8, str16 @+0x38, str16 @+0x60 |
| 5 | u8 `leadMode` | | `0x1943cc4 = (leadMode != 0)` (interlocked); on switching 0 -> 1 logs `base: the server leads machines by their leads - a machine runs here only when I lead it` (`0x11be1a0`); `0x15a6d60 = 0x12c84a8` | `0xef670` [lead]: always `u8 1` (value length 1) |
| 6 | u8 `n`; n x { u32 `id`; key (0x18 B, **type must be 2**); u8 `kind` (must be 1 or 2); u16 `len` (<= 6000 = 0x1770); len bytes `blob` } | n <= 0x10; the blob must parse with `0xbb9070` (machine-config text, see Blob) else bad | each entry (0x240 B object: id, key, kind, parsed record, timestamp) is added to the pending list `0x195dda8` unless an entry with the same `id` is already there or in the done map `0x195de48` (`0x1091a0` / linear scan); then `0xedf40` tries to apply them to the world machines | `0xef670` [lead]: records of `0x195dd00` (0x58 B each: u32 id @0, key @4, owner id @0x1c, u8 @0x28, blob object @0x30 serialised by `0xbb8710(...,0,0)`), `owner == viewer`, max 0x10 (loop `0xf < count`), blob len <= 6000 |
| 7 | u8 `n`; n x { key (0x18 B, no type test); u32 `v`; f32 `w` } | n <= 0x20 | swapped into global vector `0x195db98` (0x20 B elements: key 0x18 + u32 + f32; consumer list for slot 21 `0xf5730`, not read) | `0xef670` [lead] (setting `0x15a6d3b`): for each machine record (`0xd7ed0`) of the viewer with state `+0x398 == 1` and kind `+0x30` in 1..4, for each other player entry in its map (`+0x428`) and each inner entry seen within the last 3000 ms (GetTickCount64 diff < 0xbb9) with float > 0: `key; u32 (inner key); f32 (inner value)`; max 32 |
| 8 | u8 `n`; n x u32 `zone` | n <= 0x20 | stored (under CS `0x1666000`) in `0x195dbd8`, count `0x1943cdc` | `0xf0290` (settings `0x15a6d3c`, `0x15a6d3b`, `0x15a6d3f` all on, viewer in lead set): zones from the id-1 and id-2 lists that map (`0x9a5e40`) to a non-zero id present in set `0x195da50`, max 32 |

Guest order consumer `0xe99d0` (M): for each order in `0x195d8c8` not yet seen (map `0x195d908` keyed by order id) calls `0x64c620(&order+8, &text)` (<0 failed, 0 refused, >0 done); log `base: a guest's order #<id> (op <op>) for the machine at (<x>, <z>) - done|refused|...`. So id 4 = orders sent by "guests" for a machine, forwarded by the server to the leading client (I, from the log text). `op` = byte 6 of the element (stored at element+0x18).

*Lead row (key type 2, `d` = `0x424c4544` "BLED"; key words = the machine's place key; apply branch `0xf4c5d..0xf4dda`, M).* Value: u32 `a`; u32 `b`; u8 `c`; i8 `d` (read as u8, sign-extended); end exact.

| field | type | meaning (I) |
|---|---|---|
| `a` | u32 | lead / session id (compared with `*param_2` in `0xebff0`) |
| `b` | u32 | count compared with `*(u32*)(player+0x48c)` |
| `c` | u8 | kind |
| `d` | i8 | variant / ordinal, passed as the `d` argument to `0x72c030` |

Effect (M): looks up / creates the entry for the same place key with `d` replaced by `0x424d4143` (map `0x195d898`, `0x1093c0`). If `a` equals the stored first dword only the "dirty" byte at +0x20 is cleared; otherwise the entry is reset and set to `{a, b, c, d (default -1)}` plus timestamp (`0xd60f0`, double at +0x18), counter `0x195dad0 += 1`, and when the old entry was marked (+0x10 != 0) the led-machine position list is rebuilt (`0xea680` -> `0x195df28`, count `0x1943cd0`). If the entry's mark byte (+0x10) is 0 the lead reaction `0xebff0(key, entry)` runs (look up the machine at the place with `0x424640`; if none, try to raise it with `0x72c030(x,y,z, d)` when `0x15a6d52`...; logs; counters `0x195dad4` / `0x195dad8`; returns the code the apply returns).
Gather (`0xef670` [lead]): for every machine key in the viewer's list (map `0x195da78`, viewer id -> vector of 0x18 B keys) whose machine record (`0xd7ed0(key)`) has `+0x398 == 1` and `+0x390 == viewer id`: key copy with `+0x14 = 0x424c4544`, value `u32 @rec+0x394; u32 @rec+0x3b4; u8 @rec+0x30; u8 @rec+0x34` (M, writer order = apply order). `rec+0x30` is 1..4 elsewhere in the same function (I: kind, same numbering as the machine topic's `kind`); `rec+0x34` plausibly the machine topic's `b1` (0..3), I. Meaning: the server tells a client "you lead this machine" (I).

*Machine row (key type 2, `d` = `0x424d4143` "BMAC"; apply branch `0xf4ddf..0xf4f27`, reader `0xeb410`, M).* Value: u8 `a`; i8 `b`; f32 `c`; u16 `len`; len bytes `blob`; u32 `e`; end exact.

| field | type | limits | meaning (I) |
|---|---|---|---|
| `a` | u8 | | mode / power-like number, unnamed (gather: `node+0x60`) |
| `b` | i8 | | unnamed (gather: `node+0x64`; the server compares it with report field `a`) |
| `c` | f32 | -3.0e38 <= c <= 3.0e38 (consts `0xeb5e6c` / `0x12c1bd8`; else `ok` cleared and `c = 0`) | unnamed (gather: `node+0x74`, NaN -> 0) |
| `len` | u16 | <= 6000, else bad | blob length |
| `blob` | bytes | parsed by `0xbb9070` into a 0x220 B object, parse failure -> bad | machine config (see Blob) |
| `e` | u32 | | revision / stamp (gather: `node+0x70` or the variant's own stamp) |

Effect (M, `0xf4e28..0xf4f27`): the record is stored in the map entry for the key (`0x108ca0`): `{a @+0, b @+4, c @+8, parsed blob @+0x10, dirty byte @+0x228, e @+0x45c, applied-stamp @+0x460}`. Then, with `lead = 0x1943cc4`:
1. `lead != 0` and `0xeab10(key)` true (lead entry for the same key exists in `0x195d898` with mark byte value+0x10 != 0, set by `0xebff0` on success; I: "I adopted this machine") -> counter `0x195db0c++`, row dropped.
2. `lead == 0`, or no lead entry for the key: if NOT (`0xeab70(key)` && dirty == 0 && applied-stamp == `e`) -> `r = 0xeb3a0(key, entry)`; when `r == 0` the applied-stamp is set to `e` and a set dirty byte is cleared (counter `0x195dc20`). `0xeab70` = (lead on: mark != 0; lead off: the key's zone (cell of x,z * 0.5 / `0xebe4bc` + 0x20) is in my authored-zones list `0x195dec8`). So the row is skipped only for a zone I author whose stamp I already applied.
3. `lead != 0` and a lead entry exists with mark 0 -> lead reaction `0xebff0` (try to adopt); its result is the apply's return.
`0xeb3a0(key, entry)` (M): `0xbeec70(x,z)` true (zone busy) -> counter `0x195dc3c`, return 1; machine object at (x,y,z) via `0x424640` (key words * 0.5) missing -> counter `0x195dc24`, return 1; else `0xbb7580(obj, entry+0x10, 1)` (apply the record to the machine object): true -> `0xeac40(key)`, return 1; false -> counter `0x195dc1c`, return 0 (polarity of `0xbb7580` not checked; I: it reports "changed / needs more").
Gather (`0xf0290`, value built at asm `0xf0a53..`): for each machine node of the zones in the id-1 and id-2 lists (map `0x195e020`, key at node+0x18, key tag `0x424d4143`): `u8 node[+0x60]; u8 node[+0x64]; f32 node[+0x74] (NaN -> 0); u16 len; blob bytes; u32 e`, where the blob = the machine's config serialised by `0xbb8710` (`0xe23f0` chooses the personal or shared variant; blob length > 6000 -> row skipped) and `e` = `node[+0x70]` or the variant's own stamp. With setting `0x15a6d0f` set the value comes from the cache entry returned by `0xeef80` (`0xd5d00(out, key, cacheValue, hash)`), same bytes.

*Blob (machine config text, shared by global id 6, the machine row, and report ops 1, 3, 5).* Serialiser `0xbb8710` (2399 B) writes: byte `version` (7, 8, 9 or 10: 7/8, +2 if a flag), byte `mask` (`this+0x20`: 4 slots x 2 bits, slot used when its field == 2), per used slot a record (`0xba7140`; plus a byte and two more records when version >= 9), then a count byte `min(vector size >> 8, 0x18 or 0xf0)` and entries of 0x100 B. Parser `0xbb9070` (2266 B) reads the same. Only this container is decoded; the inner record layout (`0xba7140`, the 0x100 B entries) is not.

**Intent (client -> server).** Handler `0xf1830(this, player*, reader*)` (M). Payload after the 5-byte header: `u8 op`. Reader not ok before the op, or op not in 1..5 -> flag 0 "unknown request" (`0x11be190`). Failure counter `0x195df98`. Every reply is the notice `0x547e50(player, kind 39, code = global 0x194830c (I: the intent's client seq, as in construct), flag, text)`. Key checks use the key reader (0x18 B).

| op | fields (wire order, types, limits) | effect / reply |
|---|---|---|
| 1 host report (I: zone host reports a machine it runs) | key; u32 `a`; u8 `ops`; f32 `x` (read and discarded: result of the call at `0xf1945` is overwritten by the next read); f32 `charge`; u16 `len` (<= 6000); len bytes `blob`; u32 `rev`. Key type 2 and tag BMAC (`0x424d4143`); end exact; blob parses (`0xbb9070`); counter `0x195df80` | `0xdf2a0(player, key, a, ops, charge, blob, rev, &text)` -> bool. Checks (strings M): no record ("no record of that machine (the server never saw it standing)"), zone is the server's, not the host of the zone (zone `0x9a5e40` -> player), machine has a lead ("only its lead reports it"), `a` must equal node+0x64, `rev` must equal the record revision ("stale report (the record is at revision N, the report was made from M)"), produced / used amounts vs a rate ceiling ("eff x rate x ops"), kind 5 charge vs battery capacity; on accept `charge` -> node+0x7c, text "accepted rev=<n>". False -> log `base: report of player P about <key> refused - <text>` (first 40, then every 100th). Parse fail: flag 0 "bad report" (`0x11be040`); else flag = result, text from `0xdf2a0` |
| 2 answer (I: zone host answers guest orders) | u8 `n` (<= 0x10, loop stops at 16); n x { u32 `orderId`; u8 `done` (bool); str16<=0x40 `text` }; end exact | stamps the player in map `0x195dce0` (GetTickCount64); n == 0 is a keep-alive (counter `0x195dd84`). Per id: finds the order record (0xb0 B, list `0x195d9b0`); if `rec+0xc == sender`: removes it and sends the guest a notice `0x547e50(rec+0x10 guest player, kind 6 (machine topic), rec+0x14 guest's code, done, "the zone host did it: " / "the zone host refused: " + text)`; record of another host -> counter `0x195dd80`. Bad: flag 0 "bad answer" (`0x11be068`); else flag 1 "ok" (`0x11aca00`) |
| 3 lead report (I: the client that leads a machine reports it) | key; u32 `a`; u8 `ops`; f32 `c`; f32 `charge`; u16 `len` (<= 6000); len bytes `blob`; u32 `epoch`; u32 `handSeq`; u8 `flags`. Key type 2, tag BMAC; end exact; blob parses | `0xe7800(player, key, a, ops, c, charge, blob, epoch, handSeq, flags, &text)` -> bool. Checks (strings M): node+0x3c8 != 0, leader node+0x3c0 == sender and node+0x3c4 == `epoch` else "not your machine to lead (epoch N)"; `a` == node+0x64; flags bit1 -> "released" (`0xe4ae0` drops the lead); hand entries (node+0x3f0, 0x108 B) with stamp <= `handSeq` counted as done; `c` -> rec+0x44 (= node+0x74, the machine row's f32) if c >= 0.0; `charge` -> rec+0x4c (node+0x7c); flags bit0 with state 2 -> "its lead let it go (last report)"; text "lead report accepted rev=<n>". False -> log `base: lead report of player P about <key> refused - <text>`. Parse fail: flag 0 "bad lead report" (`0x11be0c0`); else flag = result |
| 4 claims (I: my characters operate this machine) | u8 `n` (<= 0x10); n x { key; u32 `charId`; f32 `v`; f32 `w` }; end exact (checked after the loop: entries before a failure were already applied) | stamps map `0x195d9d0`. An entry is accepted when: machine record exists (`0xd7ed0`) and `0xd80a0` ok, `charId` is in the sender's character list (player+0x18 vector), the character (`0x331950`) has a position (`0x561120`) within sqrt(62500) = 250 units (x/z) of the machine (rec floats +0x38/+0x40). Stored in the machine's per-player map (rec+0x420): `w` (NaN -> 0) and per `charId` `v` clamped to [0, 3.0] and rounded to 0.01 (`(int)(v*100+0.5)/100`) with a timestamp; entries older than 10000 ms pruned; first-seen stamp reset after an 8000 ms gap. I: these are what global row id 7 sends to the machine's leader. Bad: flag 0 "bad claim" (`0x11be0a8`); else flag 1 "claims <accepted>/<n>"; counters `0x195db70` += accepted, `0x195db74` += rejected |
| 5 hand answer (I: zone host answers a hand from global row 6) | u8 `n` (<= 0x10); n x { u32 `handId`; u16 `len` (<= 6000); len bytes `blob` }; blob parses when len != 0; end exact; parsed count == n | per id: hand record in list `0x195dd00` (0x58 B, owner +0x1c == sender): the answer's 0x100 B entries are pushed (max 0x100) to the machine record's list rec+0x3e0 with sign -1 if hand kind (+0x28) == 1 else +1; if kind == 1 `0xe2830`; compares item totals (u16 @+0xf6 per entry, 0 counts as 1): answer < hand -> counter `0x195db54`; then `0xe4840(index, 1, answer)` closes the hand. Id found instead in the late map `0x195dd28` (owner == sender) with a non-empty answer: if the machine is no longer led by the sender -> log `base: a late hand answer of player P moved things after the hand was refused and the lead is gone - the judge records it` and judge record `0x2325f0(player, "lead-late", node+0x30, 0)`; else `0x103be0(machine+0x470, 0, kind==1 ? 2 : 1, answer)`. Bad: flag 0 "bad hand answer" (`0x11be0f0`); else flag 1 "ok" |

Senders (all `0x547b00` reliable, kind 0x27, op byte first via `0xd940(str, 0, op)`; M, meanings I):

| sender | op | writes | when |
|---|---|---|---|
| `0xea1d0` (from slot 19) | 2 | u8 n = min(queue, 16); per entry: u32 id; u8 bool; u16 len = min(strlen, 0x40); bytes | answer queue `0x195dff8` (0x30 B: u32 id, bool @+4, std::string @+8), filled by `0x109280` (callee of `0xe99d0`); sent entries removed; seq kept in set `0x195dc80` |
| `0xea560` (from slot 19) | 2 | u8 0 (empty answer = keep-alive) | every 30.0 s (`0x1076ec0`) unless `0x1943c8a` is set (set by the notice handler when the server replied "unknown request"); runs when I author zones (`0x1943cbc != 0`) or lead entries exist; seq in `0x195dc80` |
| `0xeb5e0` (from slot 19, every 0.5 s when lead mode off) | 1 | key (node+0x18 of my machine map `0x195dab8`); u32 node+0x34 (place ordinal, also the `d` of `0x72c030`); u8 ops = clamp(state+0x18, 0, 255); f32 state+0x20 (NaN -> 0); f32 state+0x1d0 (NaN -> 0); u16 len; blob (`0xbb8710`, <= 6000 else skipped); u32 rev = node+0x490 | machines where `0xeab70` is true (zones I author); state = `0x72b730(machine 0x72c030(x,y,z,d))`; rate: >= 2.0 s since last send, >= 5.0 s if an answer is pending (node+0x494 != 0), and the value from `0xbabe30` changed or >= 20.0 s passed. seq -> node+0x494 and map `0x195df48` (seq -> key, max 0x100) |
| `0xec570` (from `0xecc50`, every 0.25 s when lead mode on) | 3 | key; u32 `a` (place ordinal); u8 ops (state+0x18 clamped); f32 state+0x20; f32 state+0x1d0; u16 len; blob (<= 6000); u32 epoch; u32 `handSeq` (global `0x1953ef0`, read under CS `0x18cd330`); u8 flags (`\|2` when the machine object is gone and cannot be raised) | `0xecc50` calls it with flags 1 for released leads (list `0x195dbf8`, 0x28 B: key, epoch @+0x18, ordinal @+0x1c, time @+0x20; retried until sent or 5.0 s) and leads given up after 2.0 s / zone not busy, flags 0 for regular reports (>= 5.0 s apart); seq in set `0x195dbb8` (max 0x200) |
| `0xee420` (from slot 19, lead mode on) | 5 | u8 n = min(queue, 16); per entry: u32 id; u16 len (0 if serialising fails or > 6000); blob (`0xbb8710`) | queue `0x195de08` (0x220 B: u32 id, blob object @+8), filled by `0x109fc0` (callee of the hand processor `0xedf40`); no seq kept |
| `0xf5bd0` (slot 19) | 4 | u8 0 (empty claims = keep-alive) | lead mode on, every 30.0 s (`0x15a6d60` = last send; the lead row resets it) |
| `0x104440` (from machine topic slot 19 `0x6345a0`) | 4 | u8 n; per claim: key (my machine within 2.0 units in x/z, `0xeb99bc` = 4.0 squared, same ordinal unless either is < 0); u32 charId (input+0x10); f32 input+0x14 (NaN -> 0); f32 input+0x18 (NaN -> 0) | input = machine topic list of 0x1c B entries {f32 x,y,z; i32 ordinal; u32 charId; f32; f32}; only claims with a matching machine counted; no cap at 16 in the writer (server rejects n > 16 as "bad claim") |

Field meanings (I): `a` (ops 1/3) = place ordinal (writer takes node+0x34, the same value passed as `d` to `0x72c030`; the server compares it with node+0x64 = the machine row's i8 `b`). `ops` = number of operators (multiplier in `0xd81a0` next to the "eff x rate x ops" ceiling). f32 #1 (state+0x20) is stored by op 3 at node+0x74 (= the machine row's `c`); op 1 drops it. f32 #2 (state+0x1d0) = `charge` (kind 5 battery-capacity check), stored at node+0x7c. `rev` / `epoch` / `handSeq` names come from the refusal texts.

**Notices (slot 20 `0xf6050(this, code, flag, text)`, M).**
- code in set `0x195dbb8` (lead reports, op 3): flag 1 -> counter `0x195dae8`; flag 0 -> counter `0x195daec`, log `base: my lead report was refused - <text>` (first 20).
- else code in set `0x195dc80` (op 2): flag 0 and text exactly "unknown request" -> `0x1943c8a = 1` (server has no op 2: stop answer keep-alives) and clear the answer queue.
- else code in map `0x195df48` (host reports, op 1): entry removed; the machine node's pending code (node+0x494) cleared if equal. Flag 1: counter `0x195dc2c`, parses the number after "rev=" in the text (strtoul) into node+0x490 (next report's `rev`). Flag 0: counter `0x195dc30`, node+0x258 = 1, `0xeb300` rolls the machine back to the stored record (on success revision = node+0x48c, counter `0x195dc20`), node+0x488 = 0, log `base: the server refused our report about <key> - <text> (the machine goes back to the record)` (first 40).
- Notices to ops 4 and 5 are not tracked (no seq kept).

**Remove (slot 15 `0xf5360(this, key*)`, M).**
- type 5 id 1 / 2 / 8: replaces the list with an empty one (`0xe8ec0` / `0xe95c0` / `0xea8e0`). id 4: clears the guest order vector `0x195d8c8`. id 5: lead mode off (`0x1943cc4 = 0`), every lead entry with mark (+0x40) set is queued as released (`0x109990` -> `0x195dbf8`: key, ordinal +0x3c, time `0xd60f0`) so `0xecc50` sends its last report (op 3 flags 1), then `0x109820` clears the lead map and `0xea680` rebuilds positions. id 6: nothing. id 7: clears `0x195db98`. Other type-5 ids fall through to `0x108e20(key)`.
- type 2 tag BMAC: `0xe9120(x*0.5, z*0.5, 0)`, then `0x109900(key)` (erase; non-zero = done) else `0x108e20(key)`.
- type 2 tag BLED: lead entry (same place, tag BMAC) in `0x195d898`: mark 0 -> entry erased (`0x1094d0`); marked and not yet released (+0x50 == 0) -> +0x50 = 1, +0x58 = time, `0x64c590(x,y,z, ordinal +0x3c)` (calls the machine module object `0x1988cb0` via `0x644030`; I: the machine stops running here and becomes a mirror; log in `0xecc50` "I no longer lead the machine at (..) - its last report went, it is a mirror here again").
- other types: `0x108e20(key)`. Which maps `0x108e20` / `0x109900` erase from: not checked.

**Who authors what (I).** Server rows: authored zones (1), neighbour zones (2), guest orders (4), lead mode (5), hands (6), claims of others (7), list 8, lead rows (BLED), machine rows (BMAC). Client intents: a zone host sends host reports (op 1) and order answers (op 2); a machine's leader sends lead reports (op 3) and hand answers (op 5); any client sends claims (op 4) for its characters at machines. Round trips: guest command (machine topic, kind 6) -> server order list -> row id 4 to the zone host -> `0xe99d0` runs it -> op 2 answer -> server notice kind 6 to the guest. Hand: row id 6 to the leader -> `0xedf40` -> op 5 answer. Report: op 1 -> notice with "rev=" -> next report's `rev`.
Map-node offsets (server machine map `0x195e020`): lead fields `+0x3c0` leader id, `+0x3c4` epoch, `+0x3c8` led flag; the record object starts at node+0x30 (e.g. `0xe7800`'s `rec+0x44` = node+0x74).

**Open.**
- Inner blob layout (`0xba7140`, 0x100 B entries; u16 count at +0xf6 seen).
- Guest order executor `0x64c620` (meaning of the order `op` codes); machine-module side `0x64c590` / `0x644030`.
- Slot 21 `0xf5730` (consumer of id-7 lists, also touches the answer / hand queues); slots 16, 18, 23, 24, 32, 33 not read.
- State struct of `0x72b730` (what +0x18, +0x20, +0x1d0 are); full checks inside `0xdf2a0` / `0xe7800` (only string-bearing branches read); global `0x1953ef0`; maps erased by `0x108e20` / `0x109900`.
- The setting flags by name (`0x15a6cdc`, `0x15a6cde`, `0x15a6d0f`, `0x15a6d2c`, `0x15a6d34`, `0x15a6d3b`, `0x15a6d3c`, `0x15a6d3f`).

### corpses (kind 40) - corpse, ground-item, carry, tomb, zone and drop records
Vtable `0x11c7eb8`, v1, period 1 s. Status: **full** (Kenshi-side effects of put-down and disposal not decoded).
Functions: gather `0x190390` (per-body writer `0x190080`), apply `0x191670` (tag-1 parser `0x18dde0`), remove `0x192110`, intent `0x1914d0` -> handler `0x17faa0`, sender `0x19cf90` (call `0x19d0fa`, kind 0x28, reliable `0x547b00`), value writers `0x170d70` (tags 1, 2) and `0x171350` (tag 3). Slot 26 stub. The client apply does nothing when the host handle `0x1942128 != 0`. The server gather runs only if `0x1943e89` and the viewer record is ok.

**Rows (server -> client).** Apply does not check the key type; the key id (key+4) indexes a client bookkeeping map `0x195e208` (key id -> tag, record id) used by remove. The server always uses key type 3 with `id = FNV-1a32 over the 4 LE bytes of the record id, started with (byte0 xor salt)`, other key words 0. Value = `u8 tag` + body; salt per tag below. Floats are u32 reinterpreted, range ±3.0e38 (else ok cleared). Writer positions are quantised `floor(v*2.0+0.5)*0.5` (consts `0xeb553c` = 2.0, `0xeba8bc` = 0.5).
| tag | salt | fields (wire order after the tag) | notes |
|---|---|---|---|
| 1 corpse record | `0x636f7270 "corp"` | u32 id; u8 flag (record `+4`); 5 x str16<=0x200 (S1 non-empty, S2..S5 may be empty); f32 F0 (record `+0x1c`, raw bits); f32 x, f32 y, f32 z (quantised; from record `+0x10/+0x14/+0x18`, or `+0x204/+0x208/+0x20c` when record byte `+0x200` is set = being carried, carrier's position); str16<=12000 (`+0x148`); str16<=0x1000 (`+0x170`); u32 H; end exact | parser `0x18dde0`, writer `0x170d70` (cached in record `+0x68`, hash `+0x1f0`). H = FNV-1a32 of the look string (record `+0x120`, size `+0x130`; 0 if empty), stored at `+0x15c`; the client derives a hash at `+0x158` (FNV-1a over S1 and the 4 floats' bytes mixed with the hash of the 12000-string). F0 and the two long strings: meaning not decoded |
| 2 look text | `0x6c6f6f6b "look"` | u32 id; str16<=15000; end exact | written only when the look string is non-empty (cached `+0x1c8`, hash `+0x1f4`); H on tag 1 lets the client spot a stale tag 2 (I) |
| 3 ground record | `0x67726e64 "grnd"` | u32 id; f32 x, y, z (quantised, from element `+0x10/+0x14/+0x18`); str16<=12000 (element `+0x20`); end exact | writer `0x171350` (cached element `+0x80`, hash `+0xa8`). Client parses the string with `0x16cc30` into a record; parse failure resets three containers and is tolerated. Server emits it for every element of map `0x195e290` within `[0x15a6c74]` = 2500.0 units (x/z, squared compare), or via spatial vector `0x195e368` when the index is complete. Items / body parts on the ground (I) |
| 4 carry | `0x63617272 "carr"` | u32 id; u32 value; end exact | stored in map `0x195e3c8`; server value = id and body id as 2 x u16 each, prebuilt by the carry code in `0x17faa0` (entry `+0x58`, hash `+0x70`), from map `0x195e1f0` |
| 5 tomb | `0x746f6d62 "tomb"` | u32 id; u32 x, y, z (raw, no client range check); str16<=0x40 non-empty; end exact | client keeps position + name and inserts into a grid keyed by `floor(coord * [0x12c1cc8])`. Server: tombs within viewer distance, at most 0x100 per gather, honouring an expiry double at element `+0x40` |
| 6 zone | `0x7a6f6e65 "zone"` | u32 id; u8 flag; end exact | flag = result of `0x221510` on the zone |
| 7 drop | `0x64726f70 "drop"` | u32 id; u32 value; end exact | stored in map `0x195e1c0`, notifies `0x18d920` |
Per-record emit order in `0x190080`: if record byte `+0x200` is set and a carry entry exists in `0x195e1f0` with a prebuilt string, the tag-4 row first; then tag 1; then tag 2 when the look text exists. The gather also sends corpse records of bodies within `[0x15a6c74]` squared of the viewer (x/z) or of bodies the viewer tracks, tombs, zones in a 3 x 3 cell neighbourhood, drops and carry rows for the viewer.
Remove (`0x192110`): looks up `key.id` in `0x195e208` (node: key id `+0x18`, tag `+0x1c`, record id `+0x20`), deletes the node, then by tag: 1 erases from map `0x195e2d0` and side tables `0x195e428`, `0x195e180`, `0x195e408`, `0x195e388`, releases a Kenshi object (`0x3b8fe0`) and runs `0x18a0a0`; 2 map `0x195e160`; 3 map `0x195e1a8` (`0x18d240` releases); 4 map `0x195e3c8`; 5 removes the id from its grid cell (cell = `floor(pos * 0.125)` per axis) and map `0x195e2f0`; 6 map `0x195e468`; 7 map `0x195e1c0`. Unknown id: no-op.
Meaning (I): server-authoritative corpse / body-recovery state (corpse info, loot description, "carried by", grave markers, pick-up zones, dropped items).

**Intent (client -> server).** Server runs it only if the host handle `0x1942128 != 0`, `0x1943e89` (corpses active) and, after parsing, `0x15a6c6b` (corpses feature on). Payload after the header, end exact:
| field | type | limits | meaning (I) |
|---|---|---|---|
| op | u8 | must be 1 | |
| record id | u32 | - | corpse record (tag-1 id) |
| mode | u8 | not range-checked; 0, 1, 2 used, any other nonzero acts as 1 | 0 pick up (carry), 1 put down, 2 put into a corpse disposal machine / burn |
| x, y, z | f32 each (u32 via `0x543bf0`) | within ±3.0e38 and `|v| < 1.0e7` (const `0x12c1f6c`, NaN-safe abs) | target position; server uses x and z |
Sender `0x19cf90(id, put_flag, x, y, z, burn_flag)` writes `01`, id as 2 x u16, `mode = burn_flag ? 2 : (put_flag != 0)`, x, y, z as 2 x u16 each, then `0x547b00` kind 0x28; not sent when `0x1942128 != 0`. Bad parse: nothing happens (counters only).
Server handler `0x17faa0(player, id, mode, x, y, z)` (M, partly I): player `+0..3` = player id, `+4` = body id, position at `+0xc/+0x10/+0x14`; needs the player's body (`player+4 != 0` and ok); record lookup `0x17f910` (miss drops any carry entry `0x19e540`).
- Mode 0: allowed if nobody carries the record or the same player already does; requires squared distance player-record (x/z, record `+0x10/+0x18`) <= 14400.0 (const `0x12fcb68`, 120 units); creates a carry entry in map `0x195e1f0` (carrier id, body id, GetTickCount64 twice, player position, `+0x24 = 1`, a 4-byte key = record id + body id as 2 x u16 each, hash `+0x50`), sets record `+0x200 = 1` and `+0x204..+0x20c` = player position; logs `corpses: player P carries corpse record #ID "NAME" at ...` (first 40, limit `0x1970514`). Too far: log `... from too far - REFUSED`. Carried by another player: silently ignored.
- Mode 1 / 2 with the record carried by this player: clears `+0x200`. Mode 2 with `0x15a6c8c` (disposal enabled) and `0x15a6c8d`: target (x, z) must be within 120 units of the player and `0x4298c0(x, ..)` must find a disposal machine (-1 none enabled; 0 no machine there -> log "...but the server sees none there - put down instead (`spine corpses burn check`)" and plain put-down; > 0 machine): the record is burnt, logs "went into a corpse disposal machine at (x, z)" / "burnt corpse record #", and counts the player's items in drop map `0x195e270`. Plain put-down: target farther than 120 units from the player is clamped to the player's position, then `0x17f9f0` puts the record down at (x, z); log `player P put corpse record #ID "NAME" down at (x, z)`.
- Mode 1 / 2 with the record not carried by this player: ignored.

**Open.**
- Internals of `0x17f9f0` (put-down) and `0x4298c0` (disposal machine test); the carry-entry ticker that moves a carried record with its carrier; meaning of F0 and of the two long strings of tag 1.

### cages (kind 41) - cage book entries and cage place flags
Vtable `0x11c8968`, v1, period 1 s. Status: **partial** (hash body `0x1ab790`, name check `0x1aa9c0`, meanings of the nested fields not decoded).
Functions: gather `0x1af090`, apply `0x1af5a0`, remove `0x1af930`, nested parser `0x1aadd0`. No intent (slot 12 stub `0x7ffa80`), slot 26 stub. The client apply does nothing (returns ok) when the host handle `0x1942128 != 0`; it stores only on a pure client.

**Rows (server -> client).** Value starts with `u8 tag`; the key type must match the tag, else bad.
Tag 1 = cage-book entry, key type 3 required:
| field | type | limits | meaning (I) |
|---|---|---|---|
| A | u32 | - | |
| B | u8 | - | |
| record | str16<=0x200 | nested record below; total length 4..0x200; parse must succeed | captured-member record |
End exact, and `key.id == 0x1ab790(A, B)` (hash mixing A, B and the constant `0x43414745 "CAGE"`; body not read), else bad. Stored under key id as (A, B, parsed record).
Tag 2 = cage place flags, key type 2 required with d == 8:
| field | type | limits | meaning (I) |
|---|---|---|---|
| a | u8 | < 2 | |
| b | u8 | < 2 | |
| c | u16 | - | |
End exact. Stored with the place coordinates (x, y, z scaled by `0xeba8bc`) plus a, b, c.
Nested record in the str16 (parser `0x1aadd0`): `u8 0x43 ('C')`, `u8 0x01`, `u8 d0` (< 4), `u8 d1` (`d1 & 0xffffff70 == 0`, i.e. bits 0-3 and 7 only), `str16<=0x40 s1` (checked by `0x1aa9c0`), `str16<=0x40 s2` (chars >= 0x20), `u8 flags` (bits 0 and 1 only), if `flags & 1`: 3 x 4 raw bytes x, y, z (each kept only within the bounds `[0x12c1bd8]..[0xeb5e6c]`, compared as floats), if `flags & 2`: `u8` (< 2), `u8` (< 2), `u16`; end exact. Meaning (I): captured member (names s1 / s2, location x, y, z, flags), sibling of the slave topic.
Remove (`0x1af930`): key type 2 removes all place entries of that place (multi-entry map `0x195e5a0`); any other key type removes the cage-book entry with `id = key.id` (map `0x195e4d0`).
Gather (M): tag-1 rows for every cage in the server vector `0x195e570` (0x48-byte elements: A = `+0`, B = `+4`, `+8` = owner id, string `+0x20`) whose bit is set in bitmap `0x195e508`, if `elem+8 == viewer id` or the viewer is within the squared distance `[0x12c1c98]` (x/z); value = `01`, u16 lo, u16 hi of A, B, u16 len (<= 0x200) + bytes; key type 3 id = hash(A, B). Tag-2 rows from set `0x195e538`, filtered by viewer distance (`[0x12c2598]`) and flag at `+0x48`: value = `02`, byte `+0x3c`, byte `+0x40`, u16 of `+0x44` (lo, hi); key = place key at `+0x18`.

**Open.**
- Hash body `0x1ab790`; name check `0x1aa9c0`; meaning of d0 / d1 and of a / b / c.

### ownheal (kind 42) - own-healing agreement and own-body healing claims
Vtable `0x123a338`, v1, period 5 s. Status: **partial** (row full, intent fields decoded, judging rules not decoded).
Functions: gather `0x7c5fd0`, apply `0x7c6510`, intent `0x7c6320` (slot 12), server tick `0x7c6170` (slot 11: every 30 s drops records older than 120000 ms, trims the six rate maps `0x1967d68`/`d28`/`dd0`/`da8`/`e30`/`d88` via `0x7cbca0`), claim handlers `0x7cdb60`, `0x7ccc50`, `0x7ccfc0`, `0x7c8850`, `0x7c7190`. Server switch `0x15a628e`; sub-switches `0x15a629e` (op 0x80, and 1..3), `0x15a629f` (0x81), `0x15a62a4` (0x82), `0x15a62a5` (0x83), `0x15a62a6`.

**Rows (server -> client).** Key `global:1` (apply tests `key[0]==5 && id==1`; other key bytes not tested).
| field | type | limits | meaning |
|---|---|---|---|
| level | u8 | | writer: 1 if `0x15a629e` off, 2 if `0x15a629f` off, 3 if `0x15a62a4` off, 4 if `0x15a62a5` off, else 6 (+1 if `0x15a62a6`) -> levels 1,2,3,4,6,7 (I: tier ladder of what the server judges) |
| rate | u16 | writer `min(0x15a62a8, 60000)` | I: minimum interval in ms between claims / healing rate cap (`spine ownheal rate`) |

End exact (`pos == len`), else return 2 (`mov eax,2` at `0x7c66fd`). Writer only when `0x15a628e != 0`.
Apply effect: `0x1949852 = (level != 0)`, `0x1949914 = level`, tick stored `0x1949918`; if `250 <= rate <= 60000` (`rate-0xfa < 0xe967`), `0x15a62a8 = rate`. When the accept state changes it logs `ownheal: the server (does not accept|accepts) our own healing - own bodies (follow the server's vitals as before | keep local bed and first aid, the server's vitals lay only what is worse)`.

**Intent (client -> server).** First payload byte = op.
| op | fields after op (wire order, types, limits) | effect / reply (I from log strings) |
|---|---|---|
| 1..3 | op value = entry count n; n x { u32 id (2 x u16 LE), str16<=0x400 }; reader failure -> `0x194997c`++ | handler `0x7cdb60` per entry; needs `0x15a628e`. Claim about an own body (`id` = body id, text = claimed body state: flesh, wear/wrap/part values); judge compares with the server's body: "claims a body that differs from the server's - no healing is taken until a claim matches...", "claims machine parts mended, but the server sees no repair...", "healing faster than the engine allows - clamped to the rate (`spine ownheal rate`)". |
| 0x80 | u32 healer, u32 target, f32 amount (0..1000.0 at `0xeb867c`; +-3e38 pre-check), str16<=0x400; exact end | handler `0x7ccc50`; client send not found. First-aid style claim: healer and target must be known bodies of the player (`0x543830`); amount clamped by `0x15a62a0 * dt + 1.0` seconds; needs `0x15a629e` and `0x15a629f`. |
| 0x81 | u32 id, str16<=0x80, u16 n (non-zero, clamped to <= 0x3c); exact end | handler `0x7c8850`; bed/rest claim; id and name must be non-empty; needs `0x15a629f`. |
| 0x82 | u8 n (1..3), n x { u32 id, str16<=0x400 } | handler `0x7c7190`; `ownheal: host player <id> claims the bed healing of body <id> - unconscious, laid in a bed of its zone by word (the server has no bed there); taken by the same judge under the bed limit (spine ownheal hostbed)`; needs `0x15a62a4` and a host (body-owner lookups `0x1965188`/`0x19651b0`). |
| 0x83 | u32 a, u32 b (a != b), f32 amount (0..1000), str16<=0x400, optional trailing f32 c (0..1000; absent -> 0); exact end | handler `0x7ccfc0`; `ownheal: host player <id> bandages player body <a> ... (kit ..., wounds power ...) - its NPC ...; taken by the same judge (spine ownheal hostaid)`; needs `0x15a62a5`; `a` must be the sender's body (owner check via `0x1965188`/`0x19651b0`). |

Any other op = bad (`0x194997c`++). Rejection counters at `0x19498xx`/`0x194994x`.
Senders: ops 1..3 batch writer `0x7c6a70`/`0x7c6b45` -> helper `0x7cdab0` (lossy `0x547ce0`, kind 42, site `0x7cdb16`); op 0x81 at `0x7c874b` (reliable `0x547b00`, bytes written as u8s, LE); op 0x82 at `0x7c713c` (lossy, inside `0x7c7090`, op 0x82 + n then payload); op 0x83 at `0x7ccb1d` (lossy, 6 u16 + string); op 0x80 sender not found (same family as 0x83). "Host player" = the player's machine that owns the NPC bodies.

**Open.**
- Judging rules (rates, kit charges, bed checks) in `0x7cdb60`, `0x7ccc50`, `0x7ccfc0`, `0x7c8850`, `0x7c7190`.
- Client send site of op 0x80.

### ptrade (kind 43) - player-to-player trade windows
Vtable `0x123afd8`, v1, period 1 s, kind 0x2b (M: call `0x7d9a7f`; `0x548210(0x2b, ..)` in `0x7db920`). Status: **partial** (item string meanings, swap execution and record creation path not decoded).
Functions: gather `0x7d6690`, apply `0x7d7370`, remove `0x7d7410`, intent `0x7d6d00`, notice `0x7d7920`, senders `0x7d99a0` (common helper, call `0x7d9a7f`), `0x7d7670` (op 1, call `0x7d775c`). Side block writer `0x7da350`, reader `0x7e0300`; item writer `0x7d5640`, reader `0x7d5810`. Slot 26 stub. Server state (I): vector of 0x100-byte trade records at `this+8..+0x10`.

**Rows (server -> client).** Key must be type 5 (global), else bad.
- Global 1 (always written first): value = 1 byte, must be non-zero (no end check). Client sets feature-ready byte `+0xc0 = 1` and a double at `+0xc8` (`0xc1cdcd6500000000`, about -1e9, "never sent yet", I); the heartbeat only runs once this was seen.
- Global 2 = the viewer's current trade (gather picks via `0x7da510(this, viewer)` the viewer's trade in state 0/1, else the highest id; no trade -> no row):
| field | type | limits | meaning (I) |
|---|---|---|---|
| trade id | u32 | - | record `+0` |
| state | u8 | < 4 | record `+0xb8`; 0 editing, 1 both accepted / waiting for confirm, 2 done, 3 cancelled |
| revision | u32 | - | record `+0xbc`, bumped on every offer change by `0x7dad10`; accept / confirm echo it |
| own side | side block | - | |
| partner word | u32 | - | partner side `+4`; character / body handle for inventory lookup via `[0x1965158]` |
| partner name | str16<=0x20 | - | side `+8` |
| partner side | side block | - | |
| message | str16<=0x60 | - | record `+0xd8`, status text such as "both accepted - confirm in 5 s" |
End exact.
Side block (writer `0x7da350`, reader `0x7e0300`):
| field | type | limits | meaning (I) |
|---|---|---|---|
| accepted | u8 | - | side `+0x54` |
| confirmed | u8 | - | side `+0x55` |
| cats offered | u32 | - | side `+0x50` |
| n | u8 | <= 0x18 (24), more -> whole row bad | |
| n x item | item | - | |
Item (writer `0x7d5640`, reader `0x7d5810`; 0xb0 bytes in memory):
| field | type | limits | meaning (I) |
|---|---|---|---|
| s1, s2, s3, s4 | str16<=0x40 x4 | reader rejects any byte < 0x20; record `+0`, `+0x28`, `+0x50`, `+0x78` | name / template id etc., not decoded (compare `0x7d5500` identifies "the same item") |
| f1, f2, f3 | u8 x3 | - | record `+0xa0`, `+0xa1`, `+0xa2`, not decoded |
| quantity | u16 | writer clamps to 0xffff | record `+0xa8` |
Client effect (M): copies the row into "current trade" at `+0xd0` (valid byte `+0xd0`, id `+0xd4`, state `+0xd8`, revision `+0xdc`). A new id in state 0/1 logs `ptrade: trade #N - Trading with "NAME" opened - put things in, Accept, then Confirm`; a change to state 2/3 logs `ptrade: trade #N - <message>`. When the counterpart / state changes in state 1 the client sets a wait deadline (`+0x1b8 = now + 5 s`) the confirm button honours ("wait - confirming opens in a moment").
Remove (`0x7d7410`): global 1 = no-op; global 2 clears the current trade (valid field -1).

**Intent (client -> server).** Payload after the header: `u8 op`. Op 1 has no trade id; ops 2..7 start with `u32 trade id`; the handler looks for a record with that id, state 0 or 1, and the sender being side A or B (else notice "no such trade (it may have ended)"). Reply = notice `0x547e50(player, 0x2b, code = client seq, flag, text)`; unknown op -> "unknown trade request".
| op | fields after the trade id | server effect (callee) |
|---|---|---|
| 1 | `u8 flag` (no trade id) | non-zero inserts the player in a set at `this+0x68` (`0x10a880`); zero does nothing. I: "present / I can ptrade" registration, the client resends it every 60 s (double `0xfaebd8`) while the feature-ready byte is set; sender `0x7d7670` writes `01 01` |
| 2 offer | one item (layout above), quantity != 0, end exact (else "a broken request") | offer the item (`0x7daf70`): refuses "your bag did not read", `you have no "..."`, "all of it is in the offer already", "the offer is full (24 lines)"; bag items and bag twins cannot be given; quantity clamped to what the player owns minus what is already offered; text `offered N x NAME` |
| 3 take back | one item, quantity != 0, end exact | take back (`0x7db5b0`): lowers or removes the line; "taken back" / "that is not in your offer" |
| 4 cats | `u32 cats` (new total offered), end exact | set the money offer (`0x7db6d0`): "your wallet is not ready", `you have only N cats`, "the same sum" |
| 5 accept | `u32 revision`, `u8 accept` (0/1), end exact | (`0x7db920`): accept = 1 with a different revision -> "the offer changed - look again"; state 1 -> "accepted already - now confirm" (accept = 0 in state 1 returns to editing); "nothing is offered yet" if both sides are empty; both accepted -> state 1 with a 5 s timer ("both accepted - confirm in 5 s") |
| 6 confirm | `u32 revision`, end exact | (`0x7dbc10`): only in state 1; revision mismatch -> "the offer changed - look again"; "both must accept first"; "wait N s after both accepted"; both confirmed -> executes the swap ("Trade done with X and Y", "the trade is done") |
| 7 cancel | none, end exact | record -> state 3 (`0x7daa00`), message `Trade cancelled: <name> declined`, notice "trade declined" |
Senders: common helper `0x7d99a0(this, op, body, label)` requires `0x1942128 == 0` (else notice "the server has no hands") and a valid current trade with state < 2 ("you are not trading"); builds `[op][u32 trade id = this+0xd4][body]`, sends reliable `0x547b00` (call `0x7d9a7f`), stores seq + label + time in a pending map (`this+0x1d0`); entries time out after 15 s (double `0x1021100`, log `: no answer from the server in 15 s`). Wrappers (all called from the client UI `0x7d7fa0`): offer / take back `0x7d9bd0` (ops 2, 3; item via `0x7d5640`), cats `0x7d9df0` (op 4), accept `0x7d9f50` (op 5, body = revision `+0xdc` + accept byte), confirm `0x7da0c0` (op 6, body = revision, local 5 s wait check), decline `0x7da260` (op 7, empty body).
Notice (slot 20 `0x7d7920`, M): counts accepted / refused (`+0x228` / `+0x22c`), removes the pending request with that code, on refusal logs `trade request: REFUSED - <text>` and sets the visible message field (`+0x188`, text <= 0x60).
Record creation: not through ops 1-7; the requests topic (kind 5, op 2 accept) calls `0x7d8fe0(player A, body A, name, player B)` (M, call `0x85db81`).

**Open.**
- Meaning of the 4 item strings and 3 item flag bytes (matcher `0x7d5500`, inventory scans `0x7d5f90` / `0x7d6210`); the swap execution inside `0x7dbc10`.

### skillmem (kind 44) - best skill levels per account
Vtable `0x123b948`, v1, period 2 s. Status: **full**.
Functions: gather `0x7e7600`, apply `0x7e79a0`, remove `0x7e7c20`, server tick `0x7e74d0` (slot 11), slots 19/21 `0x7e7ca0`/`0x7e7c60`. No intent (slot 12 stub), no upstream.

**Rows (server -> client).** Key `global:1` (apply tests only `key.id == 1`, else returns 2, asm `0x7e79c4`; writer builds type 5 id 1).
| field | type | limits | meaning |
|---|---|---|---|
| level | u8 | 1..20 (`level-1 <= 0x13`) | stored as float at `0x15a6290`. Writer `(int)(clamp(0x15a6288, 1.0, 20.0) + 0.5)`; meaning unknown (I: server-side skill cap / level scalar) |
| n | u8 | <= 0x40 | writer: count of qualifying entries |
| entry[i], i < n | n x { u8 idx, f32 value } | idx 1..0x26 (38 stats); value raw LE 4 bytes, in +-3e38 and `0 <= v <= 200.0` (`0xeb6934`), else bad | writer sends `floorf(v*10)/10` as two u16 (low first), includes only idx != 0x14/0x21 with value >= 0.1 (`0xebef5c`). Entries idx 0x14 and 0x21 are read/validated but dropped by the client. |

End exact (`pos == len`); ok -> `memcpy(0x179ec80, 64 floats = 0x100 B)` (zero-initialised, absent idx = 0.0), `0x19499d2 = 1`, `0x1949a0c`++, return 0; bad -> `0x1949a10`++, return 2. Remove (`0x7e7c20`): zeroes the 0x100-byte table, `0x19499d2 = 0`, level reset to a constant.
Gather (M, `0x7e7600`): only when `0x1949853 != 0` (server). Looks up the viewer's account name lowercased (`0x868f30` + `0x868e20`) in the skill-records map `0x1967e80` and writes that account's best skill levels; one row per viewer.
Source (I, strings `0x123b130..0x123b1e0`): server tick `0x7e74d0` (1 s accumulator, load `0x7e4af0`, save `0x7e5650`) keeps a text file `# AnarchyKenshi skill records v1 - account<TAB>skill:best level,...` ("skillmem: N account skill record(s) read from <file>"; refuses to overwrite an unreadable book). The topic tells a player the best level each skill ever reached on their account (kept across character deaths); file name not extracted.

### fightxp (kind 45) - fight XP caps
Vtable `0x123cda8`, v1, period 1 s. Status: **full** (which of a/b is "sparring" is inferred).
Functions: gather `0x7fbb10`, apply `0x7fbd80`, remove slots 15/21 `0x7fbe40`, server tick `0x7fba60` (slot 11, 1 Hz via `0x7fb340`; clears tables when kind 0x2d is off), client tick `0x7fbe60` (slot 19), console `0x7fc4e0` (slot 33). No intent, no upstream.

**Rows (server -> client).** Key `global:1` (apply: `key.id == 1`, else return 2; writer builds type 5).
| field | type | limits | meaning |
|---|---|---|---|
| a | u8 | <= 200 | stored as f32 at `0x1949ad4` |
| b | u8 | <= 200 | stored as f32 at `0x1949ad8` |

End exact (`pos == len`); ok -> return 0; bad -> `0x1949ae0`++, return 2. Remove: both = 0.
Writer (M, `0x7fbb10`; only if `0x1949a88 != 0` and now < `0x1949a98`): looks up the viewer (`*param_2` key in uint map `0x1967f50`) record; `a = 30.0 (0x15a6264)` if `record[4] != 0 && now < record[4] + 30000 ms (0x15a626c)`, else 0; `b = 50.0 (0x15a6268)` if the same holds for `record[5]`; row only if a > 0 or b > 0; bytes `(int)(a+0.5)`, `(int)(b+0.5)`.
Meaning (I from strings `0x123cc18..0x123ccd0` "fightxp: sparring cap ..., fights with players cap ..., grace ... s after the last blow"): cap on fighting-skill XP gain while the viewer had a recent (30 s) blow in a spar (record[4]) or in a fight with players (record[5]); 30 / 50 are the server's default caps. Client tick `0x7fbe60` (every `0xeb6fc4` s) takes the smaller positive of a, b and hands it to `0x7fd3d0` (applies the cap to the local character via `0x4965b0`).

### heldby (kind 46) - client reports which record carries its bodies
Vtable `0x123cf58`, v1, period 5 s. Status: **partial** (server effect function `0x3fd080` not decoded).
Functions: intent `0x7feef0` (slot 12), client tick/send `0x7ff370` (slot 19), clear slot 21 `0x7ff720` (clears the dirty byte `this+0xc`), send site `0x7ff4f3`. Client-to-server only: no apply (slot 14 = `0x935c40` returns 2), no gather (slot 5 stub; slot 4 `0x935c60` period stub), no upstream.

**Intent (client -> server).** No op byte. Exact end required.
| fields (wire order) | limits | effect |
|---|---|---|
| `u8 n`, then n x { u32 a, u32 b } (each as two u16 LE, 8 bytes per pair) | n <= 8 (`n > 8`, reader failure or wrong end -> drop, `this+0x20`++); reader loop stops after 8 | Server keeps a pair only if `a != 0 && b != 0` and `a` is in the sending player's own body list (`player+0x18..+0x20`, u32 array; others counted in `this+0x24`); accepted count added to `this+0x1c`; then `0x3fd080(player, list)` (4842 bytes) applies it. Meaning (I): `a` = id of a body the sender's machine owns, `b` = id of the record (NPC/body) carrying it; the server re-publishes the carry so every machine shows it. |

Sender (M): client tick `0x7ff370` every 0.5 s (`0xeba8bc`), reliable `0x547b00`, kind 0x2e, site `0x7ff4f3`; only when connected (`0x1942050`/`0x1942058` != 0) and `0x1942128 == 0`. Sorts the pair list (`0x442630`) from `0x3fefd0` (local records that carry one of our bodies) and sends the first 8; if the list is empty it sends one empty list only when the dirty byte is set (so the server clears). Log: `heldby: a record we run carries our body <a> (record <b>) - the server is told, everybody sees the carry` (first 20 times, `0x19710a4`).

**Open.**
- Effect function `0x3fd080`.

### townlayout (kind 47) - server-authoritative building names of towns
Vtable `0x123eaf8`, v1, period 2 s. Status: **full** (use of flag/fa/fb is inferred).
Functions: gather `0x8182e0`, apply `0x818960`, intent `0x818420` (slot 12), server tick `0x8182a0` (slot 11, calls `0x816a30`), client tick/send `0x818c40` (slot 19, 500 ms; send site `0x8191cb`, kind 0x2f), clear slot 21 `0x819780`, console `0x819ae0`/`0x81a110`, record creator `0x816950`, key builder `0x543430`. Switches: `0x15a6254` (townlayout enabled), `0x15a6255`/`0x15a6256` (sub-modes; `0x15a6256` = strict mode).

**Rows (server -> client).** Key type 2 (place) built by `0x543430(x,y,z,tag)`: `key[0]=2`, `key+4 = 0`, ints at +8 (x), +0xc (y), +0x10 (z) = `floor(v*2.0 + 0.5)` (0 if |v| > 1e9), `key+0x14 = 0x544c4159 "TLAY"`. Apply requires `key[0]==2 && key+0x14 == 0x544c4159`, else returns without work; world position = `int * 0.5`.
| field | type | limits | meaning |
|---|---|---|---|
| name | str16<=0x60 | 1..0x60 chars, each 0x20..0x7e and not TAB, else bad | building name |
| flag | u8 | | stored at record+0x38 |
| fa | f32 | | record+0x3c |
| fb | f32 | | record+0x40 |

Reader failure -> drop. Apply has no exact-end test (only reader ok and name valid) and no return code (treat as 0). Existing record at that place: if the name differs it is replaced (`0x1949c48`++), record+0x44 = 0; new place -> `0x816950` creates it (`0x1949c44`++). Then, if `0x15a6254 && 0x15a6255`, stamps tick `0x1969248` for that grid cell.
Gather (M, `0x8182e0`; only when `0x15a6254 != 0` and the viewer position is valid): every stored place within 10000 units (1.0e8 = `0x12c8328` squared, xz) of the viewer is sent: key via `0x543430` with tag `0x544c4159`, value = raw stored blob (name, flag, fa, fb) via `0xd5d00`.

**Intent (client -> server).**
| op | fields after op | effect / reply |
|---|---|---|
| 1 (only if `0x15a6254` on, else ignored) | `u8 n` (<= 0x40, else `0x1949c34`++), then n x { f32 x, f32 y (read and dropped), f32 z, str16<=0x60 name (same charset), u8 flag, f32 fa, f32 fb }; exact end not checked after the loop except reader ok | Per entry: name must be a known building type/name (`0x8158d0`, nonzero); sender position valid and within 12000 units (1.44e8 = `0x1319cc8`) of (x,z), else skipped. Strict mode (`0x15a6256`): zone lookup `0xb73760(x)` must be 1, or -1 with flag set and (fa,fb) within 3000 units (9e6 = `0x12c1c98`) of (x,z) and `0xb73760(fa) == 1`; plus per-player quota `0x19691d0 < 40000` + `0x815190(player)`; then `0x816950` inserts. After a successful batch logs `townlayout: player <id> named <n> new building(s) of a town's layout - first named, the truth for every machine` (first 40, then every 200th). |

Sender: client tick `0x818c40` (every 500 ms), site `0x8191cb`, kind 0x2f.
Meaning (I from the log): the first player to name a town's buildings fixes the name for everyone. (fa,fb) = a second world x,z of the building (I: door/zone anchor; flag says it is valid); not proven.

### integrity (kind 48) - game-integrity census and stand-console check
Vtable `0x123bf78`, v1, period 5 s. Status: **partial** (content of the probes in the report not decoded).
Functions: gather `0x7eea00`, apply `0x7eeb40`, remove slot 15 `0x7eed00`, intent `0x7eeee0` (slot 12), client tick/send `0x7eed70` (slot 19; `call 0x547b00` with ecx=0x30 at its end), clear slot 21 `0x7eed40`, console slots 32/33 `0x7ef0a0`/`0x7ef240`, slot 13 `0x7ef090` (writes two out floats 0.2 and 3.0; role not defined), report builder `0x7f7740` (via worker `0x7ee820`, buffer `0x15aa9e8`), sanitiser `0x7ee950`, judge `0x7ee2b0`.

**Rows (server -> client).** Key must be exactly `global:1` (apply tests `key[0]==5`, id at +4 == 1, +8,+0xc,+0x10,+0x14 == 0).
| field | type | limits | meaning |
|---|---|---|---|
| flag | u8 | no exact-end check; read failure leaves ok=0 | writer: 1 if the server's integrity mode is on (config `0x15a6280`; -1 = auto from `0x195cd80`), else 0 |

Apply: `0x1949a34 = flag != 0`, `0x1949a3c = 1` (client may now send its census). If flag == 0 and the client has the "stand console was used" mark (`0x1949a38`): log "integrity: this game session ran writing console verbs on a stand server and then joined a server that is not a stand - leaving it. Restart the game to play here." (`0x123bb10`) and shows "STAND CONSOLE WAS USED IN THIS GAME - restart Kenshi to join this server" (`0x123bbc0`) (I: "stand" = a test-server mode with console verbs; the client refuses to join a normal server after using it). Remove clears both flags.

**Intent (client -> server).**
| op | fields after op | effect / reply |
|---|---|---|
| 1 (else bad, `0x1949a58`++) | `str16 report` (<= 0x1db0 = 7600 bytes) | Server `0x7eeee0`: str16 read failure -> bad; else sanitises (`0x7ee950`: every byte < 0x20 or > 0x7e becomes '?'), stores it per player (map `0x1967f08`), logs `integrity census: player %u <text>`; when the mode is on (`0x15a6284`, else `0x195cd00`) judges it with `0x7ee2b0` against a judge file (verdict words clean / unlisted / suspect / FORBIDDEN): mode `kick` = player thrown out (`anti-cheat: ... thrown out by the anti-cheat (integrity = kick)`, text "... Restart Kenshi without it to join this server."), mode `record` = logged only. Console: `integrity verdict|census|mode|standword|judgefile|selftest`. |

Sender: client tick `0x7eed70`; builds the report once in a worker thread (`0x7ee820` -> `0x7f7740`) after 30 s (`0xeb6a5c`) of connected time with `0x1949a3c` set; sends it once (`0x1949a48` = sent).
Report content (I, from format strings of `0x7f7740`; text, not binary): starts `v1`, `build=<Steam|GOG|unknown> exe=<hash>:<size>` (or `exe=unreadable`), optional ` wine=<version>` (ntdll `wine_get_version`), ` | const slots=%d squad=%d squads=%d faction=%d` (game constants) or ` | const unknown`, ` | exe diffs=%u (checked %u)` plus `<region>+<off>` entries (code-integrity probes of the game exe), ` | mods:` list (the `Plugin=` lines of Plugins_x64.cfg and Steam workshop folder `workshop\content\233860` entries `ws:<id>`, `rejson %u`).

**Open.**
- Details of the code-integrity probes in the report.

### acttrace (kind 49) - admin-requested action trace upload
Vtable `0x123d088`, v1, period 3600 s. Status: **partial** (content of the trace lines not decoded).
Functions: intent `0x7ffa90` (slot 12), client tick/send `0x7ffc20` (slot 19; every 0.5 s; send site `0x7ffdae`, reliable `0x547b00`, kind 0x31; only connected and `0x1942128 == 0`), client notice `0x7ffe20` (slot 20), clear slot 21 `0x7ffea0` (on disconnect: if the server had asked, logs "act| the server no longer asks for the action trace" and cleans up), console slot 32 `0x7ffeb0` (stats `acttrace sent=<n> ...`), line source `0x80c490`, request switch `0x80c360`, store `0x80c810`. No row apply (slot 14 = `0x935c40` returns 2), no gather (slot 5 stub), no upstream.

**Server -> client request (notice).** Slot 20 args `(this, u32 code, u8 flag, std::string text)`: text must equal the 8 chars `acttrace` (memcmp len 8, `0x123d068`), then `0x80c360(flag, code)` switches the trace on/off (strings "asks for this client's action trace (an admin debugs actions here)" / "no longer asks for the action trace"). Meaning of `code`: I (admin id / request id).

**Intent (client -> server).** Client builds it in `0x7ffc20` from `0x80c490` (returns pending lines and an id); writer and reader `0x7ffa90` agree (M).
| op | fields after op (wire order) | effect / reply |
|---|---|---|
| 1 (else bad) | `u32 id` (2 x u16, low first), `u16 n` (<= 0x14 = 20, else bad), n x `str16<=200` (u16 len + bytes; a longer line rejects the intent); exact end | ok -> `this+0x10`++ and `0x80c810(player, id, lines)` stores them for the admin console (prefix ` C `); bad -> `this+0x14`++. |

**Open.**
- Content of the lines (action names of a character); `0x80c490`/`0x80c810` not read.

### wear (kind 50 = 0x32) - per-part wear of each body
Vtable `0x1238ba8`, v1, period 2 s. Status: **full** (server -> client only; no intent, no upstream).
Functions: gather `0x7b8f70`, apply = thunk `0x7b95b0` -> body `0x7ba290` (hold flag 0), remove `0x7b95d0`, cached value builder `0x7ba7d0` (-> `0x7b8620`). Slot 12 = stub `0x7ffa80`, slot 26 = stub `0xd5dd0`, slots 19/20/29 stubs.

**Rows (server -> client).** Key type 1 (body), else counter `+0x38`. Value layout is **identical to splint** (same primitives and limits; encoder is the same name-sorted `0x7b7b00`, **M**), end exact (else `+0x38`):

| field | type | limits | meaning (I) |
|---|---|---|---|
| count | u8 | <= 0x10 | entries |
| per entry: name | str16 | <= 0x40 | part name (`part->data->name`, data+0x58) |
| per entry: value | u16 | - | wear/damage amount in **tenths** (log prints `v/10.v%10`) |

Meaning (**I**): per-part wear of the body's medical parts. Same enumerator as splint (host getters `0xcd360`, count `0x2a9f90`, get `0x2a9fa0`) but reads and writes `part+0x50` (**M**: `0x7b8620` reads `plVar8[10]`; apply writes `*(float*)(part+0x50) = value / 10.0f`).
Skipped parts (`0x7b8540`, **M**): `0x15a62cb == 0` -> host fn `0xe9(part)` (same predicate as splint); else host fn `0xea(part)` state (4 -> not skipped; otherwise skipped if `0xcd7540(medsys, state) == 2`).
Apply (`0x7ba290` -> `0x7b8ce0` -> `0x7b87d0`, **M**): resolve the character via `0x1965158(id)`; if protected (`0x7b8d20` = `0x9a3fe0(char)`, **I**: own body) -> counter `+0x2c`, nothing applied. Otherwise per part: new = matching entry value (0 if none); cur = `floor(part+0x50 * 10 + 0.5)` capped at 65535; if cur != new:
- **Repair-kept check:** per-body record map `this+0x48` (id -> map name -> {u32 kept value @+0x40, u64 tick @+0x48}). If `cur < new` and a kept record exists with `cur < kept` and its tick is < 20000 ms old, the part keeps the kept value (counter [1] of the out struct), else counter [2]. (**I**: stops the server re-imposing old wear for 20 s after a local repair.)
- Otherwise `part+0x50 = new / 10.0f`; game fn `0xe8(part)` (max-hit-points getter, host signature `0x296610`) is read before and after; if both are > `0xeb9a4c` and `|after - before| > 0x12c1b4c`, the sibling floats `part+0x40, +0x44, +0x48` are **rescaled by after/before** (`+0x40` capped at after, `+0x48` clipped to `after - max(+0x40, 0)`). Then host refresh `0x6435c0(part)`; changed-part counter.
Results: new per-body record stored at `this+0x48` (`0x7bad00` / `0x7bb340`); counters `+0x18` applied rows (`+0x20` held variant), `+0x1c` no change, `+0x30/+0x34` sums of the repair-kept counters, `+0x28` character missing, `+0x3c` apply failed. Log (max 0x10): `wear: body <id> - <n> part(s) took the server's wear[ (hold)]` then ` <name>=<v/10>.<v%10>` per entry, or ` (none)` for an empty row.
Remove (`0x7b95d0`, **M**): key type 1 and body known -> drop its record (`0x7bae70`); if the character exists and is not protected, apply an empty list (every part's wear -> 0.0; counter `+0x24` if any changed).
Gather (`0x7b8f70`, **M**): as splint; viewer's bodies via `0x1965140/60`, `0x15a6690` cache mode, key body:`id`, value = cached string from `0x7ba7d0` (-> `0x7b8620`: up to 0x10 parts with non-zero tenths of `part+0x50`, then encoder `0x7b7b00`), non-empty only, at most **0x100** rows per gather (counter `+0x10`).

**Open.**
- Units/meaning of `part+0x40/+0x44/+0x48` (rescaled together with `+0x50`; **I**: flesh / stun / bleed style floats); what fn `0xe8` returns exactly; `0xcd7540`; repair-kept map layout beyond the fields above.

### acidrain / acidenv (kinds 51 = 0x33, 52 = 0x34) - sheltered own bodies per environment category
One class (RTTI `EnvTopic`, I), vtable `0x1239048`, v1, period 5 s. Status: **partial** (category-to-hazard mapping not decoded).
Instances (static ctors): `acidrain` = global `0x198efb0`, kind member @+8 = 0x33, name `0x1238ee0` "acidrain", category list `{0}`; `acidenv` = global `0x198eeb0`, kind 0x34, name `0x1238ef0` "acidenv", category list `{1, 2}` (list built from stack init values in the ctors at `0x7bf06a`/`0x7bf184`; u32 vector at `this+0x18..+0x20`). Slot 1 returns `this+8`.
Functions: intent `0x7bc1e0` (slot 12), server tick `0x7bc5c0` (slot 11; also client-side tick, gated by `0x19497ef` and per-category switches `0x19497ec + category`), client tick `0x7bcd00` (slot 19), clear slot 21 `0x7bd700`, console slots 32/33 `0x7bd770`/`0x7bde90` (stats `sheltered`, `-ticks=`, `-refused`, `-would=`, ` changes=`, `said=`, ` wet-bodies=`, ` dry-bodies=`, `intents=`, ` held-moved=`, `held-leaving=`, `FOREIGN=`), per-player store `0x7bf820(this+0xa8, player)`. No row apply (slot 14 = `0x935c40` returns 2), no gather (slot 5 stub; slot 4 `0x935c60`), no upstream.

**Intent (client -> server).** No op byte (M, reader `0x7bc1e0` and writer `0x7bd1b3..0x7bd30b` agree). For each category c in the instance's list, in list order: `u8 n` (<= 0x20, else bad `this+0xd4`++), then n x `u32 body id` (4 raw LE bytes). The value must end exactly after the last category (`pos == len`), else bad.
Effect: ok -> the per-player store is rebuilt: for every category the old set is cleared and each id != 0 that is in the sender's own body list (`player+0x18..+0x20`) is inserted (ids not owned counted in `this+0xd8` "FOREIGN"); timestamp stored at `+0x60`; `this+0xd0` counts intents.
Sender: client site `0x7bd35c` (`mov ecx,[r15+8]` = the instance's own kind, then `call 0x547b00`; the same code sends kind 0x33 or 0x34).
Meaning (I from console stat names and topic names): the client reports, per environment category (0 = acid rain; 1 and 2 = other hazards for acidenv), the ids of its own bodies in the sheltered/dry state; the server tick compares with its own check ("would", "changes") and applies the hazard damage ("wet-bodies" / "dry-bodies") to the others.

**Open.**
- Which category number maps to which hazard (`0x7bc5c0`, `0x7bcd00` only skimmed).

### shots (kind 53 = 0x35) - ranged shots relayed to nearby players
Vtable `0x123d338`, v1, period 0.1 s. Status: **full** (visual/replay sync, not damage).
Functions: gather `0x800c60`, apply `0x8011d0`, intent `0x800e30` (slot 12), client tick/send `0x801340` (slot 19; send site `0x8014c2`, kind 0x35), clear slot 21 `0x801540`, slot 11 `0x800b60`, shot reader `0x8005e0`, shot writer `0x8004c0`, intent validator `0x800910`, ring trim `0x801d70`. No upstream (slot 26 stub).

**Shot record (wire, 37 bytes; reader `0x8005e0` and writer `0x8004c0` agree; every u32/f32 = two u16 LE, low first).**
| field | type | limits | meaning |
|---|---|---|---|
| shooter char id | u32 | != 0 (else invalid) | server resolves it in the body map `0x195fc48` or `0x331950` and checks ownership (`0x800910`) |
| second id | u32 | | stored, never used in apply or validator (I: unknown, maybe weapon/ammo id) |
| pos x,y,z | 3 x f32 | each in +-3e38 (else ok cleared, value 0); final plausibility: |.| < 1e6 for x,z and < 1e5 for y (`0x12c2598`/`0x12c1b44`) | shot origin |
| quaternion/direction | 4 x f32 | same range test; final test `0.5 < x^2+y^2+z^2+w^2 < 1.5` (`0xeba8bc`/`0xeb6b44`) | I: orientation quaternion |
| flag | u8 | bit 0 passed to the shot replay as a boolean | I: e.g. left/right hand |

Reader ok only if `ok && id != 0 && quaternion-length test && ranges`.

**Rows (server -> client).** Key type 5, `id` = server shot sequence number (`0x1949b58`, never 0); value = the 37-byte record, stored raw in the ring entry (+0x38). Gather `0x800c60` sends the viewer every ring entry whose shooter player (`entry[1]`) is not the viewer and whose position is within 2500 units (6250000.0 = `0x12c4c18` squared, xz plane) of the viewer. Apply `0x8011d0`: reads the record (`0x8005e0`), requires exact end, inserts into the dedupe cache `0x1967fc0` keyed by the row key id (trimmed above 0x1000), then finds the character by id (`0x449140`, else `0xa73210`) and replays the shot (`0x493860(char,&pos,&quat,flag&1)`). Returns 0 for ok, duplicate or unknown character; 2 only for reader failure / wrong end.

**Intent (client -> server).** No op byte.
| fields | limits | effect / reply |
|---|---|---|
| `u8 count`, then count x 37-byte record | count <= 0x10; exact end required (else drop all, `0x1949b50`++) | Server `0x800e30` validates each record with `0x800910`: shooter id != 0; the character within 400 units (160000.0 = `0xeb5ecc` squared) of its known position; and belonging to the sending player (in the player's owned-body list, or owner lookups `0x99da00`/`0xa73300`/zone cell match `0x9a5e40` with cell size 4608.0). Accepted shots get the next sequence id and go into the ring (limit 0x200 entries, then `0x801d70` trims). Counters `0x1949b40..0x1949b58`. |

Sender: client `0x801340`, up to 16 queued shots per message, kind 0x35, site `0x8014c2`.

### rank (kind 54) - faction ranks from dialogue rewards
Vtable `0x1245108`, v1, period 5 s. Status: **partial** (speaker lookup, line price lookup, reply codes not decoded).
Functions: gather `0x873d70`, apply `0x875300`, remove `0x875320`, intent `0x874170`, sender `0x86bd20` (call `0x86bf1b`, kind 0x36), rank applier `0x620b30`. Slot 26 stub. (Ghidra lists apply / remove with size 8 / 9; the decompile under those headers is the real body.)

**Rows (server -> client).** Key type 3 with `key.id == FNV-1a32(name bytes)` (basis `0x811c9dc5`, prime `0x1000193`, unsalted; name = the str16 in the value) and key words +8, +0xc, +0x10, +0x14 all zero, else bad.
| field | type | limits | meaning (I) |
|---|---|---|---|
| faction name | str16<=0x60 | 1..0x60 printable chars (checked by `0x620b30`) | |
| rank | u32 (2 x u16 lo, hi) | <= 100 (0x64) | the player's rank in that faction |
End exact, else bad. Effect: stored in the map `this+0x18` (key -> name); `0x620b30(name, rank, &changed)` resolves the faction by name in Kenshi (`0x739180`), sets the player's standing / rank, and logs `rank: <name> -> <rank>` on change; its return value (1 rejected, 0 ok, other bad) is checked by the caller (I). Remove (`0x875320`): removes by key from `this+0x18`.
Gather (M): active only if `0x1949eea` (ranks loaded). Takes the player-name string (`0x868f30` / `0x868e20`), looks it up in the server map `0x19696d0` (name -> sub-map faction name -> rank, sub-map at node `+0x48`) and writes one row per faction: value = u8 len_lo, u8 len_hi (len <= 0x60), bytes, then rank as 4 bytes LE (node `+0x40`); key as above. Same order as apply.

**Intent (client -> server).** Payload after the header, end exact:
| field | type | limits | meaning (I) |
|---|---|---|---|
| op | u8 | must be 1 | |
| S1 | str16<=0x60 | printable 0x20..0x7e, no TAB; player-name string <= 0x40 | faction name |
| X | u32 | `1 <= X <= 5` and `X == current rank + 1` ("one rank at a time"; `X - 1 >= 5` -> "no such rank") | requested rank |
| Y | u32 | speaker must resolve via `0x876a30`, be on the server, within squared distance `[0x1315b88]` of the player's body and a member of faction S1 | speaker (NPC) body id |
| S2 | str16<=0x60 | printable, no TAB; looked up by `0x8692e0` -> price, else "that dialog line gives no rank" | dialog line id |
Field meanings of Y, X, S2 are I (log string order, `0x876a30` / `0x8692e0` usage).
Refusal notices (M strings): "faction ranks are not read yet" (`0x1949eea` = 0), "dialog rewards are off on this server" (`0x15a6228` = 0), "no such rank", "too many factions with a rank" (>= 16 and current rank 0), "that dialog line gives no rank", "nobody to talk to", "the speaker is too far", "the speaker is not of that faction", "the price of that line was not paid to this person" (the player record must hold the speaker id at `+0x40`, a payment within 60 s (0xea60 ms) at `+0x48`, amount >= price at `+0x50`), "one rank at a time". On success the new rank is stored per (player name, faction), tables marked dirty (`0x1949f54`), and logged `rank: <player> asked rank <X> in <S1> (line <S2>, price <P>, speaker <Y>)`. Sender `0x86bd20`.

**Open.**
- Speaker lookup `0x876a30`, line-to-price `0x8692e0`, notice code numbers.

### rules (kind 55) - server rules text shown to players
Vtable `0x1240218`, v1, period 2 s. Status: **full** (rows; no intent).
Functions: apply `0x830fa0`.

**Rows (server -> client).** Key id must be 1 (the key type is not checked).
| field | type | limits | meaning (I) |
|---|---|---|---|
| tag | u8 | == 1, else bad | |
| title | str16<=60 | - | title (log `server rules: the server's rules - "<title>", <N> characters`) |
| text | str16<=4000 | - | rules text |
End exact.

