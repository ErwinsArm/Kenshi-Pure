# Remote bodies: spawn, move, AI suppression, removal, world lifecycle

What our client needs to show another player as a body in the local world (plan gaps G3, G4, G5,
G7). Found by static analysis only; nothing was run.

Labels: **M** measured (disassembly, decompile, strings, header text, source text read),
**D** documented (a project's own notes say it was seen in a game run), **I** inferred.

## Sources and which build each is for

| Source | What was read | Build |
|---|---|---|
| AK | `AnarchyKenshi.dll` v0.133.1, SHA-256 `c3d2419d1c02b516552032e9037d29ec1a1b88661199ddac7775f601d97b51bb`, image base `0x180000000`; addresses below are AK RVAs. Ghidra 12.2 decompile and llvm-objdump disassembly | calls Kenshi through its site tables ([hooks.md](hooks.md)); the RVAs it carries are for Steam 1.0.68 (`kenshi_x64.exe` SHA-256 `a596ab4e407c67b58599c54ffb32dc1bf2b64510cdebd3fa9359ef05a576aeb1`, image base `0x140000000`) |
| KL | KenshiLib 0.5.0 headers (git tag `v0.5.0`, `tools/build/kenshilib-src`) | names and signatures; KenshiLib maps each name to an address at run time from RE_Kenshi's RVA files, and the installed RE_Kenshi has only `Steam_1.0.65.br` and `GOG_1.0.65.br` (**M**, file list; their format is not plain brotli and was not decoded). The `// RVA =` comments are for a build we cannot name ([kenshi-build.md](kenshi-build.md)) |
| KC | KenshiCoop, github.com/nhoral/KenshiCoop, commit `5a761e19` (2026-08-10), AGPL-3.0: `docs/API_REFERENCE.md`, `docs/REPLICATION_PITFALLS.md`, `src/plugin/game/*.cpp`, `src/plugin/sync/ReplicatorDrive.cpp`, `Plugin.cpp` | 1.0.65 Steam, through KenshiLib (README) |
| WoK | WorldOfKenshi, github.com/Blast-Pop/WorldOfKenshi, commit `8e6d6c2c` (2026-11-16), MIT: `docs/re/*.md`, `client/src/game/{spawner,orders,avatars}.cpp` | 1.0.68 Steam, raw RVAs (same exe hash as AK's reference) |

The game the user runs is the 1.0.65 exe ([kenshi-build.md](kenshi-build.md)). Our plugin calls
everything below **by KenshiLib name** (`KenshiLib::GetRealAddress(&Class::member)`, virtuals
through the object or the `_NV_` twin, **D** KC API_REFERENCE §2). Every RVA in this note is
1.0.68 Steam and is only there to tie the sources together; what AK and WoK do was seen on 1.0.68
and is carried to 1.0.65 unchecked. Field offsets come from KL headers and are what KC uses on
1.0.65 (**M**, KC source); AK and WoK use the same offsets on 1.0.68 (**M**). No offset was checked
in the 1.0.65 exe (`RE_Kenshi\Kenshi_x64.exe`, `504b362c…`): the first M4 run does that by logging
them. The plugin reaches the world through KenshiLib's global `GameWorld* ou` (`Globals.h`, **M**).

### AK names ↔ KenshiLib names

AK's site-table names (in [kenshi-sites.tsv](kenshi-sites.tsv)) are AK's own; many were guesses.
Within one code region, KenshiLib's comment RVA and AK's 1.0.68 RVA differ by one constant (e.g.
`0x780` for 260 rows, `0x1260` for the rows between `0x7916d0` and `0x7f90b0`). Matching
each AK row by the delta its neighbours share gives KenshiLib names for 622 rows with support ≥ 8,
listed in [ak-kenshilib-names.tsv](ak-kenshilib-names.tsv) (**M** method, **I** each match). The
names used here have support ≥ 11 (support = neighbours sharing the delta); for most of them
AK's own use also fits the KenshiLib name, which is a second, weaker check. One AK name is plainly
wrong (`Character_setOnScreenFlag`):

| AK name | 1.0.68 RVA | KenshiLib member |
|---|---|---|
| RootObjectFactory_spawnCharacter | `0x5836e0` | `RootObjectFactory::createRandomCharacter` |
| Faction_createSquad | `0x7f4420` | `Faction::createNewEmptyActivePlatoon(GameData*, bool permanent, const Vector3&)` |
| Platoon_liveSquad | `0x268720` | `Platoon::getActivePlatoon()` |
| GameDataContainer_getData_string / _recordNamed | `0x6bd4e0` / `0x6bfda0` | `GameDataManager::getData(sid)` / `getDataByName(name, itemType)` |
| GameWorld_destroy_RootObject_bool_char | `0x799af0` | `GameWorld::destroy(RootObject*, bool justUnloaded, const char*)` |
| GameWorld_deleteLater | `0x79a740` | `GameWorld::destroy(AttachedEntity*)`, **not** a deferred character delete |
| GameWorld_drainDeletesHook | `0x79d8a0` | `GameWorld::processKillList(bool)` |
| GameWorld_removeFromTick / addToTick | `0x7874e0` / `0x787c70` | `removeFromUpdateListMain` / `addToUpdateListMain` |
| AI_thinkDirect | `0x5112b0` | `AI::periodicUpdate(float)` |
| Character_goTo | `0x5c95b0` | `Character::setDestination(const Vector3&, bool shift)` |
| CharMovement_goToDirect_Vector3_MovePriority_bool | `0x661270` | `CharMovement::setDestination(const Vector3&, UpdatePriority, bool)` (vtable `0x90`) |
| CharMovement_placeDirect | `0x65e870` | `CharMovement::_setPositionSimple(const Vector3&)` (vtable `0xC8`) |
| CharMovement_steer | `0x333300` | `CharMovement::setDirectMovement(const Vector3& d, float limit)` |
| AbstractMovementBase_wantSpeed_float | `0x3332f0` | `CharMovement::setDesiredSpeed(float)` |
| Character_placeVisual | `0x5c8540` | `Character::teleportVisuallyOnly(const Vector3&, const Quaternion&)` |
| Character_setOnScreenFlag | `0x5c7920` | `AI::setAggressionLevel(...)` (AK logs "could not set the stance" around it) |
| GameWorld_destructor | `0x86d650` | `GameWorld::~GameWorld()` |

## G3 – spawning a body

**Recipe (three sources agree):**

1. Template: `GameDataManager::getData(sid)` or `getDataByName(name, CHARACTER)`, on
   `GameWorld+0x20` (`gamedata`). AK's `spawn` console command tries item type 1, then `0x4c`
   ("no character or animal template named", `AK 0xc87b70`, **M**); KC tries `CHARACTER` then
   `ANIMAL_CHARACTER` (**M**). Template `0` = the game's random default (**D** WoK). The `sid` is a
   VS2010 `std::string`; our plugin is built with the same VS2010 compiler and dynamic CRT
   (`MSVCR100`) as Kenshi, so a local `std::string` has Kenshi's layout and heap (**I**, from
   [kenshi-build.md](kenshi-build.md)); KC passes one the same way (**M**).
2. Faction: a non-player faction. WoK uses "Drifters" for now and plans an own faction in a data
   mod (**D**); KC takes the faction by string id or any nearby non-player faction (**M**); AK's
   proxy builder refuses the player's own faction and "builds under the borrowed one"
   (`AK 0x3212d0` strings, **M**). After spawning, read the faction back (`getFaction()`): AK logs
   when a body "was built OUTSIDE the faction it was built for (the factory chose otherwise)"
   (`AK 0x393fe0`, **M** string).
3. Squad: `Faction::createNewEmptyActivePlatoon(nullptr, true, pos)` returns a `Platoon*`; its
   `ActivePlatoon*` is `Platoon::getActivePlatoon()` (KL field `Platoon+0x1D8`).
   AK `0x393fb0`: `createSquad(faction, 0, 1, pos)` then `Platoon_liveSquad` (**M**); WoK: same
   arguments, then reads `Platoon+0x1d8` (**M**, "verified in game" **D**).
4. Body: `RootObjectFactory::createRandomCharacter(faction, pos, owner, template, home, age)` on
   `GameWorld+0x4A0` (`theFactory`). Returns `RootObject*` (a `Character*`) or null.
   - `pos` is `Ogre::Vector3` by value in the header; the x64 ABI passes it as a pointer to a copy.
     AK's wrapper `0xcd7fd0` copies 3 floats and passes their address; WoK passes `const float*`
     (**M** both). Declare the typedef with the by-value `Ogre::Vector3` (KC, **M**) or as a pointer.
   - `owner` = the new `ActivePlatoon*` (AK player bodies, call at `0x394572`; WoK). AK `0xc87b70` warns
     when nothing joined the update list after a spawn: "the body exists but is not being
     simulated. It likely needs a container" (**M** string). KC and AK's proxy copies pass
     `owner = 0` (**M**); KC: "`owner` may be null (the factory makes a free body)". Use the squad.
   - `home = 0` (all three). `age`: AK `1.0` (`0x453450`, `0xc87b70`; its player bodies use a computed value, `0x392a20`); WoK `1.0`; KC `25.0`
     or the source body's age (animal size). Humans: cosmetic (**I**, KC comment).
   - Main thread only, under SEH (KC rule, **D**).
5. Optional after spawn: `Character::setSquadMemberType` (WoK RVA `0x6215c0` = KL
   `setSquadMemberType`, 2 = leader **D**), name (`setName` virtual), look (WoK `.bod2` route).
   AK pins the race by editing the template's race list around the call (`AK 0x2bdb90`
   "race-pin"); out of scope for M5.

