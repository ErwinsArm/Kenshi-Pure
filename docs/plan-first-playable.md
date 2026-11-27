# Plan: first playable step

Goal of this step: two players run Kenshi with **our** client mod, connect to **our** server, and
each sees the other's character as a body in their own world, walking where the other walks.
Nothing else (no combat, items, NPC sync, saving, accounts). AnarchyKenshi (AK) v0.133.1 is a
reference only; none of its DLLs are loaded, reused or shipped.

Labels: **M** measured, **D** documented, **I** inferred (as in the findings notes). Kenshi RVAs
are for the Steam `kenshi_x64.exe` whose SHA-256 is `a596ab4e…aeb1` and which AK's notes call
1.0.68 ([hooks.md](findings/hooks.md)); that this is the build the user plays is **not
established** (G8). "A n"/"B n" = 0-based row n of site table A/B in
[kenshi-sites.tsv](findings/kenshi-sites.tsv). Function names in that table are AK's names; what a
function does is **I** unless a note measured how AK uses it.

Everything marked "without the game" is verified here by builds and tests. The risky parts
(G1-G5, G7: reading and driving real characters, spawning, load/unload) are only verified by
in-game runs **by the user**; Claude can only read the logs those runs leave.

## 1. What we know that this step needs

**Getting into Kenshi**
- Kenshi's Ogre loads every `Plugin=` line of `Plugins_x64.cfg` and calls the DLL's exported
  `dllStartPlugin`; AK's line is `Plugin=mods/AnarchyKenshi/AnarchyLoader` (path relative to the
  game folder, no `.dll`). AK's loader exports only `dllStartPlugin` and an empty `dllStopPlugin`,
  runs once (guard byte) and never aborts the game on failure. Source: AnarchyLoader.dll.md "Role",
  "Exports", "Start flow" (**M**: export table, enable-script string; the Ogre plugin convention
  itself **D**, Ogre plugin API).
- Runtime and ABI: AK's Loader and Launcher import `MSVCR100` (VS 2010 CRT; AnarchyLoader.dll.md
  header, **M**). `kenshi_x64.exe`, `OgreMain_x64.dll` and `MyGUIEngine_x64.dll` in the Steam folder
  also import `MSVCR100`/`MSVCP100` (**M**, by the critic's import check; not yet re-run by us). So
  Kenshi, its Ogre and MyGUI use the VS2010 C++ runtime, and any STL object or `Ogre::String`
  crossing into them has a VS2010 layout and allocator (**I**). Whether a current MSVC toolchain
  can call them safely is **not decided**: the M0 ABI proof settles it.
- Second reference: **KenshiLib / RE_Kenshi** is installed on this PC: `RE_Kenshi` is in the user's
  `Plugins_x64.cfg`, `RE_Kenshi/RVAs/{GOG,Steam}_1.0.65.br` exist, and `RE_Kenshi_log.txt` names
  KenshiLib 0.5.0 (**M**, by the critic; files not read by us). Its headers may already hold
  signatures and struct layouts for G2/G3 (**I**, contents not read). Our plugin must coexist with
  it (a second plugin hooking the same exe).
- AK binds game globals at plugin start but does all game work in per-frame tasks (`AK 0x807e0`,
  AnarchyKenshi.dll.md §1 steps 4, 13, **M**). Kenshi's own frame callback is
  `MainListener_frameRenderingQueued` (B277, Steam `0x82ac70`, detoured by AK, **M**); that game
  logic runs inside an Ogre frame listener is **I** from the name.
- Stalls: startup and save loading block Kenshi's main thread for about 60 s (`kenshi.log`,
  `RE_Kenshi_log.txt`, **M**, by the critic). AK itself runs a separate network thread
  (protocol.md §1, **M**).
- Game speed can be raised up to ×20 (`RE_Kenshi.ini`, **M**, by the critic); the game can pause.

**Finding Kenshi functions**
- AK checks the build by masked canary bytes at two RVAs per build (Steam `0x8b9ef0`, `0xbaa450`;
  GOG `0x8b9630`, `0xba9b90`), then scans the live `.text` for 795 byte patterns, takes the median
  shift, and falls back to the static GOG/Steam column. hooks.md §1 (**M**).
- All 795 patterns occur exactly once in the Steam exe at the Steam RVA (**M**, by script); GOG RVAs
  are unchecked. The TSV holds names and RVAs, **not the patterns**, so another build cannot be
  re-derived from it alone.
