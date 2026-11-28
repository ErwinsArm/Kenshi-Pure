# Hand-over: M6 "two real players" (read this first if you take over)

One note per job in `handover/`, named after the job. The session doing the job keeps it current after every
small step (job, done, next steps), so another session or account can take over. When the job is finished its
facts move to `docs/` and the note is deleted.

Job: M6 "two real players" (`docs/plan-first-playable.md` M6). State: **prepared and committed; waiting for the
user's real test with the friend.** Facts are in `docs/client.md` ("Game speed (M6)", "Multiplayer package (M6)"),
`docs/playing-together.md` (steps for both people), `WORK.md` (M6 gaps).

Decided by the user: while connected there is no pause and no faster speed, always x1 (G10).

Done: speed lock (client/SpeedLock.cpp), cheats off once connected, package (`scripts/make-package.ps1` ->
`build\package\KenshiPure-0.0.8-m6.zip`, gitignored, rebuild it from a fresh clone after the two build scripts),
install-client.ps1 package mode, run-game-test.ps1 `-NoReKenshi`, unattended runs A (1.0.65) and B (1.0.68) passed,
both critics' points applied. Merged with the open-points work (`handover/open-points-m0-m4.md`): the merged build
(KenshiPure 0.0.8-m6, loader 0.0.4-m3 with the clean leave at game exit) builds and passes its checks, but no game
run was made with it yet, and the user's Kenshi still has the pre-merge 0.0.8-m6 package installed (host 127.0.0.1,
name player). Before the friend test: rebuild the package (`scripts/build-plugin.ps1`, `scripts/build-net.ps1`,
`scripts/make-package.ps1`) and give both players the new zip.

Next, after the user's test:
1. Read the user's `<Kenshi>\mods\KenshiPure\KenshiPure.log`, the friend's copy of theirs and the host's
   `server\kp-server.log` (in the unzipped package folder). Look for: `net: welcomed`, `player … joined`, `remote: body
   "[KP] <name>" built`, gap values, `speed: pause blocked (pause key|dialogue|build mode)`, `the character editor's pause
   passed`, `exception`.
2. Fix what fails (cause, not symptom); then mark M6 done in plan, roadmap (`DATA`), knowledge map, WORK.md.

Shared game folder: other sessions run games in the same Kenshi folder; before a run check that kenshi_x64 is not
running and that `game-lock.txt` in the session group's shared notes folder (outside the repo, named in the
session's brief) is absent, write it, delete it after the run.