Squad lifetime: AK and WoK pass `permanent = true` and never remove the squad (**M**). KL has
`Faction::destroyPlatoon` / `removePlatoon` (**M**, header); nobody calls them for this, so whether to
call one after the body is gone, and what `permanent` changes, is open (each join could otherwise
leave an empty permanent squad, **I**).

Not solved statically: whether a body built into its own squad near the player is ticked when the
player walks away (zones, G7), and which faction relations make local NPCs ignore it.

## G4 – keeping the body's own AI out

Kenshi runs AI per character from two places: `Character::periodicUpdate()` (KL, virtual) and
`AI::periodicUpdate(float)` (`Character+0x650` → AI). Options seen:

| Way | Who | Effect |
|---|---|---|
| Detour `Character::_NV_periodicUpdate`; skip the original for bodies in a set rebuilt every frame | KC `installAiSuspendHook`, `EngineInternal.cpp` (**M**) | "no autonomous re-tasking while the body keeps animating and executing its current/injected action" (**D** KC) |
| Detour `AI::periodicUpdate(float)` (`A11`, AK name "AI::thinkTick"); skip the original for bodies in a published list (≤128 pointers, double-buffered) | AK installer `0x5351c0`, detour `0x534970`, list test `0x44eed0` (**M**) | AK help: "the avatar AI hush: no word, no decisions … an attacker within 300 or lost flesh lets the AI decide" (**M** string, server side) |
| `Character::clearAllAIGoals()` once after spawn; `separateIntoMyOwnSquad` for town NPCs | KC spawn (**M**) | quiets goals; KC: do **not** call it while walking, it cancels our destination (**D**) |
| `GameWorld::removeFromUpdateListMain(char)` | KC `suppressNpc` (**M**) | KC: "freezes the movement controller (walk + teleport both no-op)" (**D**); only for bodies to hide, never for driven bodies |

