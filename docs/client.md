# Client: how KenshiPure starts and finds the game

Labels: **M** measured, **D** documented, **I** inferred. Plan and reasons: [plan-own-loader.md](plan-own-loader.md).
Run in game (**M**, user runs): with RE_Kenshi on 1.0.65 (57/57 rows) and without it on Steam 1.0.68 (56/57,
`inventory` off): loader and plugin started, frame check passed, DXGI STRETCHED -> UNSPECIFIED, picture full
screen without Alt+Enter, probe `status`/`characters` answered on 1.0.68.

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

# Client: own pose (M1)

Source: `client/Pose.cpp`, pure helpers `client/PoseMath.h` (unit tests `client/test/PoseMathTest.cpp`,
run by `build-plugin.ps1`). Run in game (**M**, the user's run on 1.0.65 with RE_Kenshi, character Gecko,
1 owned): stand, walk with move orders, turn, game speed ×5, pause, menu and save load; no fault, the
game kept running. Second PC (**M**, a tester's log, same build 0.0.3-m1): Steam 1.0.68 without RE_Kenshi,
64/65 rows (`inventory` off), ~4 min of walking with the same behaviour (speed ~70 while walking, yaw =
facing, 0 standing); no fault.
- First sample after a load can be half set up (**M**, second PC): destination and path destination
  0/0/0 (so `walking` 1) and a yaw (147.5) that does not match `facing` (0, 0, 1); the next sample, 1 s
  later, was consistent. M3 should drop poses until destination is non-zero or skip the first sample.

- Every 100 ms on the frame thread (after the game's frame): the local character is the player's
  selected character if it is one of `PlayerInterface::playerCharacters`, else the first of them; none
  (menu, loading) → `pose: no own character`. A change of character gets its own log line. Only one
  owned character was tested.
- Read (all in `__try`, plain copies; rows of feature `optional:pose` plus the probe's
  `selectedCharacter`, `hand::getCharacter`, `Character::getMovement`, `getName`/`getPosition` vslots;
  any unresolved → `pose: off`):

| Value | Source | What the run showed (**M**) |
|---|---|---|
| position | virtual `Character::getPosition()` | x/z follow the walk; y settles onto the ground after a load (1542.8 → 1536.2) |
| orientation, yaw | virtual `RootObject::getOrientation()` (slot 0xC0, not overridden in Character's vtable; copies 4 floats at `this+0xB0..0xBC`); yaw = heading of the rotated +Z axis, `atan2(2(xz+wy), 1-2(x²+y²))`, 0 = +z, 90 = +x (AK's formula) | (sin, cos) of yaw equals `facing` x/z to 2 decimals in every sample checked (74.4°, -158°, -17.9°, 91.7°), and the direction of `motion` while walking |
| facing | `AbstractMovementBase::direction` 0xD0 | unit vector; small y on slopes |
| velocity | `AbstractMovementBase::currentMotion` 0xA8 | 0 standing; while walking a vector in world units per second whose length equals `currentSpeed`; at game speed ×1 the logged position moved 71.9 units in a second at speed 72.1 |
| speed | `AbstractMovementBase::currentSpeed` 0xB8 | 0 standing, ~72 walking (Gecko), lower while starting or turning |
| moving | `AbstractMovementBase::currentlyMoving` 0x24 | 1 exactly while speed > 0 |
| wanted speed | `AbstractMovementBase::desiredSpeed` 0xBC | 45 after a load, 999 after the first move order (meaning of 999 **I**: no limit) |
| destination, path destination | `destination` 0xDC, `pathDestination` 0xE8 | both equal the clicked target during a move order and the position when standing; never different in this run |
| walking | x/z distance² from position to path destination > 4 (AK's rule) | lags `moving`: 1 for one sample after arriving 3 units short of the target; `moving` is the better flag |

- While paused every value stays frozen (`paused` in the line). At ×5 speed the position moved faster
  than `speed` per real second, so speed and velocity are per game second (**I**, short walks only).
- Log: once a second `pose: <name> pos … yaw … facing … motion … speed … want … moving … walking … dest …
  path … step/s …`; `step/s` is the x/z distance between two logged samples per real second. Non-finite
  or huge values (> 1e7) skip the sample (logged at most every 10 s).
- Each sample carries an `epoch` that goes up when the local character changes, comes back after
  "no own character", or after a gap of more than 2 s between samples (a load stall); the network
  side uses it to drop half-set-up samples.

# Client: networking and the core DLL (M3)

Status: built, tested without the game, and run in game (**M**, the user's run on 1.0.65 with
RE_Kenshi, build 0.0.4-m3, `kp-server --local --log-poses` + `kp-bot` on the same PC, character Nei):
- Welcomed 0.24 s after the plugin start, before the first frame; the ~19 s from start to the first
  frame (the frame thread does not run) kept the connection.
- `KenshiPure.log` logged the bot's circle once a second (`net: remote bot (1) … age ~90–100 ms`);
  the server logged the user's walk (`pose: player 2 (player) …`, moving 0/1 and paused as played).
- A save load from the Escape menu (~6 s without own character): no `connection lost`, the same
  player id; after each load 1 and 2 samples were dropped before sending, and the first logged
  samples were already consistent (destination = position, yaw = facing), so the half-set-up
  sample was not seen in this run. A 60 s load stall was not seen in game (only in the test).
- Game exit: the server reported `player 2 left (timed out)` ~7 s later (no `dllStopPlugin`, below);
  RE_Kenshi's known first-chance access violation at exit (`RE_Kenshi.dll+0x381e9`) came again.

**Pieces**
- `KenshiPureCore.dll` (`net/core/Core.cpp`): MSVC 14.44, C++20, static CRT, built by
  `scripts/build-net.ps1` (which checks it exports only `kpCoreGetApi` and imports no C runtime DLL).
  It holds the protocol codec (`net/proto`) and `NetClient` (`net/client`) and never sees a game pointer.
- Interface `client/KpCore.h` (version 1): plain C, POD only. `kpCoreGetApi(version)` returns a table:
  `start(config)` → core (starts the network thread; no `DllMain`, no thread before `start`), `stop`,
  `setLocalPose`, `takeEvents`, `takeRemotePoses`, `status`. Every call takes the core's mutex only
  for a copy; the mutex is never held across ENet calls or the log callback.
- `client/NetFrame.{h,cpp}`: the frame-thread side, compiled by **both** VC++ 2010 (into
  KenshiPure.dll) and MSVC 14.44 (into the test), so the test runs the plugin's own code. It reaches the
  game only through `IGameAccess` (local pose, log; spawn/move/remove are stubs for M4/M5).
- `client/Net.cpp`: the game adapter. Reads `KenshiPure.cfg`, loads the core from the plugin folder
  (`LoadLibraryExW` with the full path from `PluginDir()`), runs `NetFrame` each frame after
  `PoseOnFrame`. Any problem (no `host=` line, no core DLL, other core version, bad config) logs
  `net: off (...)` and leaves the rest of the plugin as it was.

**Config** `<Kenshi>\mods\KenshiPure\KenshiPure.cfg` (key=value, `#` comments): `host` (no line or empty
= networking off), `port` (default 27850), `name` (default `player`; 1–32 bytes, no control
characters, else off). `install-client.ps1` adds `host=127.0.0.1`, `port=27850`, `name=player` when
missing and keeps lines already there (another server, another name).

**Threads**
- The network thread (priority above normal) is the only one touching ENet. It services ENet every
  ≤ 2 ms, connects, sends Hello, and while welcomed sends each new local pose once.
- States: waiting → connecting (deadline 10 s for Welcome) → welcomed. A failed attempt backs off 1,
  2, 4 … 30 s; a refusal (Reject) waits 60 s; a lost connection retries after 1 s. The first failure
  of a row is logged, then one summary line a minute; a loss or a refusal is always logged. A failed
  attempt disconnects at once (`enet_peer_disconnect_now`), so the server keeps no ghost peer.
- The frame thread: each frame `NetFrame` takes the latest sample (Pose.cpp samples at 10 Hz) and
  hands each new one to the core. After an epoch change it drops the first sample, then samples whose
  destination is 0/0/0, for at most 2 s (then sends anyway and says so). Non-finite values (e.g. a
  yaw without heading) are dropped. No pose while there is no own character: the server keeps the last
  one and its age grows. Velocity = `currentMotion`, moving = `currentlyMoving`, paused = `isPaused`.
- Each frame it drains events (join and leave logged) and the remote poses (latest per announced
  player, at most 64), each player's pose logged at most once a second:
  `net: remote <name> (<id>) pos … vel … yaw … moving … age … ms`.
- Exit: the loader's `dllStopPlugin` calls KenshiPure's `kpStop` (leave + stop, at most 2 s; the
  thread is left to the process exit if it hangs). Kenshi does not seem to call `dllStopPlugin`: no
  `KenshiPureLoader.log` holds its line, 4 game exits including the M3 run (**M**); so the server
  reports a closing player as timed out after ENet's 5–30 s (~7 s in the M3 run).

**Load stalls (plan G14)**: the game's frame thread blocks ~60 s while loading; the network thread
keeps servicing ENet meanwhile, so neither side's ENet timeout (5–30 s, protocol-ours.md "Timeouts")
is reached. Shown without the game (**M**, `kp-core-test`), not yet in game.

**Test** `net/test/CoreTest.cpp` (`kp-core-test`, CTest, ~62 s; **M**, passes on this PC, 52 checks):
the core DLL loaded by `LoadLibrary` through its C table, `NetFrame` with a fake game, the real
`RelayServer` and an observer bot each on their own thread. Checks: bad configs refused, cfg parsing;
half-set-up samples never reach the observer; poses both ways; a 60 s sleep of the frame thread keeps
the same player id and no leave, the observer sees our pose's age reach ~60 s, then fresh again,
while an unserviced client beside it is timed out by the server (so ENet's timeout is real); a core
aimed at a closed port fails and retries; server gone → "connection lost" → reconnect with a new
id; stop leaves on purpose (server logs "left") in a few ms.

**In-game check**: double-click `start-test-server.cmd` (server `--local --log-poses` and bot in two
windows, logs in `build\net\kp-server.log`, `kp-bot.log`), then play; `KenshiPure.log` shows
`net: welcomed` and `net: remote bot …`, the server log `pose: player … (player)`.

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
| `{"cmd":"heal","who":"all"}` | **write**: parts to max health, blood to max, bleeding 0; stun damage to 0 (`"stun":"keep"` leaves it) |
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

# Client: cheat window (the user's testing tool)

Separate, optional `KenshiPureCheats.dll` (sources `client/cheats/`; host side `client/CheatHost.cpp`,
interface `client/CheatApi.h`). Installed only with `install-client.ps1 -Cheats` (a plain install removes
it); without the file KenshiPure logs `cheat: not installed` and nothing changes. Built and compile-checked;
**not yet run in game**.

- KenshiPure loads the DLL in `kpStart` if it is next to it, calls `kpCheatStart` (exe path and hash) and
  `kpCheatFrame` from its frame listener. The DLL resolves its own copy of the address table (same
  `sites.tsv`; known builds only), installs no hooks and logs to `KenshiPureCheats.log`.
- Window: drawn with Kenshi's own GUI library MyGUI 3.2.3 (the game's `MyGUIEngine_x64.dll`, SHA-256
  `fea6f1f935703812485bd6606ff4d3ceea172ece131ed4c6dcbf7a7e25b083a8`), so it shows inside the game like
  Kenshi's own windows, also in full screen. Headers and import library from KenshiLib 0.5.0 (declarations
  only); the build checks that all 37 MyGUI symbols the DLL imports are exported by the game's DLL. That
  KenshiLib's MyGUI headers match the game's build in class layout is **I** (a critic found export-name
  differences elsewhere in the library).
- Templates and layer from Kenshi's own layouts (`data/gui`): `Kenshi_WindowCX`, `Kenshi_Button1`,
  `Kenshi_TextboxStandardText`, `Kenshi_ComboBox`, `Kenshi_EditBox`, `Kenshi_WordWrap`, layer `Window`.
  Created once MyGUI, the layer and all six names exist; every MyGUI call runs in `__try` and a failure
  switches the window off for the run (logged).
- Content: the selected character's name; buttons Heal, Damage, Knock out, Teleport to camera; an item box
  (list Bread, Katana, Samurai Armour or the `cheat_item=` lines of an optional `KenshiPure-cheats.cfg`;
  any name or string ID can be typed), an amount box (1–100) and Create item; the last result. Close
  button hides the window, Ctrl+Shift+C shows/hides it. Clicks only queue the action; the frame listener
  runs it on the game thread.
- Actions (`client/cheats/CheatActions.cpp`), on the selected own character:

| Button | Action |
|---|---|
| Heal | every part to max health, stun damage 0, blood to `getMaxBlood`, bleed rate 0 (the probe's measured full heal), knockout timer 0 |
| Damage | each non-robotic part loses 30 % of its max health, never below -50 % (margin to death: **I**) |
| Knock out | torso and head parts get stun damage so their health reads -20 % (stun recovers), and `MedicalSystem::knockoutTimer` is raised to 20 |
| Teleport to camera | `Character::teleport` to `GameWorld::getCameraCenter` (that it is the look-at point, not the eye: **I**; the log shows both) |
| Create item | the typed item, N single adds (inventory full → on the ground) |

- Knock out (**M**, 1.0.65 disassembly): `MedicalSystem::reassessCollapseMode` (0x648890) sets
  `unconcious` (`+0x161`) when `knockoutTimer` (`+0xA0`) is above a threshold, among other triggers
  (blood, a part's collapse stage, hunger). It runs from the periodic medical update, not every frame, so
  the fall may come a moment later; the timer's unit is not measured.
- Not checked yet (needs a run): whether clicks on the window also reach the game world, text fitting at
  other UI scales, whether Ctrl+Shift+C does something in Kenshi too.
- Single-player only by intent: nothing stops it in a multiplayer session yet (needed before M3 is used with
  others).

## The probe's three bugs: causes and fixes

Read in RE_Kenshi's 1.0.65 exe (`Kenshi_x64.exe`, SHA-256 `504b362cde850d56afb1cea6f5b7b0ee014d9dd7b47e188599d91c804502cd3e`;
iced-x86 disassembly; RVAs, image base 0x140000000). In 1.0.68 only the rows we call are proven (the proof
rules above); their callees (`tryAddItem`, the section's `addItem`, ...) were not compared.

1. **Weapons: the factory returned nothing.** `RootObjectFactory::createItem(gd, hand, weaponMesh, matData,
   level, faction)` (0x57fcc0) reads the GameData type (`gd+0x50`) and returns 0 at once for type 2
   (`WEAPON`) and a few others (`cmp eax,2; je` → `xor eax,eax`, **M**). Weapons are built in its branch for type 0x33
   (`WEAPON_MANUFACTURER`): gd = maker, third argument = the weapon (null → one picked from the maker's
   "weapon types" list), null material → picked from the maker's "material" list; with no level found it
   falls back to maker `1057-gamedata.base` and material `1058-gamedata.base` (so `cheat_weapon_maker`
   is then ignored), then allocates 0x258 bytes and builds the weapon (**M**). Fix: `createItem(maker, hand(), weaponData, NULL, -1, NULL)`. That the
   third argument takes the weapon's own GameData (KenshiLib calls it `weaponMesh`) is **I**: the log names
   the item made.
2. **Amount > 1 left extra single items.** `Inventory::addItem(item, q, drop, destroy)` (0x745140) calls
   `tryAddItem(item, q)` (0x74b1e0), which tries each section's `InventorySection::addItem` (0x74bd70) and
   passes q = 1 to the sections after one fails. A section's addItem sets q = q + item quantity - 1 and the
   item's quantity (`+0x12C`) to 1, merges into matching stacks, then, while q is above the stack limit,
   places copies (`RootObjectFactory::copyItem`) one by one; when a later placement finds no space it
   returns false and does not take the placed copies back (**M**). After a failed `tryAddItem`,
   `addItem`'s own drop path drops the item and, for an item that does not stack, copies (`copyItem`) and
   drops more per its quantity (**M**). Which of these made the probe's 5 + 4 is **I**. Fix: an amount N is
   N single creates and `addItem(item, 1, true, false)` calls: q = 1 never copies in the section code; the
   log gives the item's quantity right after `createItem` (1 expected, **I**) and the stack merge is
   expected to join them (**I**).
3. **Teleport lost while paused.** The probe called only `CharMovement::_setPositionAndTeleport(pos, 0)`
   (0x65deb0). The game's `Character::teleport(const Vector3&)` (0x5c9bf0) also writes the character's own
   position (`this+0x48`), calls the movement's `_setPositionAndTeleport(pos, -1)` (vtable +0xB8 of
   `this+0x640`; floor -1 leaves `+0x334` unchanged, 0 set it to 9) and, unless a thread runs
   (`ThreadClass::isRunning`), `AnimationClass::setPosition` on `this+0x448` (**M**). Which difference lost
   the move is **I**. Fix: `Character::teleport`. Check (log only): the position right after the teleport
   and again after 1 s of running game, with the distance to the target.

## Test steps for the user (after M3, with the game free)

1. Claude installs the new build with the cheats (`install-client.ps1 -Cheats`); nothing to type.
2. Start Kenshi and load a save. The "KenshiPure cheats" window appears; select one of your characters
   (its name shows in the window).
3. Press each button once, also once while paused: Damage three or four times (health bars drop, the
   character may fall over), Heal (healthy again), Knock out (falls unconscious, wakes later by itself),
   Create item with Bread and amount 5 (one stack of 5 in the bag), then Katana, then Samurai Armour;
   move the camera away and press Teleport to camera (once more while paused, then unpause).
4. Tell Claude what you saw: does the window look right, did a click also move your character, did
   anything go wrong. Claude reads `KenshiPureCheats.log`.

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
and log before/after (`dxgi: ...` in `KenshiPureLoader.log`). Measured in both runs: the game asks for
`1920x1080 ... scaling STRETCHED, windowed`, we pass UNSPECIFIED, and the picture is right (user).

Fallback (`client/Display.cpp`, in KenshiPure): a worker thread waits until the game window is in front
and answers messages (3 checks, 0.5 s apart). If the client area
differs from the desktop mode saved in Windows, it minimizes the window and restores it after 0.7 s.
Log lines `display: ...` show window, client, monitor and display-mode sizes and DPI. Not yet run in
game.