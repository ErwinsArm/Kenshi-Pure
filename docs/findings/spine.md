# AnarchyKenshi.dll: the Spine state sync

- File: `AnarchyKenshi.dll` from mod v0.133.1, SHA-256
  `c3d2419d1c02b516552032e9037d29ec1a1b88661199ddac7775f601d97b51bb`, PE32+ x64, image base
  `0x180000000`. All addresses are RVAs in this file unless they are marked as Kenshi RVAs.
- Kenshi reference: the Steam `kenshi_x64.exe`, SHA-256
  `a596ab4e407c67b58599c54ffb32dc1bf2b64510cdebd3fa9359ef05a576aeb1` (read statically).
- Evidence: Ghidra 12.2 decompile of the whole DLL, `llvm-objdump` disassembly, strings, `.pdata`
  function bounds. Nothing was run. Labels: **M** = measured (decompile, disassembly, strings,
  xrefs, bytes), **I** = inferred. Ghidra `FUN_…` names and every function name given here are
  guesses. In the decompile, `HMODULE + N` is scaled by 4 (`sizeof(HMODULE__)`), so the byte offset
  is `4*N`.

## 1. What the Spine is (measured unless marked)

One mechanism carries ~48 keyed tables ("topics"). The same DLL contains the **server side too**: it resolves the
client exports (`AnarchyClientSpineTake` -> `0x19424c8`, `...SendIntent` -> `0x19424d0`, `...SendIntentLossy` ->
`0x19424d8`, `...SpineDropped` -> `0x19424e0`) and the host exports (`AnarchyHostSpineSend` -> `0x19424a8`,
`...TakeIntent` -> `0x19424b0`, `...Allowance` -> `0x19424b8`, `...Dropped` -> `0x19424c0`) in one block at
`0x58388..0x58510`. `0x1942128` = host handle (0 on a pure client), `0x1942058` = client handle.
Strings in the DLL speak of "the server", "viewers", "stand" (a test server), "judge", "Book" (server-side record store).

- Registry: `FUN_1805440b0` (RVA `0x5440b0`) registers topics; `FUN_18055ed70` (`0x55ed70`) calls it 48 times at start.
  Lookup by kind: `0x544440` (map at `0x19651e0`); per-kind on/off switch `0x544580` / `0x5445e0` (map `0x1965280`).
- Client tick `0x5478b0` (only when `0x1942128 == 0` and the connection state getter returns 2): drains
  `SpineTake(client, &kind, buf, cap, &len)` up to 0x200 messages per frame into dispatcher `0x541420(kind, buf, len, now)`.
  Then runs `0x5423b0` (upstream gather) and `0x541d70`.
- Server intake `0x540590`: `HostSpineTakeIntent(host, &player, &kind, buf, cap, &len)` under a time budget (0x2000 intents max per pass);
  server send helpers `0x53f850` (digest-only tell), `0x53fa00` (full/delta tell, chunks kept below ~0x7000 bytes of the 0x8000 Core limit),
  `0x547e50` (kind-1 notice).
- Per-topic virtual interface (vtable slot n = offset 8n). Measured by decoding every vtable:
  slot 1 `kind()` (a `mov eax,imm; ret`), slot 2 `name()` (string), slot 3 schema version byte, slot 4 gather period (float s),
  slot 5 server gather (viewer state -> rows), slot 12 (`+0x60`) server handler for a client intent (called from `0x540590` via `0x53d1a0`),
  slot 14 (`+0x70`) client apply row (key, reader) -> 0 ok / 2 bad, slot 15 (`+0x78`) client remove row, slot 20 (`+0xa0`) client notice
  (code, flag, text) from kind-1 op 7, slot 26 (`+0xd0`) "describe my bodies" (host-authored upstream), slot 31 (`+0xf8`) set host mode.
  Slot roles are *inferred* from their call sites (thunks `0x53d180..0x53d380`).

## 2. Wire format (measured)