Recommendation for M4/M5: skip `AI::periodicUpdate(float)` for our bodies (AK's choice; the
narrower cut: at 1.0.68 `0x5112b0` it ticks the task system and the state broadcast, with the
owning character at `AI+0x2F8`, **M** by both critics and `AK 0x534970`), plus one
`clearAllAIGoals()` right after spawn. Fallback if the body still re-tasks: KC's
`Character::_NV_periodicUpdate` skip (it also covers movement update calls; whether it stops
health, bleeding or KO is unknown). Keep the skip list readable from another thread: AK's detour
uses locked counters and a double-buffered list, and AK logs "the AI tick did not finish within
… ms" (`AK 0x3cd40`), so the AI tick probably runs off the main thread (**I**). Our bodies are
never in the local player's squad, so the player cannot order them (**I**: not in
`PlayerInterface::playerCharacters`). AK's hush lines are server-side help text; what its client
does with its copies is the direct steering in G5.

Only a run shows: whether a skipped `periodicUpdate` also stops combat reactions to local NPCs,
and whether the body still plays walk animations (KC says yes, **D**).

## G5 – walking to a point and drift correction

Four ways are in use:

| Call | Who | Notes |
|---|---|---|
| `CharMovement::setDestination(dest, HIGH_PRIORITY, false)` + `setDesiredSpeed(speed)`, through `Character+0x640` | KC `walkTo` calls this **and** the next row for every body, "each body obeys the one that applies to it" (`EngineEntity.cpp`, **M**); WoK slot `0x90` | KC: "overrides the NPC's autonomous movement goals"; WoK: "called alone, the AI overrides it" (WoK had no AI suppression) |
| `Character::setDestination(pos, false)` (the click-to-move path) | KC `walkTo` (above) and `orderMoveTo` (**M**) | a player body ignores a bare `CharMovement::setDestination` (**D** KC) |
| `AITaskSystem::addJob(TASK_MOVE=1, hand{}, pos, true)` on `*(Character+0x650)+0x20` | WoK `orderMoveTo` (1.0.68 RVA `0x5086d0` = KL `addJob`, **I** by delta; WoK calls the last bool "playerOrder", KL "shift") | walks with own pathfinding and animations, ~5 units/s (**D** WoK, player character) |
| per frame `CharMovement::setDirectMovement(dir, limit)` + `setDesiredSpeed(speed)`, place when off | AK client drive `0x503b40` → `0x4fafb0` / `0x4fb300` (**M**) | AK steers its copies directly, no pathfinding |

