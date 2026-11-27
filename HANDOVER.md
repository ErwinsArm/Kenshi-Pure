# HANDOVER: work in progress (read this first if you take over)

Kept current after every small step by the session doing the work. When the work is committed and
finished, its facts move to `docs/` and this file is emptied or deleted.

Current job: milestone M5 of `docs/plan-first-playable.md` ("spawn and remove a remote body").
Brief: on PlayerJoined spawn an own body per remote player (own squad, non-player faction, read the
faction back), drive it (walk order with its AI on, RUN speed orders, teleport snap), remove it with
`GameWorld::destroy(body, false, reason)` on PlayerLeft/disconnect/stale; handle save loads; reduce the
lag (raise the body's max speed, retry AK-style direct steering); pure lifecycle tests without the game;
unattended in-game runs with `scripts/run-game-test.ps1` (Kenshi closed first; the save test only on a
COPY of the save "MP TEST"); notes (`docs/client.md` M5 section, plan, `docs/findings/remote-bodies.md`,
roadmap, knowledge map, `WORK.md`); commit by path, pull, push.

State: the M5 code is NOT committed yet (uncommitted files in this folder: see `git status`); only this file is.

## Done
- Static facts (section "New static facts" below): AK steers with a UNIT direction (M4 passed the whole velocity:
  the game multiplies it by the speed -> glitchy steer); max speed chain CharStats::moveSpeed (+0x17C) ->
  AnimationClass::setMaximumSpeed (+0x19C) -> AbstractMovementBase::maxSpeed (+0xB4); getEmptyFaction = "nofac";
  template 0 = "random character base-copy".
- Disassembly helpers were scratch scripts outside the repo (iced-x86 via `scripts/lib/x64.mjs` + KenshiLib map via `scripts/lib/kenshilib.mjs`); rebuild them if needed.
- `client/sites.tsv`: 15 new rows, all proven in both exes (feature `optional:remote-write`; `SaveManager::save` in `optional:autoload`).
- Critics done (builder full, skeptic quick). Skeptic: saves may keep bodies (test autosave, try permanent=0), nofac relations unknown (log faction and down state), the speed raise must be checked in game.
- Pure lifecycle `client/Remote.{h,cpp}` (`RemoteBodies`), wired into `client/NetFrame.{h,cpp}` (IGameAccess Remote* methods); `client/Drive.h` BodyState.down.
- Builder critic (full) done; accepted: a 2 s stall also bumps the world epoch, so bodies are now VERIFIED by hand
  (name "[KP] <player>", faction, not a player char) after a world change instead of forgotten blindly; save guard =
  detour on `SaveManager::saveGame` (only writer; autosave and save() only request) that destroys bodies and runs
  `processKillList(true)` first (cfg `remote_save_guard`, default on); dead -> replace, KO 10 s -> replace; destroy can
  be immediate or deferred, so REMOVING waits for the hand to stop resolving and asks destroy up to 3 times (no respawn
  while the old one may stand). Rows `SaveManager::saveGame`, `GameWorld::processKillList` added.
- Steer fixed in `client/Drive.cpp` (unit direction, speed capped at min(1.5 x remote, 200), limit = speed^2); DriveTest updated.
- Game adapter `client/RemoteGame.{h,cpp}` written, wired in `client/Net.cpp`; StandIn.cpp shares its AI-skip detour
  (array, `AiSkipSet`) and `DriveApplyTo`. `client/Game.h` includes CharStats.h. **Plugin builds** (build-plugin.ps1 ok).

## Next steps (in order)
1. Tests: `net/test/RemoteTest.cpp` (join, spawn, drive, leave, stale, reload forget, respawn, faults); add to
   `net/CMakeLists.txt` (kp-core-test and kp-drive-test already list `../client/Remote.cpp`). Build: `scripts/build-net.ps1`,
   `scripts/build-plugin.ps1`.
2. Test tooling: `scripts/run-game-test.ps1` -BotSeconds / -BotRestart; autoload steps `at=<s> load|save <name>`.
3. In-game runs (Kenshi closed first; G7 on a copy of "MP TEST", never on it).
4. Notes: docs/client.md M5 section, plan, remote-bodies.md, roadmap DATA, knowledge map, WORK.md; commit, pull, push.

## Design and new static facts

### New static facts (this session, 1.0.65 exe unless noted; AK = AnarchyKenshi.dll v0.133.1, base 0x180000000)

1. **Steering (AK client drive).** AK `0x50c699` calls `0x4fafb0(character, &dir, a, b)`; `dir` = (vx/|v|, ?, vz/|v|)
   built at `0x50c3c3..0x50c3ee` (a **unit** vector, M), `a` = |v| where v = sender velocity + correction, capped at
   `min(1.5 × b, 200)` when b > 0.1 else 200 (`0x50bd0a..0x50bd4e`, constants 1.5 / 200 / 0.1 read from .rdata, M);
   `b` = a per-body speed ([rbp-30h], probably the sender's speed, I). `0x4fafb0`: `setDesiredSpeed(cm, b > 0.1 && a > b ? b : a)`,
   then `setDirectMovement(cm, dir, a)` (or `a*a` when a flag at AK `0x15a66de` is set) (M). Stop = `0x4fb300`:
   desired 0, direct (dir, 0) (M).
2. **What setDirectMovement does (1.0.65 `0x3332c0`, M):** `setMovementMode(2)`, `desiredMotion` (+0x38C) = dir,
   `moveLimit` (+0x398) = sqrt(limit). In `CharMovement::update` mode 2 (`0x65f64d..0x65f6da`): Havok manual mode,
   `HavokCharacter::updateVelocity(desiredMotion, dt)`, then the step time is cut to `moveLimit / |velocity|` when
   `dt × |velocity| > moveLimit`. `HavokCharacter::updateVelocity` (`0x147c60`) multiplies the vector by
   `HavokCharacter+0x7C` (M), which `HavokCharacter::setDesiredSpeed` (`0x144070`, writes +0x7C at `0x1440a0`, M) gets
   from `CharMovement::update` as min(desired +0xBC, max +0xB4) (`0x65f5c7..0x65f5ed`, M).
   So **the vector is a direction scaled by the speed**: M4 passed the whole velocity (length ~50) → velocity ≈ 50 ×
   speed, which fits M4's "297 reported, glitchy" steer phase (cause I, the multiplication M). AK passes a unit vector.
   The steer speed is capped by the same max speed as the walk.
3. **Max speed chain (M):** `CharStats::calculateMaxRunSpeed` (`0x885590`) writes `CharStats::moveSpeed` (+0x17C) =
   (ideal run speed − 1.25?) × encumbrance × … × `athleticsMultiplier` (+0x18) + K, with overrides (crawl 11, etc.);
   `MedicalSystem::updateStats` (`0x644840`) and `updateDamageState` (`0x645dd0`) call
   `AnimationClass::setMaximumSpeed(stats->moveSpeed)` (`0x51bed0`: +0x19C = v, and clamps +0x178/+0x180 down to v);
   `CharMovement::periodicUpdate` (`0x66036c`) copies `animation(+0x3A0)->+0x19C` into `AbstractMovementBase::maxSpeed`
   (+0xB4) each periodic update. Only these two callers of setMaximumSpeed exist (xref through the thunk `0x2257`, M).
4. **Faction:** `FactionManager::getEmptyFaction` (`0x2e7ac0`) = `getFactionByStringID("nofac")` (M); gamedata.base
   has the faction record "No Faction" / `nofac` (M, file bytes). Its relations to others: unknown.
5. **Template null:** `createRandomCharacter` with template 0 looks up "random character base-copy" (or "random
   character base" when the faction is a player's, `Faction+0x250`) (`0x582cb3..0x582d81`, M strings).

### Design

**Config** (`KenshiPure.cfg`): `remote=on|off` (default on when networking is on; off if `standin` is set — M4's tool
stays, never both), `remote_drive=order|steer` (default order), `remote_faction=<string id>` (default `nofac`),
`remote_maxspeed` (default 150; 0 = leave the body's own), `remote_range` (default 3000), `remote_settle` (s, default 3),
`remote_squad_permanent` (default 1, AK and WoK pass true).

**Pure lifecycle** (new `client/Remote.{h,cpp}`, both toolchains, C++03, tested by `net/test/RemoteTest.cpp` with a
fake game): `RemoteBodies` keeps per remote player `{playerId, slot, state, track, planner, timers}`; the game adapter
keeps a hand per slot (never a pointer across frames). States: NONE → (fresh pose, world live and settled ≥ remote_settle
s, within remote_range of the local character, not in backoff, ≤ 8 bodies, one spawn per frame) SPAWN → LIVE → (PlayerLeft,
connection lost / Welcome again, no snapshot for 3 s, pose age > 15 s, out of range + 500, 3 failed commands, hand no
longer resolving) REMOVING → (hand no longer resolves, or 10 s) NONE. Failed spawn: backoff 5 s, after 3 failures give
up on that player (logged). World change (no own character, or the world epoch changes: menu, load, load stall) or Stop:
forget all hands with **no** game call; bodies come back from the next snapshot after the settle time. A removed
player who is still there respawns after REMOVING ends.

**Drive** per LIVE body: M4's `RemoteTrack` + `DrivePlanner`, mode order (walk order `Character::setDestination` with
its own AI on, speed orders RUN, `setDesiredSpeed`, teleport snap past 300 units) or steer (AK's values: unit
direction, desired = |v| capped at min(1.5 × remote speed, 200), limit = |v|; AI skipped; place = `_setPositionSimple` +
`teleportVisuallyOnly`). The planner's steer command changes to a unit direction for the M4 stand-in too.
Every frame before the command: max speed = `remote_maxspeed` written into the body's `CharStats::moveSpeed`, through
`AnimationClass::setMaximumSpeed`, and into `AbstractMovementBase::maxSpeed` (own body only).
Lag measured per body: gap to the drive target (150 ms behind) and gap to the newest pose extrapolated to now; logged
per second and as a summary at removal.

**Game adapter** (`client/Remote.cpp` game side → `client/RemoteGame.cpp`), frame thread, each call in `__try`:
- Spawn: faction `GameWorld::factionMgr->getFactionByStringID(remote_faction)`; squad
  `faction->createNewEmptyActivePlatoon(nullptr, permanent, pos)`, `platoon->getActivePlatoon()`; body
  `theFactory->createRandomCharacter(faction, pos, activePlatoon, nullptr, nullptr, 1.0f)`; read back
  `getFaction()` (log if different); `clearAllAIGoals()` once; `setName(<remote player name>)`; build its hand,
  check it resolves back; then teleport to pose + yaw.
- Remove: resolve hand, `GameWorld::destroy(body, false, "KenshiPure: player left")` once; then poll the hand.
- AI skip: the existing `AI::periodicUpdate` detour checks a small array of pointers (≤ 16, written on the frame
  thread) instead of one.
- New rows (sites.mjs, proven in both exes): FactionManager::getFactionByStringID, GameWorld::factionMgr,
  Faction::createNewEmptyActivePlatoon, Platoon::getActivePlatoon, RootObjectFactory::createRandomCharacter,
  GameWorld::destroy(RootObject*,bool,const char*), Character::clearAllAIGoals, vslot Character::setName,
  Character::stats, CharStats::moveSpeed, CharMovement::animation, AnimationClass::setMaximumSpeed,
  SaveManager::save (test only).

**Unattended tests** (`scripts/run-game-test.ps1`): new `-BotSeconds` (bot leaves), `-BotRestart` (rejoin after a pause),
and autoload steps in `autoload.txt` after the first line: `at=<s> load <save>` / `at=<s> save <save>` (posted to the
game window like the first load, so they run on the main thread outside the frame hook). G7 orphan test on a copy of
"MP TEST" (folder copy, deleted afterwards): load the copy, body spawns, `save` into the copy, `load` the copy, log
every character carrying a remote body's name before our respawn. Fallback if an orphan shows: sweep on load
(characters of the remote faction carrying a remote body's marker name get destroyed) or remove bodies before a save.

**Out of scope:** appearance, names over heads beyond setName, combat, items, squad cleanup (`destroyPlatoon`) unless the
tests show leftovers.
