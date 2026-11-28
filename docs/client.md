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

## Start time without RE_Kenshi

An earlier run without RE_Kenshi needed ~100 s to the first frame (~20 s with it), put down to RE_Kenshi's shader
cache (`RE_Kenshi\shader_cache.sc`, 248 shaders). Kenshi's Ogre has its own microcode cache
(`GpuProgramManager::setSaveMicrocodesToCache`, `loadMicrocodeCache`, `saveMicrocodeCache`; the D3D11 render system
imports them) that Kenshi never switches on (no reference in either exe, **M**). The loader counts the render
system's `D3DCompile` calls (it imports them from the game's `D3DCompiler_43.dll`) and logs count and time when the
render loop starts and ends. Run on Steam 1.0.68 without RE_Kenshi (**M**, `run-game-test.ps1 -NoReKenshi`, save
"MP TEST" and one reload): first frame 22.7 s after the plugin start, 194 compiles in 8.7 s in all. A cache could save
at most those ~9 s per start, so none is built; what made the earlier start slow is not known (a cold file cache or
the driver's own shader cache are guesses, **I**).

# Client: own pose (M1)

Source: `client/Pose.cpp`, pure helpers `client/PoseMath.h` (unit tests `client/test/PoseMathTest.cpp`,
run by `build-plugin.ps1`). Run in game (**M**, the user's run on 1.0.65 with RE_Kenshi, character Gecko,
1 owned): stand, walk with move orders, turn, game speed ×5, pause, menu and save load; no fault, the
game kept running. Second PC (**M**, a tester's log, same build 0.0.3-m1): Steam 1.0.68 without RE_Kenshi,
64/65 rows (`inventory` off), ~4 min of walking with the same behaviour (speed ~70 while walking, yaw =
facing, 0 standing); no fault.
- First sample after a load can be half set up (**M**, second PC): destination and path destination
  0/0/0 (so `walking` 1) and a yaw (147.5) that does not match `facing` (0, 0, 1); the next sample, 1 s
  later, was consistent. NetFrame drops the first sample of each epoch and then samples with destination 0/0/0
  (at most 2 s) and logs per world how many it dropped and whether the first was half set up
  (`net: sending own pose (…; the first had destination set|0/0/0 (half set up); …)`). On this PC the first
  sample had its destination in all 4 loads checked (two in the M3 run, two in a 1.0.68 run, **M**), so the rule has not met a half-set-up
  sample in game yet.

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
  sample was not seen in this run.
- Game exit (that build): the server reported `player 2 left (timed out)` ~7 s later; fixed by the exit hook
  (below).

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
- Exit: see "Exit" below; `kpStop` = leave + stop, at most 2 s (the thread is left to the process exit if
  it hangs).

**Exit** (**M**, 1.0.65 exe disassembly, RVAs): Kenshi never destroys Ogre's Root, so Ogre never calls
`dllStopPlugin` (no loader log line in 4 exits). Every way of quitting sets the quit byte `0x2132408`
(`TitleScreen::exitGame` `0x9142b0`, a window-close listener `0x812500` (its role **I**), `0x913050`,
`0x82a460`, `~GameWorld`); the frame callback `0x82aaf0` then returns false, Ogre's `Root::startRendering`
returns to `Renderer::go` (`0x812890`: `mov rcx,[rcx+68h]; jmp [startRendering]`), then
`KingOfRenderThread::startup` (`0x86c0c0`, which also creates the Root: our plugin starts inside it) and WinMain
(`0x744960`) return, WinMain with 1, and the C runtime calls `exit`. The loader (0.0.4-m3) hooks OgreMain's
`Root::startRendering` export and calls `kpStop` once it returns: no frame runs any more and every thread is
alive. 1.0.68 has the same `Renderer::go` (`0x813460`). Runs (**M**, step `quit` = WM_CLOSE to the game window;
two on 1.0.65 with RE_Kenshi, one on Steam 1.0.68 without it): the loop ended 80–140 ms after WM_CLOSE, the server
logged `player 2 left (left)`, the remote bodies were forgotten without a game call, exit code 1 (WinMain's own), no
crash dump, and no exception at all at exit (the first-chance access violation in `RE_Kenshi.dll+0x381e9` that
the user's exits in the M0 and M3 runs showed did not come; that it was RE_Kenshi's own and handled is **I**: it was
in RE_Kenshi's module and the game went on to exit).

**Load stalls (plan G14)**: the game's frame thread blocks ~60 s while loading; the network thread
keeps servicing ENet meanwhile, so neither side's ENet timeout (5–30 s, protocol-ours.md "Timeouts")
is reached. **M**: `kp-core-test`, and in game the test step `stall 60` (the game's main thread sleeps 60 s,
no frame runs) in two runs: no `connection lost`, the server timed nobody out.

**Timer resolution**: ENet's `enet_initialize` calls `timeBeginPeriod(1)` (`win32.c`); no Kenshi module
names `timeBeginPeriod` (both exes and every DLL in the game folder, string search). `client/Perf.cpp` logs every
10 s `perf: fps …, cpu …% of one core, Sleep(1) … ms, system timer … ms` (Sleep(1) measured in the process: what
the game's own waits get). A/B runs (**M**, same save and steps): with ENet's request undone (`timeEndPeriod(1)`
returned ok) Sleep(1) still took 1–2 ms, so something else in the process holds a fine timer; fps 51–57 and cpu
136–160 % in both. ENet's request costs nothing measurable and stays (the network thread's 2 ms polling needs it).

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

# Client: stand-in body (M4)

Status: done (**M**: one run by the user, build 0.0.5-m4, then three unattended runs with
`scripts/run-game-test.ps1`, all on 1.0.65 with RE_Kenshi, save "MP TEST", `kp-bot --around-first --radius 300`).
A borrowed NPC follows the bot's circle; the way that works is the normal walk order with the NPC's own AI on.

**Choice**: the stand-in is the nearest loaded NPC within 600 units of the local character that is not the
player's (`isPlayerCharacter` false, not in `PlayerInterface::playerCharacters`), alive, conscious, with
movement and AI (`client/StandIn.cpp`). Not a spawned body: spawning is M5's risk (G3); M4 isolates the
driving. Side effects on that NPC: it is moved, its AI may be skipped, `Character::setDestination` clears its
job list; on release it is halted and teleported back to where it stood. Do not save during a test (an autosave
at 21:49 in the user's run wrote `autosave1` while it was driven).

**Pieces**
- `client/Drive.{h,cpp}` (pure, both toolchains): `RemoteTrack` (pose time = receive time − age; repeats
  ignored; interpolation 150 ms behind; extrapolation ≤ 300 ms) and `DrivePlanner` (place every 100 ms; snap
  when the x/z gap > `standin_snap` (300), at most once a second; walk to a lead point 0.6 s ahead, re-issued when
  it moved > 15 units or after 1 s, speed × (1 + gap/100) capped at 2.5; steer = velocity + 2/s × gap, capped).
- `NetFrame` owns the lifecycle: the first remote player with a pose drives it; pick every 2 s; then a wake-up
  (AI on, walk orders until the body moves, ≤ 20 s); release on no own character or a new *world* epoch (menu,
  load, load stall; not a click on another own character) without any call into the old world; release with
  put-back on a remote pose older than 3 s, the player leaving, or 3 failed commands in a row. Every read
  re-resolves the stored `hand` and refuses a body that is a player character or has another name.
- `client/StandIn.cpp`: every call in `__try` on the frame thread, through the rows of feature
  `optional:standin-write` (`client/sites.tsv`; each 1.0.68 code value equals AK's RVA in remote-bodies.md).
  AI skip: a MinHook detour on `AI::periodicUpdate` (or `Character::periodicUpdate`, `standin_ai=character`)
  that returns early for one pointer. Walk and steer first set the speed orders to RUN, then the float.
- Cycle (`standin=cycle`): place 20 s (AI skipped) → path 45 s (`CharMovement::setDestination`, AI skipped)
  → order 45 s (`Character::setDestination`, AI on) → steer 45 s (`setDirectMovement`, AI skipped); a summary
  line per phase and a status line per second (body, target, gap, speed, desired, max, walk speed, speed orders).
- Config keys in `KenshiPure.cfg`: `standin` (off|place|path|order|steer|cycle, default off), `standin_ai`
  (ai|character|none), `standin_snap`, `standin_range`, `standin_name`, `standin_settle` (s, default 0).

**Results (M)** — last unattended run, bot 50 units/s, Meathead's max speed 51.6:

| Phase | gap mean / max | body moving | snaps | speed (asked) |
|---|---|---|---|---|
| place | 4.9 / 126 (3.2 in the second cycle) | 99 % | 0 | — |
| path (AI skipped) | 254 / 433 | 53 % | 21 | 34.6 (100) |
| **order (AI on)** | **107 / 368** | **95 %** | **2** | 45.9 (96) |
| steer (AI skipped) | 101 / 302 | 88 % | 2 | 297 reported (98); the user saw "faster but glitchy" |

- Speed: an NPC's speed orders are WALK, and Kenshi sets desired speed from them (`setDesiredSpeed(MoveSpeed)`,
  1.0.65 `0x6646c0`: WALK = walk speed `+0xC0` (15 for Meathead), JOG = 55, RUN/GROUPED = 999, **M**); the move
  task restores it from the orders (`CharBody::_move` `0x5c5ecc`). With RUN first the walk went from 15 to the
  NPC's max speed (`+0xB4`, ~52–55, set from the character every periodic update, `0x66036c`); `CharMovement::update`
  passes min(desired, max) on (`0x65f5c7`). So a body can never be faster than its own max (**M**).
- Busy NPCs: Meathead (picked at y +33 above the ground: probably sitting) ignored teleports and orders while its
  AI was skipped, also 60 s after the load, and moved as soon as its AI was on (**M**, two runs; "busy" **I**).
  Hence the wake-up.
- Path with the AI skipped moves the body only half the time; order with the AI on works best; steer moves it but
  reports nonsense speeds and looked glitchy: **order** is the way for M5 (with a spawned body the AI decides
  less, **I**).
- The skipped AI ticks run on another thread than the frame thread (**M**). A save load released the stand-in
  without calls and picked it again; no fault in any run.

**Unattended test** (`scripts/run-game-test.ps1 -GameDir <Kenshi> -Save <name>`): starts server and bot, sets
`kenshi.cfg` windowed, writes `modsKenshiPureautoload.txt`, starts Kenshi through Steam; the plugin
(`client/AutoLoad.cpp`) reads and deletes the file, presses OK in Kenshi's start dialog, moves the game window
to a non-primary monitor and asks `SaveManager::load(name)` through a message posted to the game window (our
frame hook and an Ogre frame listener do not run at the main menu, **M**); after the given seconds the script
kills Kenshi without saving, restores `kenshi.cfg` (also one left by an aborted run) and prints the log lines.

# Client: remote bodies (M5)

Status: done (**M**: kp-remote-test without the game; unattended runs with `scripts/run-game-test.ps1` on 1.0.65 with
RE_Kenshi, save "MP TEST" (save tests on the copy "KP TEST MP TEST"), `kp-bot --around-first --radius 300 --speed 60`:
eight runs with 0.0.6-m5, then the runs below with 0.0.7-m5). Each remote player gets an own body: built when its first
poses arrive and only where the game has the zone loaded, driven, removed when the player leaves, goes quiet or walks
out of the loaded zones; it stays in the world while the game saves and is never written into a save.

**Pieces**
- `client/Remote.{h,cpp}` (pure, both toolchains): `RemoteBodies` keeps per remote player `{player id, slot, state,
  track, planner}`; the game adapter keeps one hand per slot, never a pointer. States: waiting → live → removing →
  waiting/free; "given up" after 3 failed spawns. NetFrame feeds it (join, leave, session end, poses) and calls it each
  frame; it is off while the M4 stand-in is on.
- `client/RemoteGame.{h,cpp}`: the game calls, each in `__try`, on the frame thread; rows of feature
  `optional:remote-write` in `client/sites.tsv` (all proven in 1.0.65 and 1.0.68).
- Test: `net/test/RemoteTest.cpp` (kp-remote-test): settle time, spawn, drive with the raised max speed,
  leave, no snapshot for 3 s, a pose older than 15 s, a world change (kept after the hand check, or forgotten; no game
  call while there is no world), failed spawns, a destroy that does not remove, death, knock-out, faulting commands, the
  session ending, range, the body limit, auto switching, pause, the sweep once per world, loaded zones (no spawn outside, removal after 1 s outside).

**Spawn** (G3 recipe, remote-bodies.md): faction by string id (`remote_faction`, default `nofac` = "No Faction",
the faction `FactionManager::getEmptyFaction` returns), squad `Faction::createNewEmptyActivePlatoon(NULL, permanent,
pos)` (`remote_squad_permanent`, default 0 = temporary, see Squads) → `Platoon::getActivePlatoon()`, body `RootObjectFactory::createRandomCharacter(faction, pos, squad, NULL, NULL,
1.0)` (template NULL = "random character base-copy"); faction read back; `clearAllAIGoals()` once; name `[KP] <player>`;
hand built and checked; teleported to the pose. At most 8 bodies, one spawn per frame, only within `remote_range`
(3000) of the own character, `remote_settle` (3 s) after the world appeared, and only where the zone is loaded
(below). Each spawn logs the squad's string id and whether it is permanent, and how every faction near the body stands
towards "No Faction".

**Drive**: every frame the body's max speed is set (`remote_maxspeed`, default 150; through `CharStats::moveSpeed`,
`AnimationClass::setMaximumSpeed` and `AbstractMovementBase::maxSpeed`, the chain remote-bodies.md G5 measured), then
one planner command. `remote_drive`:
- `steer`: `setDirectMovement(unit direction, speed²)` with desired speed = |sender velocity + 2/s × gap|, capped at
  1.5 × the sender's speed (200 when it stands); AI skipped. Place = `_setPositionSimple` + `teleportVisuallyOnly`.
- `order`: M4's walk order (`Character::setDestination`, RUN, lead point 0.6 s ahead), AI on; the asked speed is held
  every frame (the move job puts desired back to the walk speed between orders).
- `auto` (default): steer; switch to order while stuck (gap > 40 and speed < 20 for 0.4 s, or gap > 150), back to steer
  when the gap stays < 15 for 1 s.
Snap past `remote_snap` (300).

**Removal**: `GameWorld::destroy(body, false, "KenshiPure: remote player gone")` once on leave, session end, no
snapshot for 3 s, a pose older than 15 s, out of range + 500, death, 10 s knocked out, 3 failed commands, 1 s outside the loaded zones; the slot is
freed when the hand stops resolving (asked again after 10 s, at most 3 times; never a new body while the old one may
still stand).

**Loaded zones and squads (M, 1.0.65 exe)**: `ActivePlatoon::unloadCheck` (`0x4fe140`) keeps a squad while
its position lies in a loaded (or loading) zone's bounds minus 170 units of unloaded edges (or where there is no zone
map); otherwise a 4.5 s timer runs out and `ActivePlatoon::update` returns false. An empty squad of a non-player faction
also returns false (`0x4ff995..0x4ff9ad`). `Faction::updateActivePlatoons` (`0x6ba810`) then deactivates a permanent
squad (it stays as an unloaded squad and would come back later as a body nobody drives) and destroys a temporary one
(`Faction::destroyPlatoon`). So our squads are temporary, `RemoteGameLoadedAt` repeats the zone test (rows
`GameWorld::zoneMgr`, `ZoneManager::getZoneMap`, `ZoneMap::isLoadedMT` / `isBeingLoadedMT` /
`getBoundsMinusUnloadedEdges`), no body is built outside the loaded zones, and a body that stands outside for 1 s is
removed by us (before the game's 4.5 s) and built again once the remote pose is back inside.

**World change**: no own character (menu, loading) or a new world epoch (load, or a 2 s stall) → no game call; once the
world is back each hand is checked (resolves, not a player character, its `[KP]` name and its faction): kept, else
forgotten. Once per world, after the settle time, characters of our faction named `[KP] …` that are not ours are destroyed
(logged `STRAY`; only a save written by 0.0.6-m5 or older can hold one).

**Saves (G7)**: a body standing while the game saves would be written into the save and come back on load, from a
permanent or a temporary squad (0.0.6-m5 runs 5 and 7, **M**). The save path (**M**, 1.0.65 exe): `Faction::saveState`
(`0x372920`) calls `Platoon::serialiseEverything` (vtable `0xB8`, base `0x372db0`) for each of its squads and ignores
the result (`0x372d05`, `0x372d54`); that function writes the squad's record and, through
`ActivePlatoon::serialiseEverythingToDisk` → `saveToDisk` (their only callers), its file `platoon/<id>.platoon` with
the characters; for squads it does not save (dead ones far from the player) it returns NULL itself (`0x372dfb..0x372ef9`).
`remote_save_guard` (default on) detours it: for one of our squads (address and the platoon's string id as recorded at
spawn; entries outlive the body, so an empty squad is skipped too) it takes that same NULL path. The body keeps walking
through the save and the loaded save holds none (runs below). The earlier guard (destroy every body in a
`SaveManager::saveGame` detour; bodies came back 5 s later) is gone; a forced `GameWorld::processKillList(true)` there
had crashed the game in `Character::stumbleState` (0.0.6-m5 run 6, **M**).

**Results (M)**, bot 60 units/s on a circle through two obstacles:

| Run | Drive | gap to target mean | notes |
|---|---|---|---|
| M4 | order, own max ~52 | 107 | borrowed NPC |
| 1–2 | order, max 150 | 62.5 / 63.3 | 0 snaps; gaps 100–280 only at the two obstacles (path around them), open ground 1–25 |
| 3 | steer | 88.7 | open ground 0.1–3, stuck at the obstacles (speed 0) |
| 4 | auto | **45.5** | gap > 100 in 16 % of status lines (order 26 %) |

0.0.7-m5 runs (**M**): (1) save at 60 s: "the save skips our squad No Faction_0" 155 ms after the request, a status
line every second through the save; load: "kept 0 bodies", no STRAY, new body 3 s later, gap mean 44.1. (2) leave, load,
rejoin: destroyed at once, new body after the load. (3) leave, save 36 s later: no squad skipped, so the game had
destroyed the empty temporary squad; load: no STRAY. Relations of the 13 factions near the test spot (Dust Bandits,
Starving Bandits, Holy Nation Outlaws, Nomads, the player's "Nameless", …) towards "No Faction": -10 to 0, none at the
enemy line -30 (`FactionRelations::isEnemy` is relation <= -30, `0x6b23b3`, **M**). (4) circle radius 3500 at 140 units/s
(`remote_range=8000`): 225 status lines, the save skipped the squad, the body never left the loaded zones (they reach past
3500 here); gap mean 141 (the bot near the body's max 150). (5) radius 6500 (`remote_range=20000`): "outside the loaded
zones - waiting", a body once the pose was inside, "it left the loaded zones" 1 s after it stood outside, gone at once,
built again when the pose came back in (twice in 5 minutes). No fault in any run.

Lifecycle: leave → destroy returned 1, gone at once; rejoin (new id) → new body; in-world load → old hand forgotten,
new body 3 s later; no fault in any run except run 6. Log: per body a status line a second (`remote: <name> (<id>)
<mode> body … gap … lag … speed … max …`) and a summary at removal.

**Test runs** (`scripts/run-game-test.ps1`, added for M5): `-Cfg "k=v;…"` (KenshiPure.cfg keys for one run, restored
after; `standin` lines dropped), `-Steps "at=<s> save|load SAVE NAME;…"` (run by `client/AutoLoad.cpp` from the game
window's messages, seconds after the world was entered; also `stall <s>` (the main thread sleeps), `select <n>`
(`PlayerInterface::selectPlayerCharacter(n, false, false)`, as `GameWorld::processKeys` calls it for a number key,
1.0.65 `0x787745`) and `quit` (WM_CLOSE to the game window; the script then waits for Kenshi to exit by itself and
prints its exit code, the loader's stop line, the server's leave line and any new crash dump)), `-BotSeconds`,
`-BotRestart`, `-SaveCopy`, `-NoReKenshi` (the `Plugin=RE_Kenshi` line is left out for one run: Steam's 1.0.68 exe).

Open: relations measured at one spot only (a faction elsewhere could stand at -30 or below; nothing makes ours neutral
yet); two real players (M6); the bot's circle, not a real player, made the measurements.

# Client: game speed (M6)

The user's rule (plan G10): while connected to a server the game runs at x1 and the player cannot pause it. A remote
body moves only while the local game runs and at its speed, so two players at different speeds would see each other
trail or jump. `client/SpeedLock.{h,cpp}`, feature `optional:speedlock-write` (rows `GameWorld::userPause`,
`setGameSpeed`, `togglePause`, `processKeys`, `DialogueWindow::show(Dialogue)`, `CharacterEditWindow::initCharacters`,
`BuildMode::pauseOnOpen`; all proven in 1.0.65 and 1.0.68).

How the game sets speed and pause (**M**, disassembly of both exes; 1.0.65 RVAs from KenshiLib's address map):
- Speed is `GameWorld+0x700` (`getFrameSpeedMultiplier`), paused is `+0x8B9` (`isPaused`). `setGameSpeed(s, click)`
  (`0x787300`) writes the speed, then calls `togglePause(s == 0)`; `togglePause(on)` (`0x787200`) sets
  paused = on || speed == 0. `userPause(p)` (`0x787470`) sets the speed to 0 (p) or back to the speed saved before the
  pause, with the same tail.
- Every direct caller (calls and jumps to the functions and their incremental-link thunks, whole `.text`, the same set
  in both exes): `userPause(true)` from `GameWorld::processKeys` (`0x7875c0`, the pause key toggles),
  `DialogueWindow::show(Dialogue*)`, `CharacterEditWindow::initCharacters` (a new game's editor) and the level editor;
  `setGameSpeed` from `processKeys` (speed keys), `MainBarGUI::changeSpeed` (buttons, tail jump) and the editor's confirm
  (1.0); `togglePause(true)` from the build mode (`0x4e5b50`, 1.0.68 `0x4e65e0`, no KenshiLib name; called from
  `MainBarGUI::buildButton` and `processKeys`, so opening the build mode pauses the game), `SaveManager::loadGame`,
  `ZoneManager::processLoading`, `ZoneManager::activateAllActiveZones` (tail jump), two functions reached only
  indirectly (`0x913274` near `LoadingWindow`, `0x4d0e90`), and the tails of `setGameSpeed` / `userPause`.
  `togglePause(true)` alone leaves the speed as it is.

Lock (from the first frame the core is welcomed, `NetConnected()`, until the game exits; latched like the cheats, so a
dropped connection opens no window for pause or speed); a caller is the function whose `.pdata` entry holds the
return address (`RtlLookupFunctionEntry`; the call sites above lie inside the named functions in both exes, **M**):
- `userPause(true)` passes only from the character editor; every other is dropped and logged (`speed: pause blocked
  (pause key|dialogue|another caller)`). After `userPause(false)` the speed is set back to x1.
- `setGameSpeed(s)` with s != 1 runs as `setGameSpeed(1)` (logged).
- `togglePause(true)` from the build mode is dropped (`pause blocked (build mode)`); the loading pauses stay (they keep
  the world from running half loaded).
- Each frame: a speed other than 1 (from before the connection, or a path not found statically) is set to x1, except
  while the editor's pause holds or the game paused itself (paused with a speed above 0: calling `setGameSpeed` would
  end that pause through `togglePause`). Every change of speed or pause is logged (`speed: now x… paused|running`), at
  most one frame line per 500 ms.
- Not covered: a call through a function pointer to these functions (none found statically).
- `sites.mjs` code rows for functions without a KenshiLib name: `--sym rva65:0x…` (the 1.0.65 start must begin a `.pdata`
  entry, the 1.0.68 match too; no thunk is generated).
- Test steps for `scripts/run-game-test.ps1 -Steps`: `at=<s> speed <x>`, `at=<s> pause x`, `at=<s> unpause x` call
  `setGameSpeed` / `userPause` from the plugin (while connected the pause is dropped as "another caller"; the pause key,
  dialogue and build mode paths are covered only by a player).

# Client: multiplayer package (M6)

`scripts/make-package.ps1` builds `build\package\KenshiPure-<version>\` and its `.zip` for a PC without build tools or
this repo: `Install KenshiPure.cmd` / `Uninstall KenshiPure.cmd` (run `files\install-client.ps1 -Ask`), `files\` (the
three DLLs, the install script, `lib\CfgEdit.ps1`, `builds.txt` = the `#build` lines of `client/sites.tsv`), `server\`
(`kp-server.exe`, `Start server.cmd`: port 27850, `--log-poses`, appends to `kp-server.log`), `README.txt`,
`LICENSE.txt`, `SHA256SUMS.txt`. The script refuses a package holding a cheat DLL, a `.pdb` or any file that contains
this PC's user name, user folder or repo path (`build-plugin.ps1` links with `/PDBALTPATH:%_PDB%`, so the DLLs name
only their `.pdb` file, **M**: no user path left in the three DLLs).

`install-client.ps1` runs from the repo and from the package (package mode: the DLLs lie next to it):
- `-ServerHost`, `-Port`, `-PlayerName` replace those lines of `KenshiPure.cfg` in place (other lines kept); names are
  1-32 printable ASCII characters without `#` or `=`; `-Ask` asks for the Kenshi folder, the server address and the
  name when not given (earlier values as default).
- Without `-GameDir` it looks for `steamapps\common\Kenshi` in Steam's folder (registry) and the libraries of
  `libraryfolders.vdf`.
- Before installing it hashes the exe the game will run (RE_Kenshi's copy while `Plugin=RE_Kenshi` is on, else
  `kenshi_x64.exe`) and stops without changes when the build is not one of `builds.txt` / `sites.tsv`.
- Unblocks the copied DLLs (the "from the internet" mark of a downloaded zip); `-Cheats` is refused without a cheat
  DLL. Pester tests: 11 (`scripts/tests`), including a package folder against a fake Kenshi folder.

Steps for the two people: [playing-together.md](playing-together.md).

**Runs (M)**, 0.0.8-m6 installed from the package (host 127.0.0.1), `scripts/run-game-test.ps1 -SaveCopy -Steps "at=20 speed
3;at=30 pause x;at=40 unpause x;at=55 save SAVE NAME"`, save "MP TEST", bot 60 units/s:

| Run | Build | welcomed | lock | x3 step | pause step | save | body gap mean |
|---|---|---|---|---|---|---|---|
| A | 1.0.65 (RE_Kenshi) | 0.2 s after start | on the first connected frame | kept at x1, no speed change after | blocked (another caller) | skipped our squad | 50.2 (133 lines) |
| B | 1.0.68 (`-NoReKenshi`, Steam's exe) | 0.2 s | same | same | same | same | 46.8 (130 lines) |

No exception in either run; `Plugins_x64.cfg` came back with its RE_Kenshi line. Not covered by a run: the pause key,
the speed buttons, a dialogue, the build mode and a new game's character editor (they need a player), two real
clients (G9: Steam starts one Kenshi per PC and both would write the same log; not tried).

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
it); without the file KenshiPure logs `cheat: not installed` and nothing changes. The user's run on 1.0.65
(Steam, with RE_Kenshi) passed: window shown in game, every button worked as the user saw it, and
`KenshiPureCheats.log` agrees (below).

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
- In game (**M**, user's run on 1.0.65): damage, heal, knock out (timer 20) on the selected character; Katana
  made through the weapon maker, 1 in the inventory; Samurai Armour and Bread x2 land in the inventory,
  later Armour x2 all on the ground; Bread x100 gave 1 in the inventory and 99 on the ground (a full bag is
  **I**); teleport to camera centre lands on target (0.0 x/z after 1 s). The user saw no problem.
- Not checked yet: the same on 1.0.68, text fitting at other UI scales, whether Ctrl+Shift+C does something
  in Kenshi too.
- Single-player only: once KenshiPure is welcomed by a server, `CheatHost` calls the DLL's `kpCheatStop` (drops a
  queued click, hides the window, `kpCheatFrame` does nothing more) and stops forwarding frames for the rest of the
  game run, also after a disconnect (`cheat: off for the rest of this game run`). The multiplayer package has no
  cheat DLL at all.

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

## Test steps for the user

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