Drift correction (snap):
- KC: `CharMovement::_setPositionDirectionAndTeleport(pos, rot)` (vtable `0xC0`) without
  `halt()` while moving ("no-halt teleport keeps the clip phase advancing"); `halt()` once only
  when the body comes to rest, since `halt()` every frame freezes the clip on frame 0 (**D**).
- WoK: `_setPositionSimple(const Vector3&)` (vtable `0xC8`) when more than 40 units behind;
  writing `Character+0x48` or the movement position is undone every frame (**D**).
- AK place `0x4fba20`: `_setPositionSimple` then `Character::teleportVisuallyOnly(getPosition(),
  getOrientation())` (**M**).

Re-issue rules from KC (**D**): re-issue the move only when the lead point moves (a new order
every frame restarts the path and stutters); lead point ≈ 0.6 s ahead along the sender's velocity;
catch-up speed proportional to the gap, capped at 2.5×.

Speed: `setDesiredSpeed(float)` only stores the float at `CharMovement+0xBC` (1.0.68 `0x3332f0`,
**M** by the critic); KL names `+0xB8` `currentSpeed` and `+0xBC` `desiredSpeed`. Unit: world units
per game second (M1, [client.md](../client.md) "Own pose"; the player walks ~72). The speed orders override
it for NPCs and the max speed caps it (below).

No source ran the M4 chain (spawned body + AI skip + one of these drives) on 1.0.65. M4 tries two
candidates on the same stand-in and keeps the one that looks right:
- **A (path walk):** KC's dual call, `Character::setDestination(target, false)` and
  `CharMovement::setDestination(target, HIGH_PRIORITY, false)`, plus `setDesiredSpeed`; re-issued
  only when the lead point moves; snap with `_setPositionDirectionAndTeleport` when the gap passes
  a few metres.
- **B (direct steer):** AK's shipped way: every frame `setDirectMovement(direction, limit)` and
  `setDesiredSpeed`, place with `_setPositionSimple` + `teleportVisuallyOnly` when off.

M4 runs on 1.0.65 (an existing NPC, [client.md](../client.md) "Stand-in body (M4)", **M**): `Character::setDestination`
with the NPC's AI on moved it 95 % of the time (gap ~107 at 50 units/s, 2 snaps in 45 s); `CharMovement::setDestination`
alone with the AI skipped 53 % (21 snaps); B (`setDirectMovement`) 88 % but with nonsense speeds and a glitchy look.
Speed: set the speed orders to RUN (`setDesiredSpeedOrders`) before `setDesiredSpeed(float)`, else an NPC walks at
its walk speed (15); the body is capped at its own max speed (`+0xB4`, ~52). Skipping `AI::periodicUpdate` ran
without fault (the AI tick runs on another thread than the frame thread); a busy NPC needs its AI on to get up.
**Use for M5: the walk order with the AI on, teleport snap.**

## Removal