Core carries `u16 kind` + opaque blob. **kind = topic kind** (below); **kind 1 = control**; kind 0 and 8, 34-38 unused (no topic).
Every blob, both directions, starts with a 5-byte header:

`u8 version` (topic slot 3; receiver rejects a different value: counter "SCHEMA-BAD") + `u32 seq` (LE, written as two `u16`).
Client->server seq = one process-wide counter `0x1948338`, +1 per message, any topic. Server->client seq = per (player, topic) counter.
The receiver does not check seq in the code read (it is skipped at `0x541420`, header = 5 bytes); it is for logs/acks (*inferred*).

Primitives (shared reader `{buf, len, ok, pos}`): u8 `0x543b90`, u16 `0x9f8d0`, u32 `0x543bc0`, str16(max) `0x543c50`,
writer u8 `0x5439a0`, u16 `0x5439f0`. Reads that overrun clear `ok`; a handler returning 2 = "bad".

**Key** (`0x543f90` reader, `0x543aa0` writer, text form `0x5435f0`): `u8 type` + payload.
type 1 `body:` = u32 entity id; 2 `place:(x,y,z)` = 4 x u32 (16 B, three fixed-point coords scaled by `*(float*)0xeba8bc` plus one more u32);
3 `name:` = u32; 4 `player:` = u32 player id; 5 `global:` = u32 (singletons use id 1).

**Server -> client blob** = header + ops until the blob ends (dispatcher `0x541420`; server writers `0x53fa00`, `0x53d540`):

| op | body | effect on the client's table |
|---|---|---|
| 1 | key, `u16 len`, bytes | upsert row; FNV-1a of the value is stored and an identical value is not re-applied; apply may be deferred ("applybudget" engine calls per frame) |
| 2 | key | delete row |
| 3 | - | start of a full tell: mark every row stale |
| 4 | - | end of a full tell: delete rows still stale |
| 5 | `u32 count`, `u32 hash` | digest of the whole table (hash = XOR over rows of a boost-style combine of FNV(key) and value hash, `0x53d380`); mismatch -> client sends a resync request (below) |
| 6 | - | clear table (all rows removed, reset hooks slot 21/22) |

**Resync request** (client -> server, kind 1, `0x5412b0`): `u8 1, u32 seq, u8 1, u16 topic`; rate-limited by a double at `0x1076ec8`
(seconds, value not read). Server handler (in `0x540590`, `kind == 1` branch) resets that topic's told state for that player so the next
tell is FULL ("retell").

**Kind 1 server -> client** (`0x547e50`, header `01 00000000`): op 7 notice `u16 topic, u32 code, u8 flag, u16 len, text<=200 printable`
-> topic slot 20 (used for "REFUSED - ...", status lines); op 8 `u16 topic, u8 mode` = switch the topic's author (`mode 1` = "the cell HOST authors the
bodies of its cells (G8.1): this client describes the bodies it holds and sends them up"; `0` = "the server authors it again") -> slot 31
(handler `0x540e20`, strings at `0x1211780..0x1211a40`).

