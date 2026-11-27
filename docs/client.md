# Client: how KenshiPure starts and finds the game

Labels: **M** measured, **D** documented, **I** inferred. Plan and reasons: [plan-own-loader.md](plan-own-loader.md).
Not yet run in game in this form (built and checked statically; game runs pending).

## Start: KenshiPureLoader.dll (Ogre plugin)

- `scripts/install-client.ps1` puts `KenshiPureLoader.dll` and `KenshiPure.dll` in `<Kenshi>\mods\KenshiPure\`
  and the line `Plugin=mods/KenshiPure/KenshiPureLoader` in `Plugins_x64.cfg`, right after
  `Plugin=RE_Kenshi` if that is there, else before the first `Plugin=` line. RE_Kenshi is not needed; the
  old RE_Kenshi start (`RE_Kenshi.json`, `KenshiPure.mod` in the mod list, `startPlugin`) is gone.
- Ogre calls `dllStartPlugin` while it builds its Root, before the render system and window exist. The
  loader (`loader/KenshiPureLoader.cpp`) pins itself, logs pid, exe path and the exe's SHA-256 to
  `KenshiPureLoader.log`, stays off if `Plugins_x64.cfg` switches AnarchyKenshi on (read from the file:
  AK may load after us), arms the display fix, then loads `KenshiPure.dll` and calls
  `kpStart(const KpHost*)` (`client/KpHost.h`: version 1, plain C, carries the hash and exe path).
- Kenshi imports Ogre's `Root` constructor and `Root::initialise`, not `Root::createRenderWindow` (**M**,
  imports of both exes); `Root::initialise` creates the window and the swap chain.
- Build: `scripts/build-plugin.ps1` (VC++ 2010, `/MD`, MinHook 1.3.4 compiled in). Neither DLL links or
  imports KenshiLib; KenshiLib 0.5.0 provides headers (declarations) and its 1.0.65 address map (input to
  `scripts/sites.mjs`) only.

## Address layer

Every game address and field offset KenshiPure uses is a row of `client/sites.tsv`, made and proven by
`scripts/sites.mjs` from the two exes on this PC: Steam 1.0.68 (`kenshi_x64.exe`, `a596ab4e…`) and
RE_Kenshi's 1.0.65 copy (`RE_Kenshi\Kenshi_x64.exe`, `504b362c…`).

| Kind | 1.0.65 value from | Proof in 1.0.68 |
|---|---|---|
| `code` | KenshiLib's map (mangled name) | masked pattern from the 1.0.65 function start, unique in both `.text`; shift check |
| `global` | KenshiLib's map | an instruction referencing it RIP-relatively (anchor), matched in 1.0.68; target read there |
| `field` | KenshiLib header offset (`// 0x.. Member`) | an instruction in a method of the class accessing `this->field` (`this` tracked from `rcx`), matched in 1.0.68; displacement read there |
| `vslot` | KenshiLib header vtable offset | the class's vtable found through RTTI in both exes; the slot leads (through the incremental-link `jmp` thunk) to the function matched as for `code` |
| `patch`, `callsite`, `data` | format only; no rows yet (`sites.mjs` refuses to add them automatically) | |

- Columns: name, kind, module (`exe`), need (`required` or `optional:<feature>`), sym, one value per build
  (`value` or `value@anchor`), pattern (`??` = masked), site (offset of the anchor in the pattern), read
  (`rip:<disp>:<len>`, `disp:<off>:<size>`, `disp0`, `vt`), expect (patch bytes), evidence.
- Masking (iced-x86 1.22.0): rel32 branch/call targets and RIP-relative displacements; if a pattern has no
  1.0.68 hit, all memory displacements too. Patterns grow (16–64 bytes) until unique in both exes.
- Shift check: the 4 nearest KenshiLib functions on each side are matched in 1.0.68 (±0x10000). Accepted
  when the row's shift equals one side's agreed shift and the order is kept, or, for a function start
  between two blocks, when its whole body is identical (every instruction, moving operands masked). The
  plan's "equal to the neighbours' block shift" alone is too strict: `GameWorld::_NV_mainLoop_GPUSensitiveStuff`
  moved +0xb90 between neighbours at +0xb40 and +0xba0 (**M**), body identical (368 instructions).
- `node scripts/sites.mjs check` re-proves every row (~15 s); `build-plugin.ps1` runs it whenever
  `sites.tsv` changed. `sites.mjs vias Class::member` lists methods usable as field anchors.
- Result (**M**): 26 code rows, 1 global (`ou`: the GameWorld object itself, referenced by `lea rcx,[rip+…]`),
  24 fields, 6 vtable slots. 56 rows proven in both builds; every field offset is equal in 1.0.65 and
  1.0.68. `InventorySection::SectionItem::item` has no anchor (no method reaches it through `this`), so
  it is 1.0.65-only and the probe's `inventory` command is off on 1.0.68. Both critics found, with their
  own scripts, every code row's whole body identical in the two builds.