`GameWorld::destroy(obj, false, "reason")` on the main thread, once. All three agree on the call: AK
`0x3b8fe0` / `0xcd6740` pass `justUnloaded = 0` and "anarchy: owner left" (**M**); KC `false`,
`"coop-…"` (**M**); WoK `0`, "World of Kenshi: player left" (**M**, "verified" **D**). AK resolves
the body from its stored `hand` right before the call (`0x321e48` `hand::getCharacter`, **M**).

The engine removes it **later**, not at once: in the 1.0.68 exe, `destroy` puts the object into
the kill lists (KL `killListPhase0` `+0x7D0`, branch at `0x799b82` that returns false;
`killListPhase1` `+0x810` with the float 5.0 at `0x79a6ec`; a character also goes to
`mainUpdateListRemovalQueue` `+0x888`), and `GameWorld::processKillList` frees it (**M**, both
critics; the branch condition and the unit of 5.0 are unread). So after `destroy`: mark the entry
"removing", never call `destroy` twice on it, do not drive it, and treat it as gone only when
`GameWorld::getIsInKillList` is false and its hand stops resolving. A player who rejoins during
that window gets a new body. AK's "GameWorld_deleteLater" is `destroy(AttachedEntity*)`, not a
character delete.

## G7 – world lifecycle

- `GameWorld` is one static object (WoK: global at 1.0.68 RVA `0x2134110`, **M**/**D**); its
  destructor runs at game exit, not on a save load. AK hooks `~GameWorld` only for its exit guard
  ("the game is tearing its world down after the main loop", `AK 0xc1b450`, **M**). So a save
  load replaces the world's contents in place, and our stored bodies become invalid without any
  destructor call (**I**).
- AK hooks `SaveManager::load(...)`, `save`, `newGame` as a "save-guard" (`AK 0xc11a40`, **M**);
  KC hooks `SaveManager::load` to queue load edges (**M**). Open risk: a local save or autosave
  while remote bodies exist may write them and their squads into the save, so they come back as
  orphans on load (**I**). M5 tests it (save with a remote body present, reload, look for it);
  fallbacks: remove our bodies before a save, or block saves as AK does.
- KenshiLib also offers `GameWorld::resetGame()`, `_clearAndDestroyGameWorldStuff()`,
  `isLoadingFromASaveGame()` and the field `gameResetting` (`+0x8BA`) (**M**, header). No source
  uses them; a detour on `resetGame` may give an exact "world is going away" edge (**I**).
- Detecting "in a world": KC `gameplayLive = gw->player && gw->player->playerCharacters.size() > 0`,
  an edge from live to not-live and back (longer than a minimum) is a world reload; then every
  pointer map is reset (`tickWorldSwapEdge`, `sessionResetForWorldReload`, **M**). WoK: the
  selected squad at `PlayerInterface+0x2a8` is a `Platoon` (by RTTI); the title screen already has
  a `PlayerInterface` (**D**).
- Never keep a bare `Character*` across frames: store the body's `hand` (`RootObjectBase::getHandle`)
  and re-resolve with `hand::getCharacter()` each use; a destroyed body's hand stops resolving
  (KC pitfall §15, written about ground items, extended here to characters, **I**; AK keeps hands and calls `hand_characterPtr` before each use, **M**).
- Zones: KC uses 4608-unit zone cells (**D**); bodies in unloaded zones are destroyed by the game
  ("corpse unloaded", AK strings, **M**). M6 keeps both players together.
- What only a run shows: whether our frame hook runs during the load screen (KC logs it as a probe),
  and how long the not-live gap is.

## For M4 and M5

1. M4 stand-in: a body that is not in the local player's squad, spawned with the G3 recipe (so M4
   also tests the spawn), or a nearby NPC. Try G4's AI skip and both G5 candidates; log the
   1.0.65 offsets used, gap, `+0xB8` speed and whether it animates.
2. M5: spawn into its own squad of a neutral faction, read the faction back; keep
   `{hand, player id, state}`; remove with `GameWorld::destroy(…, false, …)` once and wait for the
   kill list; on a world-reload edge forget all hands and rebuild from the next snapshot; test a
   save and reload with a remote body present; decide the squad cleanup.
3. No source ran this exact chain on 1.0.65; the first M4/M5 runs are the proof.