**Client -> server intent** (`0x547b00`, lossy twin `0x547ce0` goes to Core's channel-2 export): header + topic-defined payload; the server
checks the first byte equals the topic version, then calls slot 12 `(player, payload)`.
**Upstream state ("host authored")**: `0x5423b0` sends blobs whose first byte is `version | 0x80`; server accepts it only if
`(byte ^ version) & 0x7f == 0` and routes to `0x53dd40` (the "judge": checks plausibility, keeps a per-host mirror). Used by topics
whose slot 26 is non-stub: **Work 7, Limbs 18, Splint 19** (*measured*); op layout inside upstream chunks not decoded.

Server tick (strings `0x1211598..0x1212d60`, *measured*): per viewer a gather every `period` seconds (per-topic overridable with the
console `spine period <topic> <s>`), signature-skip if unchanged, "delta" tell for topics that can (structure), digest on time,
per-player grid phase, budgets (`gatherbudget`, `applybudget`), counters `DIGEST-BAD`, `SCHEMA-BAD`, `MALFORMED`, `retells`, `intent-unknown`.

## 3. Topics

Columns: kind (u16), name (slot 2), RTTI class, vtable RVA, version (slot 3), period s (slot 4), roles. Roles: **I** = server has an intent handler
(slot 12 non-stub), **I!** = client sends it with an immediate kind constant at call sites (*measured*), **S** = server->client rows applied by the client
(slot 14 real), **H** = host upstream (slot 26), **N** = server sends kind-1 notices for it. The last column is what it syncs: *inferred* from the
class name, log strings and key/value layout unless it says measured.

| kind | name | class (RTTI) | vtable | v | per | roles | what it syncs |
|---|---|---|---|---|---|---|---|
| 2 | probe | ProbeTopic | `0x1212e68` | 1 | 2 | I S N | diagnostic/probe rows (strings "(unknown here)") |
| 3 | factions | FactionsTopic | `0x121f208` | 1 | 2 | S | faction table incl. renamed player factions; console `spine factions fake` (stand only) |
| 4 | relations | RelationsTopic | `0x121f358` | 1 | 2 | S | faction-to-faction relations |
| 5 | owntask | OwnTaskTopic | `0x12143c8` | 1 | 1 | I S N | work orders of own bodies at machines (strings "owntask: the server refused our word") |
| 6 | machine | MachineTopic | `0x1225198` | 2 | 1 | I S N | machines: craft queue, fuel, fill, farm ripeness, light flow, hydro; client commands craftadd/craftdel/press/fill |
| 7 | work | WorkTopic | `0x1237248` | 1 | 1 | S H | which body works at which machine seat; G8.2 host describes its bodies, server mirrors and judges |
| 9 | construct | ConstructTopic | `0x121eea8` | 1 | 5 | I! N | build requests: place / upgrade / dismantle, refusals (client sends kind 9 from 6 sites) |
| 10 | structure | StructureTopic | `0x121efc8` | 1 | 1 | S | placed structures (supports delta tell); client takes down gone ones |
| 11 | bodies | BodiesTopic | `0x1212f98` | 1 | 30 | (S stub: slot 14 returns 2) | body census, server-side only (no client apply code) |
| 12 | buildings | BuildingsTopic | `0x1213208` | 1 | 5 | S | town buildings ("built ... the server has it, this world did not") |
| 13 | groups | GroupsTopic | `0x1226218` | 6 | 5 | I! S N | player groups / citshop / friendly-fire flag; requests answered within 15 s |
| 14 | bodyfaction | BodyFactionTopic | `0x12254e8` | 1 | 4 | S | faction per body |
| 15 | research | ResearchTopic | `0x1233728` | 1 | 1 | I S N | research progress and choice (role check "ask the leader") |
| 16 | slave | SlaveTopic | `0x1235828` | 2 | 1 | I S | slavery state, cages, escape orders |
| 17 | bounty | BountyTopic | `0x12363a8` | 1 | 1 | S | bounties / wanted bodies |
| 18 | limbs | LimbsTopic | `0x12383e8` | 1 | 2 | I! S H N | limb/prosthetic state per body; client sends kind 0x12 intent; host upstream; "limbs cut" stands |
| 19 | splint | SplintTopic | `0x12389b8` | 1 | 2 | S H | splint patches on bodies; host describes, server judges |
| 20 | mods | ModsTopic | `0x124b878` | 1 | 5 | I! S N | mod list + content SHA-256 check (`modcheck`, `modcheck_data`); client intent kind 0x14; verdict can disconnect |
| 21 | stores | StoresTopic | `0x1242268` | 1 | 2 | I! S N | storage/chest inventories and personal chest ("stores: player ... layout") |
| 22 | requests | RequestsTopic | `0x1243b78` | 2 | 1 | I S N | player-to-player requests: duels (first hit / knockout), cancel, stop |
| 23 | vote | VoteTopic | `0x1243e18` | 1 | 0.5 | I S N | "skip the pause" vote |
| 24 | wallet | WalletTopic | `0x1244fe8` | 1 | 1 | I! S N | the faction wallet (cats); client intent kind 0x18 |
| 25 | trade | TradeTopic | `0x1248778` | 1 | 0.5 | I! S N | trader windows (3-minute turn per trader), stock rebuilt from server |
| 26 | dialog | DialogTopic | `0x1239378` | 1 | 5 | S | dialogue packages for squad bodies |
| 27 | stealth | StealthTopic | `0x1239688` | 1 | 0.25 | S | per-body "being noticed" level (see sec. 4) |
| 28 | locks | LocksTopic | `0x123f718` | 1 | 1 | I! S | lock picking / lock levels; client intent kind 0x1c |
| 29 | serverinfo | ServerInfoTopic | `0x123f938` | 1 | 5 | S | server slot count, attack slots (how many enemies may hit one body) |
| 30 | spawn | SpawnTopic | `0x1241188` | 1 | 1 | I! S | where a joining player's character is placed ("the trip to ..."); intent kind 0x1e |
| 31 | weather | WeatherTopic | `0x12539e8` | 2 | 0.5 | S | weather effect sets and puts (acid rain) |
| 32 | cellhost | CellHostTopic | `0x1260ee8` | 6 | - | I! S (lossy) | cell-host election and the host's body snapshots; judge refuses implausible steps; sent over the lossy export |
| 33 | squads | SquadsTopic (`SpineSquadsIn`) | `0x126c7e8` | 4 | - | I! S N | squad membership/records/rebuild (17 send sites, some lossy) |
| 39 | base | BaseTopic | `0x11c0a18` | 2 | 1 | I! S N | machine reports and "lead" reports from clients, zone host hands, authored zones |
| 40 | corpses | CorpsesTopic | `0x11c7eb8` | 1 | 1 | I! S | corpse records, rob/calm/pin/limbs commands; intent kind 0x28 |
| 41 | cages | CagesTopic | `0x11c8968` | 1 | 1 | S | cage book / captured members |
| 42 | ownheal | OwnHealTopic | `0x123a338` | 1 | 5 | I! S | own-healing agreement (client keeps local bed/first aid); intent kind 0x2a, also lossy |
| 43 | ptrade | PlayerTradeTopic | `0x123afd8` | 1 | 1 | I S N | player-to-player trade |
| 44 | skillmem | SkillMemoryTopic | `0x123b948` | 1 | 2 | S | skill memory / XP multiplier (x5 below level 30 ... x1.5 below 100) |
| 45 | fightxp | FightXpTopic | `0x123cda8` | 1 | 1 | S | fight XP caps (sparring cap, vs players cap) |
| 46 | heldby | HeldByTopic | `0x123cf58` | 1 | 5 | I! | client tells the server which record it carries (kind 0x2e); no client apply |
| 47 | townlayout | TownLayoutTopic | `0x123eaf8` | 1 | 2 | I! S | town building layout, first named is the truth; intent kind 0x2f |
| 48 | integrity | IntegrityTopic | `0x123bf78` | 1 | 5 | I! S | integrity census / stand-word check; intent kind 0x30 |
| 49 | acttrace | ActTraceTopic | `0x123d088` | 1 | 3600 | I! N | action trace upload, kind 0x31 (client -> server only) |
| 50 | wear | WearTopic | `0x1238ba8` | 1 | 2 | S | worn/damaged armour wear ("repair-kept", robot parts) |
| 51 | acidrain | EnvTopic | `0x1239048` | 1 | 5 | I | client reports wet/dry own bodies; instance with kind in a member (`0x18198efb8 = 0x33`) |
| 52 | acidenv | EnvTopic | `0x1239048` | 1 | 5 | I | second instance (`0x18198eeb8 = 0x34`), same class |
| 53 | shots | ShotsTopic | `0x123d338` | 1 | 0.1 | I! S | projectile shots; intent kind 0x35 |
| 54 | rank | RankTopic | `0x1245108` | 1 | 5 | I! S | faction ranks from dialogue rewards; intent kind 0x36 |
| 55 | rules | RulesTopic | `0x1240218` | 1 | 2 | S | server rules text shown to players |
| 1 | (control) | - | `0x540e20` | - | - | - | resync request (C->S) / notices and author switch (S->C) |
| - | (bases) | `Topic@Spine@Anarchy` | `0x11c08f8` | 1 | 1 | - | abstract base; `BaseTopic` is kind 39, not an alias |


47 topic classes, one of them (`EnvTopic`) registered twice, = 48 `FUN_1805440b0` registrations (measured count).

## 4. Representative layouts (client apply code, *measured*; field meanings *inferred*)

- **rules 55** (`0x830fa0`): key `global:1`; value = `u8 1`, `str16 <=60`, `str16 <=4000`, then end of value (exact). Client shows "server rules: ... (revision N)": field 1 is the revision/title, field 2 the text.
- **wallet 24** (`0x8705c0`): key `global:1`; value = `u32 balance` (must be >=0), `u32 second` (kept as `0x1949eec`); balance shown as "N cats".
- **vote 23** (`0x868000`): key `global:1`; value = `u32 a`, `u16 b`, `u16 c`, `u8 flag` (stored at `0x1949e78..e84`); "players voted to skip".
- **stealth 27** (`0x7c4130`): key `body:<id>`; value = single `u8` < 3 (noticed level); anything else counts as bad.
- **weather 31** (`0x936730`): key is a u32 id (type not checked): ids `0x1000000..0x1ffffff` = effect sets (handled by `0x9368e0`);
  ids `0x10000..0x10fff` = effect puts: value `u16, u32 (2xu16), str16 <=0x40`, nothing may remain.

Where a handler returns 2 the client counts "bad row"; server rows that the client cannot place yet are retried (state 2 "hold").

## 5. Spine versus the fixed messages (inferred from `protocol.md` and the above)

Fixed Core messages are the high-rate, engine-neutral state: Position/Snapshot/Spawns/Despawns/Vitals/Hits/Combat/Chat/Orders (`protocol.md`).
The Spine carries slower, rule-heavy world and economy state that needs per-viewer filtering and recovery: factions, research, machines,
structures, stores, wallet, trade, mods, rules, weather, limbs/splint/wear on top of the Vitals. Pattern: **intent up** (client asks, server
judges, replies as a notice or as the changed row), **authoritative state down** (keyed rows, full tell then deltas, digest to detect drift),
**resync by request** (kind 1). The G8.1/G8.2 exception: for cells it hosts a client is the author of bodies/work/limbs/splint and sends them up
(`version|0x80`), the server keeps a mirror and a judge. Lossy channel only for CellHost, OwnHeal and a few Squads sends.

## 6. Open questions

- Upstream (host) chunk layout `version|0x80` and the judge `0x53dd40`; what `FUN_18053a200` emits.
- Is `seq` ever checked or echoed (acks)? Not seen in the receive paths above.
- Per-topic row formats other than the five in sec. 4 (Squads, Limbs, Machine, Stores, Groups are large; gather = slot 5 functions in the vtable table).
- Which topics use `AnarchyHostSpineAllowance` (`0x19424b8`, called at decompile lines near `0x5449a0`/`0x5455d0`) and how the allowance is shared.
- Calls into Kenshi: the hook tables are signature records (names/patterns, registered at `0xcd8d20`), but the DLL's own `DAT_1965xxx` pointers are internal;
  I could not map topic code to named Kenshi functions from the tables, only from strings and field offsets.
- Kinds 0, 8, 34-38: no topic found; may belong to removed features or a different server build.
- Exact `period`/resync throttle constants at `0x1076ec8`.