- Not covered by the proof: layouts of containers compiled into both from the same headers (`lektor`,
  Boost `unordered_set`, `std::string`, `std::vector` element size), struct sizes, and callees of the proven
  functions (call targets are masked). Assumed equal (same compiler and headers; **I**).

At run time (`client/Sites.cpp`) patterns are checked in the exe **file** (mapped read-only): another mod
may already have hooked a function in memory, and the file's SHA-256 names the build.
- Known build: each row's pattern at the address recorded for that build; the operand read there must
  equal the table value. Unknown build: pattern search, unique hits, longest ordered subset, shift near a
  neighbour's; vslot rows unresolved; `-write` features off; and KenshiPure installs **no game hook** (a
  wrong hit would patch the wrong function), so an unknown build gets only the display fix.
- `required` row unresolved → KenshiPure inactive; `optional:<feature>` → that feature off (the log lists
  rows and features). `build\client\sites-test.exe` (built and run by `build-plugin.ps1`) runs this
  resolver on both exe files: all 57 / 56 rows resolve to the table values; in unknown-build mode 50 of 57
  rows are found, all equal to the table (vslot and header-only rows are not searched) (**M**).
- Calls use KenshiLib's declarations; the generated `kp_thunks.asm` defines each mangled name as
  `jmp [slot]`; a slot starts at a trap stub that logs the row and raises an exception for the caller's
  `__try`, so nothing jumps to 0.
- Fields only through `KP_FIELD(ptr, Class, member)` (`client/Sites.h`): it compiles only for members with
  a row (`FieldRow` specialisation) and uses the declared type with the per-build offset; an unresolved
  field holds a non-canonical offset (a read faults into `__try`). The generated table `static_assert`s
  that each 1.0.65 offset equals the header's. `scripts/check-fields.mjs` fails the build on raw access to
  a Kenshi data member (variables of Kenshi class types, chained call/cast/index results, `auto`); typedef'd
  types and pointer arithmetic are not caught.
- Writes: `sites.mjs gen` refuses a row of a `-write` feature that is not proven in every build.
- Hooks: `client/Hooks.cpp`, one MinHook detour per function, listeners behind it (`OnFrame`).

# Client: live game probe

Source: `client/Probe.cpp`, `client/Probe.h`; MCP side: `mcp/kenshi-probe.mjs`. Setup: `docs/setup.md`.
Measured in game on Kenshi 1.0.65 Steam (the earlier KenshiLib-linked build): all commands answered with plausible
values; `paused` followed the in-game pause, the position stayed fixed while paused and changed by
tens of units per 2 s while the character walked, and a newly started game showed its new character.
`nearby` listed the Trade Ninja guards and barman of a nearby bar, Garru (faction Herbivore) and
outlaws with matching faction names; `unconscious` turned true when the user's character was knocked out.

## How it works

- The plugin starts a TCP server on **127.0.0.1 only** (default port 47800, `probe_port=N` in
  `mods\KenshiPure\KenshiPure.cfg`, 0 = off) on its own thread (Winsock, `ws2_32.lib`).
- One JSON object per line in, one JSON line out, one client at a time, lines up to 4 KB.
  Requests that are not `{"cmd":...}` are refused (a web page cannot do anything useful with it).
- `status`, `characters` and `nearby` touch the game: they go into a one-slot queue and the frame hook
  (`MainLoopHook`, game thread) answers them after the game's own frame. No game call runs on the
  server thread. If no frame comes within 3 s (menu, loading) the reply is an error.
- Game reads are wrapped in `__try` and copy only plain data (as `SampleAbi` does), so a bad pointer
  gives an error reply instead of a crash in Kenshi.
- `log_tail` is answered on the server thread from an in-memory ring of the last 500 log lines
  (no game access).

Second run (same game, character Gecko), all measured:

- `health`: `percent` = (flesh - stun) / maxHealth on all 7 parts (Chest: (12.17 - 25.24) / 100 = -0.1307);
  undamaged parts have stun 0. So `stun` is stun damage that recovers, and `heal` with `stun: zero`
  is a full heal: afterwards every part read flesh 100, stun 0, percent 1, blood 75.9 of 75.9.
- `inventory` listed equipped sections (hip, back, head, shirt, armour, legs) and the main bag; `find_items`
  "bread" gave `Bread` / `1946-gamedata.base` (309 item templates, mods included).
- `spawn_item` Bread x1 landed in the bag. With a full bag, Samurai Armour x1 and Bread x5 came back
  "refused" but lay on the ground next to the character (`addItem` drops on fail). Bread x5 produced a stack
  of 5 **and** 4 single breads (9 in all; cause unknown). Katana (category weapon) failed: the factory
  returned nothing, likely because `createItem` needs the weapon's manufacturer/material data (inferred).
- `teleport` while paused replied ok but the character stayed in place; unpaused, `dx 200` moved x by
  exactly 200 and the game put y back on the ground; absolute x/y/z also worked.
- `knockoutTimer` is non-zero (0.7, 10.8) on some knocked-out NPCs and 0 on others; `money` was 1000 for the
  player and a Vagrants thug, 0 for Holy Nation Outlaws (still not clear whether per faction).

## Commands

