# HANDOVER: work in progress (read this first if you take over)

A session doing a long job keeps this file current after every small step (job, done, next steps), so
another session or account can take over. When the job is finished its facts move to `docs/` and this
file says so.

**Job (the user's words: no workarounds, no crashes, work alone until it is completely done):** finish M5
properly before M6.
1. Saves: a body must stay in the world while the game saves and must not be written into the save
   (today the guard destroys it and it comes back 5 s later: a workaround).
2. Close the M5 gaps from `WORK.md`: empty squads left after a body goes (`Faction::destroyPlatoon`),
   how local NPCs treat a "No Faction" body, bodies far from the local player (zones), the `[KP]` name as
   the only identity check, the drive `auto` switch rules.
3. Confirm no crash over repeated unattended runs (run 6's crash came from the dropped forced kill list).

**Done (static, 1.0.65 exe, M):**
- Saves: `Faction::saveState` (0x372920) calls `Platoon::serialiseEverything` (vslot 0xB8, base 0x372db0) per squad
  and ignores the result (0x372d05, 0x372d54); that function is the only writer of a squad's file (→
  `ActivePlatoon::serialiseEverythingToDisk` 0x4feb10 → `saveToDisk` 0x36bb50 "platoon/<id>.platoon", only callers)
  and itself returns NULL for squads it does not save (dead, far from players, 0x372dfb..0x372ef9).
  → code: detour on it returns NULL for our squads (pointer + platoon string id check); SaveManager::saveGame guard gone.
- Squads: `Faction::updateActivePlatoons` (0x6ba810): a squad whose `update()` returns false (empty non-player squad,
  or `ActivePlatoon::unloadCheck` 0x4fe140 false = outside loaded zone bounds minus 170 for 4.5 s) is deactivated if
  permanent (kept as unloaded squad, would reload later as a stray body!) else `destroyPlatoon` (gone).
  → code: squads temporary by default (`remote_squad_permanent` default 0); spawn only where the zone is loaded,
  remove our body after 1 s outside (`RemoteGameLoadedAt` mirrors unloadCheck).
- NPC enemies: `FactionRelations::isEnemy` = relation <= -30 (0x6b23b3). → code logs every nearby faction's relation
  towards ours at each spawn.
- Stray sweep now also needs our faction (not only the "[KP] " name).
- New rows (both builds proven): Platoon::serialiseEverything, isPersistentSquad, getPlatoonStringID,
  GameWorld::zoneMgr, ZoneManager::getZoneMap(Vector3), ZoneMap::isLoadedMT/isBeingLoadedMT/getBoundsMinusUnloadedEdges,
  Faction::relations, FactionRelations::getFactionRelation. Version 0.0.7-m5. kp-remote-test passes (new zones test);
  kp-core-test failed once in build-net, passed on rerun (flaky, unrelated). Built and installed.

**Next:** run 1: `run-game-test.ps1 -GameDir <Kenshi> -Save "MP TEST" -SaveCopy -Seconds 200 -Steps "at=60 save SAVE
NAME;at=100 load SAVE NAME"` — expect "the save skips our squad", status lines go on through the save, after the load
no "kept" and no STRAY; squad "temporary"; relation lines. Then: leave/rejoin run (empty squad gone?), crash-free
repeats, notes.
