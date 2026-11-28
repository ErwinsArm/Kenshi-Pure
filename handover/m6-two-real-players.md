# Hand-over: M6 "two real players" (read this first if you take over)

One note per job in `handover/`, named after the job. The session doing the job keeps it current after every
small step (job, done, next steps), so another session or account can take over. When the job is finished its
facts move to `docs/` and the note is deleted.

Job: M6 "two real players" (`docs/plan-first-playable.md` M6). State: **ready for the user's test with the
friend; nothing else to do before it.** Facts are in `docs/client.md` ("Game speed (M6)", "Multiplayer package
(M6)", "Exit"), `docs/playing-together.md` (steps for both people), `WORK.md` (M6 gaps).

Decided by the user: while connected there is no pause and no faster speed, always x1 (G10).

Done: speed lock (client/SpeedLock.cpp), cheats off once connected, package (`scripts/make-package.ps1`),
install-client.ps1 package mode, both critics' points applied. The build with the clean leave at game exit
(KenshiPure 0.0.8-m6, loader 0.0.4-m3) builds, its tests pass, and unattended runs A (1.0.65) and B (1.0.68) with
speed, pause, save and quit steps passed. The package `build\package\KenshiPure-0.0.8-m6.zip` (gitignored; from a
fresh clone: `scripts/build-plugin.ps1`, `scripts/build-net.ps1`, `scripts/make-package.ps1`) is built from it and
installed on the user's Kenshi (host 127.0.0.1, name player).

The user's next steps (`docs/playing-together.md`): set up Tailscale with the friend; send the friend the zip (a zip
of the same name sent before the loader 0.0.4-m3 build is outdated); host starts `server\Start server.cmd`; both
start a new game with the same start and meet; tell Claude what they saw.

Next, after the user's test:
1. Read the user's `<Kenshi>\mods\KenshiPure\KenshiPure.log`, the friend's copy of theirs and the host's
   `server\kp-server.log` (in the package folder the server was started from; copy it before running
   `make-package.ps1` again, which replaces `build\package\KenshiPure-<version>\`). Look for: `net: welcomed`,
   `player … joined`, `remote: body "[KP] <name>" built` / `spawned a body`, gap values, `speed: pause blocked
   (pause key|dialogue|build mode)`, `the character editor's pause passed`, `exception`.
2. Fix what fails (cause, not symptom); then mark M6 done in plan, roadmap (`DATA`), knowledge map, WORK.md.

Shared game folder: other sessions run games in the same Kenshi folder; before a run check that kenshi_x64 is not
running and that `game-lock.txt` in the session group's shared notes folder (outside the repo, named in the
session's brief) is absent, write it, delete it after the run.