| Request | Reply |
|---|---|
| `{"cmd":"status"}` | build, kenshiVersion, frames, frameThread, sameWorld, paused, frameSpeed, timeStamp, playerInterface, playerCharacters |
| `{"cmd":"characters"}` | `characters` (player-owned, max 64): name, faction, x, y, z, player, dead, unconscious, money, blood, hunger, knockoutTimer |
| `{"cmd":"nearby","radius":3000,"limit":30}` | `loaded` (all characters the game updates), `inRadius`, `characters` with the same fields plus `distance` from our first character, nearest first |
| `{"cmd":"health","who":"all"}` | per body part: flesh, stun, maxHealth, hpMult, percent; blood, maxBlood, hunger |
| `{"cmd":"inventory","who":"first"}` | items per inventory section with quantity |
| `{"cmd":"find_items","query":"bread","category":"item"}` | item templates: name and string ID |
| `{"cmd":"heal","who":"all","stun":"keep"}` | **write**: parts to max health, blood to max, bleeding 0; `stun` keep, max or zero |
| `{"cmd":"teleport","who":"selected","dx":200}` | **write**: absolute x/y/z or relative dx/dy/dz via `CharMovement::_setPositionAndTeleport`; lost while the game is paused |
| `{"cmd":"spawn_item","name":"...","quantity":1}` | **write**: `RootObjectFactory::createItem` + `Inventory::addItem`; weapons fail, see below |
| `{"cmd":"log_tail","lines":50}` | `text`: last lines (max 500) |
| `{"cmd":"ping"}` | `pong` |

Sources (KenshiLib 0.5.0 headers, addresses and offsets through `client/sites.tsv`; values confirmed by the run above
unless noted):

- own characters: `GameWorld::player->playerCharacters` (member read); all loaded characters:
  `GameWorld::getCharacterUpdateList()` (Boost `unordered_set`, iterated in `__try`);
- name: virtual `getName()`; position: virtual `getPosition()`; faction: `getFaction()->getName()`;
  `isPlayerCharacter()`, `isDead()`, virtual `getMoney()`;
- `getMedical()`: `isUnconcious()`, fields `blood` (+0x70), `hunger` (+0x60), `knockoutTimer` (+0xA0).
- Open: `money` was the same (8377) for every Trade Ninja, so it is likely the faction's money, not the
  person's (inferred). `knockoutTimer` stayed 0 on the knocked-out player but was non-zero on some knocked-out NPCs (meaning unclear). The units
  of `blood` and `hunger` are not known (Gecko 46-48 after a fight, NPCs 80-130; hunger about 1.6-3).

`Probe.cpp` sets `BOOST_ALL_NO_LIB` and `BOOST_SYSTEM_NO_DEPRECATED`: `Faction.h` pulls in Boost.Thread
headers, which otherwise want Boost libraries we do not build.

## Safety

Read-only by default. `heal`, `teleport` and `spawn_item` change the single-player game and answer only
when `mods\KenshiPure\KenshiPure.cfg` holds `probe_write=1` (written by `install-client.ps1 -ProbeWrite`,
removed by a plain install or `-Uninstall`). Port bound to loopback; other programs on the same PC can
still connect (no authentication), so keep writes off when not testing.

# Client plugin: display fix

Source: `client/Display.cpp`. Kenshi can start full screen in a smaller, stretched mode until Alt+Enter
(seen by the user: first frame 1680x1050 on a 1920x1080 screen, `kenshi.cfg` asking for 1920x1080 full
screen). Cause (**M**, AnarchyKenshi's log): the game creates its swap chain with
`DXGI_MODE_SCALING_STRETCHED`, and DXGI then picks a smaller mode; AK sets it to `UNSPECIFIED` in a
`CreateSwapChain` hook ([findings](findings/AnarchyKenshi.dll.md), section 5).

Early fix (`loader/DxgiFix.cpp`): when `kenshi.cfg` says `Full Screen=Yes` and `KenshiPure.cfg` has no
`display_fix=0`, the loader hooks `Ogre::Root::initialise`. There, before the original runs, Ogre's D3D11
render system has loaded `dxgi.dll` (the only module of the start that imports it, **M**), so the loader
never loads `dxgi.dll` itself and a ReShade `dxgi.dll` in the game folder still loads first (ReShade 5.4.2
loads into the RE_Kenshi copy, **M**, `ReShade.log`). It creates a factory through the game's `dxgi.dll`
and through Windows' one, detours vtable slot 10 (`CreateSwapChain`) of each, and in the first call the
new swap chain's slot 14 (`ResizeTarget`); both set `Scaling = UNSPECIFIED` on a copy of the description
and log before/after (`dxgi: ...` in `KenshiPureLoader.log`). Not yet run in game.

Fallback (`client/Display.cpp`, in KenshiPure): a worker thread waits until the game window is in front
and answers messages (3 checks, 0.5 s apart). If the client area
differs from the desktop mode saved in Windows, it minimizes the window and restores it after 0.7 s.
Log lines `display: ...` show window, client, monitor and display-mode sizes and DPI. Not yet run in
game.