- Version evidence conflicts (**M**, by the critic): the exe with the hash above contains only
  "1.0.68" strings, but today's `kenshi_info.log` says "Version: 1.0.65 - x64 (Newland) (Steam)" and
  RE_Kenshi says 1.0.65 is supported. Unexplained (G8).
- Globals: `Global_gameWorld` B83, Steam data RVA `0x2134110` (anchor code `0x874e70`), plus
  gameGui/gameInput/gameSettings/renderMaster (hooks.md §1, kenshi-sites.tsv, **M**).

**Reading the local character** (protocol.md "Position `0b`" table: AK's sender `AK 0x2f5a30`, **M**)
- Position: `Character_positionDirect` A138, Steam `0x5ce680`. Disassembly (**M**, by the critic)
  shows it is **not** a plain field getter: it checks `[rcx+0x2f8]` and calls a lookup that uses a
  global. Its AK name is a guess; the real position source needs a second look (G2).
- Facing: `RootObject_orientationDirect` A322, `0xd1f00`: same shape as motionNow below, 4 floats at
  `+0xb0..+0xbc` (**M**, critic). AK sends the quaternion's rotated +Z axis `(2(wy+xz), 1−2(x²+y²))`,
  normalised in xz, so yaw = atan2(x, z) (protocol.md, **M**).
- Velocity: `AbstractMovementBase_motionNow` A12, `0x5c7840`: `this` in RCX, writes the Vector3 into
  the out-buffer in RDX and returns RDX (**M**, by the critic). Declare it as
  `Vec3* (*)(void* self, Vec3* out)`, not as a pointer-to-member.
- Destination: movement object at `Character+0x640`, destination at `+0xe8`, wanted speed at
  `+0xb8`; "walking" = xz distance² to destination > 4.0 (offsets **M**, unnamed in Kenshi).
- "Mine" candidates (names only, **I**): `GameWorld_tickedCharacters` A227 `0x664360`,
  `Character_isPlayerOwned` A118 `0x7916d0`, `PlayerInterface_pickOwned` A305 `0x7f90b0`.

**Spawning and moving a remote body** ([remote-bodies.md](findings/remote-bodies.md): AK, KenshiLib,
KenshiCoop and WorldOfKenshi compared; calls by KenshiLib name, RVAs there are 1.0.68)
- Spawn: `Faction::createNewEmptyActivePlatoon(0, true, pos)` → `Platoon::getActivePlatoon()` →
  `RootObjectFactory::createRandomCharacter(faction, pos, activePlatoon, template, 0, 1.0)` (AK and
  WoK agree, **M**; WoK "verified in game" **D**).
- AI: skip `AI::periodicUpdate(float)` (AK) or `Character::periodicUpdate` (KenshiCoop) for our
  bodies in a detour (**M**); never `removeFromUpdateListMain` on a driven body (**D** KenshiCoop).
- Move, two candidates for M4: `Character::setDestination` + `CharMovement::setDestination(dest,
  HIGH_PRIORITY, false)` + `setDesiredSpeed`, snap with `_setPositionDirectionAndTeleport`
  (KenshiCoop, **M**/**D**); or AK's way: AK steers its copies per frame with
  `setDirectMovement` and places them with `_setPositionSimple` + `teleportVisuallyOnly` (**M**).
- Remove: `GameWorld::destroy(body, false, reason)` on the main thread, once (all three agree,
  **M**); the engine frees the body later through its kill lists (**M**).
  AK's "GameWorld_deleteLater" is `destroy(AttachedEntity*)`, not a deferred character delete.
- AK's Spawns `08` entry carries id, strings, x/y/z (**I**) and a float; Character `09` name,
  texts and an appearance blob (protocol.md, sizes **M**, meanings **I**). Appearance is out of scope.
- Characters outside the active zones are probably not ticked, so a remote body far from the local
  player may not move at all (**I**; G7).

**Transport and message templates** (protocol.md §1–2, **M**)
- ENet 1.3.x static; 5 channels; MTU 1200, max packet 64 KiB; no checksum/compression; Position on
  channel 2 unreliable, control on channel 0 reliable; network thread services ENet every ≤2 ms.
- Framing: one message per ENet packet, byte 0 = type, little-endian unaligned, `str16` strings,
  parsers must consume the payload exactly. Position `0b` is 52 B (position, destination, walking,
  wanted speed, velocity, facing x/z, state bits, floor); Snapshot `05` packs per entity `i16`
  position delta /8, `i16` velocity /64, `u16` facing. AK's default sync period is 0.1 s
  (protocol.md, constant **M**, use **I**).
- AK's real server is a headless Kenshi running the same DLL; the shipped Core cannot host
  (AnarchyKenshi.dll.md §3, **M**/**I**): no server code to learn from beyond client expectations.

## 2. Gaps and how to close them

| # | Gap | Cheapest way to close | Risk to the step |
|---|---|---|---|
| G8 | **Which build is the user's?** Hash matches the reference but logs say 1.0.65 | Do first: plugin hashes the loaded `kenshi_x64.exe`, logs hash + any version string; also static hash of the Steam folder exe (read only). Keep AK-derived byte patterns in a gitignored folder so another build can be re-derived | **High** (a mismatch invalidates every RVA) |
| G6 | Safe per-frame hook: does an own Ogre `FrameListener` run on Kenshi's game thread, and in which order vs. Kenshi's | Do first, static: OgreMain_x64.dll version and exports (`Root::addFrameListener`). Ogre 1.x keeps listeners in a set ordered by pointer (**I**), so our order is arbitrary; default fallback is a MinHook-style detour on `MainListener_frameRenderingQueued` (B277, `0x82ac70`). Runtime: M0 logs the thread id next to a game call | **High** (everything runs in this callback) |
| G13 | **ABI**: can our toolchain call Kenshi's VS2010-runtime code safely | M0 ABI proof (below); Kenshi-facing glue passes only POD arguments and explicit out-pointers | **High** |
| G1 | Which `Character*` is the local player's (selected, squad leader, first owned?) and how to reach it from `Global_gameWorld` | Static: read AK's sender `AK 0x2f5a30` and its caller; fallback: iterate `GameWorld_tickedCharacters` + `Character_isPlayerOwned`; KenshiLib headers (G12) | High (blocks M1) |
| G2 | Verified **calling-convention sheet** for the ~6 functions M1/M4 use (position source, orientation, motionNow, goTo, place, halt): this/out-pointer/return, argument count, `MovePriority` values | Static, before M1: AK call sites + disassembly of the exe + KenshiLib headers. Already measured: motionNow and orientation (§1); positionDirect is not a getter | High (wrong signature = crash) |
| G12 | What do KenshiLib / RE_Kenshi already give us (headers, RVA files for 1.0.65, hook helpers)? | Read its headers and `RVAs/*.br` format (read-only, installed on this PC); decide with the user whether we depend on it | Medium (may save G2/G3 work, or add a hard dependency) |
| G3 | How to spawn: arguments, template, faction, squad | **Closed statically** ([remote-bodies.md](findings/remote-bodies.md) G3): squad via `createNewEmptyActivePlatoon`, then `createRandomCharacter` with that squad as owner (AK and WoK agree, KenshiLib signatures). Open: 1.0.65 offsets, squad cleanup, faction read-back, by the first spawn run | Medium (was High; first run in M4/M5) |
| G4 | How to stop the remote body's own AI (wandering, fleeing, fighting) from fighting our orders and the local player's orders | **Closed statically** (remote-bodies.md G4): detour `AI::periodicUpdate(float)` and skip our bodies (AK), fallback `Character::periodicUpdate` (KenshiCoop); `clearAllAIGoals` once; never `removeFromUpdateListMain`. Open: combat reactions, threads; runtime in M4 | Medium |
| G5 | Which call makes a body *walk* naturally to a point, and how to correct drift | **Closed statically** (remote-bodies.md G5): two candidates for an M4 A/B, KenshiCoop's path walk (+ teleport snap) and AK's direct steering (+ place); speed unit calibrated in M4 | Medium |
| G7 | World lifecycle (save load/unload, never touch stale pointers) and **zones** | **Partly closed** (remote-bodies.md G7, Removal): `~GameWorld` runs only at exit; detect reloads by an edge (KenshiCoop) or `resetGame`; keep hands, not pointers; `destroy` is deferred. Open: saves writing remote bodies, zones; runtime: M5 reload and save test, M6 starts both players together | Medium (crashes) |
| G14 | ENet peer timeout vs. main-thread stalls of ~60 s | ENet gives up after 5–30 s of silence (pinned source, **M**); the client's network thread keeps servicing ENet during a stall: a 60 s frame-thread stall keeps the connection in `kp-core-test` (**M**); in game the ~19 s start and a ~6 s save load kept it (**M**); a long in-game stall not seen yet | Low (was Medium) |
| G9 | Can two Kenshi instances run on one PC (Steam)? They would also share logs and the same game folder | Runtime, vanilla Kenshi, user approves and runs | Low: the test bot (M2) and a second PC are fallbacks |
| G10 | Pause and game speed (up to ×20): a remote body moves only while the local game runs, at the local speed | M6 criterion: both players at the same speed, or speed sync is out of scope | Low |
| G11 | Indoor floors (`Character+0xa4`) | Out of scope; remote bodies inside buildings may glitch | Low |

G8, G6, G13 come first (M0). G1-G5, G7, G12 are load-bearing analysis (Opus high), no game run.

## 3. Build order

Working name **KenshiPure** (DLL, log and cfg names can change later). Every milestone ends with a
commit; nothing from AK or the game is committed.

**M0 – toolchain, version pin, frame hook and ABI proof** — **done** (user's in-game run,
log read): G8, G6, G13 closed. Details and evidence: [kenshi-build.md](findings/kenshi-build.md).
- G8: the game runs RE_Kenshi's 1.0.65 Steam exe (`504b36…`), not Steam's 1.0.68 `a596…`; the
  plugin logs path and hash at start (**M**). So the Steam RVAs in kenshi-sites.tsv do not apply;
  Kenshi code is reached through KenshiLib.
- G12: the client is an RE_Kenshi plugin on KenshiLib 0.5.0 (mod folder + `RE_Kenshi.json`,
  export `?startPlugin@@YAXXZ`), built with VC++ 2010 (v100) x64 `/MD` from the Windows SDK 7.1 ISO
  (`scripts/setup-build.ps1`, `scripts/build-plugin.ps1`); source in `client/`.
- G6: frame hook via `KenshiLib::AddHook` on `GameWorld::_NV_mainLoop_GPUSensitiveStuff`; it runs
  on the game-window thread, ~45-75 frames/s, through a new game and a save load (**M**). Not an
  Ogre FrameListener (KenshiLib asks plugins to use its hook system).
- G13: `ou == gw`, `getFrameSpeedMultiplier`, `isPaused`, `getTimeStamp` gave plausible values for
  ~5 min, no fault (**M**). The v100 toolchain is the plugin's toolchain; server, bot and tests get a
  modern compiler at M2 (shared code then compiles under both, or the plugin talks to a modern core
  through a plain C interface).
- `scripts/install-client.ps1` switches AK off / KenshiPure on and back (Pester tests in
  `scripts/tests/`).
- Not done: where crash dumps go (Kenshi writes `crashDump<ver>_x64.dmp` in the game folder, **M**;
  Windows LocalDumps not checked).

**M1 – read own position** (M; needs G1, G2, G6, G8) — **done** (user's run, log read): position, yaw, velocity (`currentMotion`, units per second, length = `currentSpeed`) and `currentlyMoving` behave as below; reads, sources and results: [client.md](client.md) "Own pose (M1)"
- Each frame in a loaded world: find the local character, read position, yaw, velocity, walking;
  log once a second. Uses the verified calling-convention sheet (G2).
- Without the game: unit tests (quaternion→yaw, finite checks, rate limiter).
- In game: logged x/z follow the walk, velocity ~0 when standing, yaw turns; menu and reload: no crash.

**M2 – server, protocol and test bot** (M; no game needed) — **done** (tests pass on this PC;
no game run needed). Spec: [protocol-ours.md](protocol-ours.md); code in `net/`; build: [setup.md](setup.md).
- Protocol v1: Hello, Welcome, Reject, Pose, Snapshot (each entry with the pose's age in ms, so a
  stalled player is visible), PlayerJoined, PlayerLeft; AK's framing rules; 2 ENet channels
  (reliable control, unreliable poses); every message ≤ 1200 bytes.
- `kp-server`: standalone relay, 10 Hz snapshots, port configurable, up to 32 players. `kp-bot`:
  walks a circle at 10 Hz. Toolchain: `scripts/setup-modern.ps1` (MSVC 14.44 + SDK unpacked from
  Microsoft's pinned package manifest, CMake 4.5.1, Ninja 1.13.2, ENet 1.3.18; no installer,
  1.6 GB) and `scripts/build-net.ps1`.
- Tests (CTest): codec layouts, round trips, every truncation, trailing byte, oversize, NaN/inf and
  range rejection; loopback server + clients: B's pose reaches A within 300 ms (measured worst
  ~102 ms over 5 runs, set by the 100 ms tick), leave → PlayerLeft "left", rejects (version, bad
  name, full, no Hello, malformed, wrong channel, second Hello), the client's Snapshot filter.
- Not tested: ENet timeout leave (needs ≥ 5 s of silence); the setup on another PC.

**M3 – send and receive positions in game** (M; needs G14) — **done** (tests pass without the game;
the user's run: the bot's circle in KenshiPure.log, the user's walk in the server log, a save load kept
the connection; a 60 s load stall only in the test). Design, threads and test: [client.md](client.md) "Networking and
the core DLL (M3)"; the core split (step 6 of the loader plan) is `KenshiPureCore.dll`.
- `KenshiPure.cfg` (host, port, name). **A network thread** services ENet from the start and talks
  to the frame callback through queues (no game calls off the game thread): the main thread can
  stall ~60 s on load, beyond ENet's peer timeout. The callback sends its pose at 10 Hz and drains
  received poses; the thread logs timeouts and reconnects.
- Without the game: the client's net module against the real server in tests, with a fake game
  interface (`IGameAccess`: local pose, spawn, move, remove) and a simulated 60 s main-thread stall.
- In game (user): server + bot on the same PC; Kenshi log shows the bot's circle, server log shows
  the user's walk; a save load does not drop the connection.

**M4 – drive a stand-in body** (M; needs G2, G5, G4 partly)
- The stand-in is a body outside the local player's squad: one spawned with the G3 recipe or a
  nearby NPC (a player-squad member takes a different move call, remote-bodies.md G5). Drive it
  from the bot's poses: first place it each
  snapshot (proves coordinates), then walk it with a move order to the interpolated target
  (~150 ms behind) and snap when it drifts more than a few metres; try both G5 candidates. The stand-in's own AI may
  contradict our orders (G4); that is measured here.
- Without the game: interpolation/correction tests with the fake game.
- In game: the stand-in walks the bot's circle, animated, facing forward.

**M5 – spawn and remove a remote body** (L; the longest part; needs G3, G4, G7)
- On PlayerJoined spawn a body at the pose, drive it as in M4; on PlayerLeft or disconnect remove
  it with `GameWorld::destroy(body, false, reason)` from the frame hook (main thread, never inside a worker-thread callback); drop
  all bodies on world unload, recreate on load. Recipe: [remote-bodies.md](findings/remote-bodies.md); most risk is in the spawn arguments
  (G3) and the body's own AI fighting `goTo` orders (G4).
- Without the game: lifecycle tests with the fake game (join, leave, reconnect, world reload).
- In game: bot starts → a body appears and walks; stops → it vanishes; save/load/menu → no crash,
  no leftover body.

**M6 – two real players** (S if M5 holds)
- Two PCs/people (two instances on one PC only if G9 allows; logs would collide, so each needs its
  own log name), server on one; both start a new vanilla game **at the same location** (bodies
  outside active zones are probably not ticked, G7) and at the same game speed (or speed sync is
  out of scope, G10). Check: each sees the other's character walk where the other walks.

Order: M0 (done) → M1 (after the G2 sheet) → M2 (only after M0's proof) → M3 → M4; G3/G4/G7 analysis
during M3–M4; then M5, M6. Static work (G2-G5, G7, G12) needs no game and may overlap the user's runs.

## 4. Design choices

**Decided ("how")**
- C++20 + CMake for server, bot, tests and (M3) the client core: MSVC 14.44 unpacked by
  `scripts/setup-modern.ps1`; the plugin side stays VC++ 2010 (M0). Server builds on Windows; Linux
  later if wanted.
- The codec is plain C++20 and lives only on the modern side: at M3 it goes into the client core
  DLL behind the plain C interface ([plan-own-loader.md](plan-own-loader.md) "Core split"); the
  VS2010 side never compiles it.
- ENet 1.3.x (MIT), the transport AK uses, so its measured channel and size limits are a tested
  template; pinned by hash.
- Find Kenshi code by exe SHA-256 + RVAs from `kenshi-sites.tsv`; unknown exe → plugin stays
  inactive and says so. AK-derived byte patterns are kept in a gitignored folder so another build
  can be re-derived; an own pattern scan only when another build must be supported.
- Frame hook: Ogre `FrameListener` if G6 allows it, else a MinHook (BSD-2) detour on B277.
- Absolute f32 coordinates and yaw (AK's delta/i16 packing saves bandwidth we do not need for two
  players); 10 Hz like AK's default period.
- Game calls only on the frame callback thread; ENet on **a network thread** from M3 on, with queues.
- All game access behind one interface (`IGameAccess`) so everything but the Kenshi glue is tested
  without the game.
- Fail safe: any unexpected state disables the feature and logs; never kill the game.

**Decided by the user**
- Protocol: **own** (not wire-compatible with AK).
- Builds: **Steam now; GOG is planned later** (needs a GOG exe to check RVAs).
- Kenshi version: the user's main menu shows **"Kenshi 1.0.65 - x64 (Newland)"** with RE_Kenshi 0.3.5 and
  AK v0.136.1 installed (screenshot, **M**). The measured exe (`a596ab4e…aeb1`) contains "1.0.68" strings;
  it is Steam's exe, but the game runs RE_Kenshi's 1.0.65 copy (`504b36…`, G8 closed in M0).
- Server for this step: **standalone relay**; revisit before any world sync.
- KenshiLib / RE_Kenshi: the user allows depending on it if it makes the mod much simpler; Claude decides
  after reading it (G12). If we depend on it, players need it installed too.
- Licence: **AGPL-3.0** for our own source (client, server, bot), so KenshiLib (GPL-3.0) can be used and
  KenshiCoop code (AGPL-3.0) may be reused with credit ([related-projects.md](related-projects.md)); never AK
  binaries or decompiles. WorldOfKenshi (MIT) is read as a reference; its 1.0.68 offsets are re-derived on 1.0.65.
- In-game tests: **the user** runs them on this PC. The user's Kenshi has AK installed (no vanilla copy);
  no second copy is needed: AK is switched off for a test run and on again afterwards (the user runs
  AK's own scripts, or ours swaps the `Plugin=` lines; never by Claude without the user's words).
- Test server: **this PC**, friends via private VPN / LAN; no public server without a login.
- VS 2022 Build Tools (C++ workload): **yes** (limit 10 GB). Not needed for M0 (the plugin uses VC++ 2010
  from the SDK 7.1 ISO, ~1.7 GB in `tools/build/`, downloads approved by the user). At M2 its compiler
  and SDK were unpacked into `tools/modern/` (1.6 GB) instead of running the installer: same Microsoft
  packages, no admin rights, identical on every clone.

- Second player for M6: **a friend of the user** joins the server on the user's PC over a private VPN
  (e.g. Tailscale or ZeroTier) or a forwarded port.

## 5. Risks and when to stop

- **Toolchain/ABI mismatch** with Kenshi's VS2010 runtime (STL, allocator, exceptions). Mitigation:
  G13 proof in M0, POD-only glue, `/MT`.
- **Wrong build** (1.0.65 vs 1.0.68): RVAs misread or crash. Mitigation: G8 first, hash check at
  start, plugin inactive on unknown builds.
- **Frame hook ordering or threading** wrong. Mitigation: G6 first, detour fallback.
- **Spawning is harder than reading/moving** (hidden inputs, AI). Mitigation: M4's stand-in keeps
  the step demonstrable; G3 before M5.
- **The remote body acts on its own** (AI, combat, local player orders). Mitigation: G4; a neutral
  own faction; halt + re-order each update.
- **Crashes from stale pointers or worker-thread races.** Mitigation: G2, G7, deferred delete,
  lifecycle tests, validity checks before every call.
- **Load stalls vs. ENet timeout:** network thread from M3 (G14).
- **Coexistence with RE_Kenshi** and an existing cfg backup: install script never reorders or
  overwrites (G12). **Unsigned DLL blocked** by Smart App Control/antivirus (AnarchyLoader.dll.md, **M**).
- **Worlds differ** (save, NPCs, time, speed, pause); only the two bodies match, and only near each
  other. Accepted.
- **Our RE is AK-derived:** a wrong name or guess shows up as a wrong value in M1/M4 logs.

Stop and ask the user when: the installed exe is not the reference build (G8); our plugin cannot
load, game calls from the frame callback crash even for reads, or the ABI proof fails; spawning
(G3) still crashes after static analysis and two runtime tries (then: ship the stand-in approach or
more RE); any step would need AK's DLLs, its servers, or code aimed at other players.
