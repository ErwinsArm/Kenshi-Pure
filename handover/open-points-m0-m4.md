# Hand-over: open points of M0-M4 (finished)

Job: the open points left from M0-M4 (worktree branch `claude/fix-open-points-330082`). State: **finished and
committed** (commit "Open points of M0-M4: clean leave at game exit, flaky core test, timer, stall and start-time
measurements"), merged into `main` together with M6. Facts are in `docs/client.md` and `WORK.md`.

Done: clean leave at game exit (Kenshi never runs `dllStopPlugin`; loader 0.0.4-m3 hooks the end of
`Root::startRendering` and calls `kpStop`, the server logs "left" instead of a timeout), the flaky `kp-core-test`
check, the timer, stall and start-time measurements, test steps `stall`, `quit` and `select` in `run-game-test.ps1`.

Decided (the user left it to us): a server password and per-IP limits are not built now. LAN and Tailscale need
neither; they come before anyone runs a public server (`WORK.md`, M2 gaps).

Nothing is left for this job. Delete this note once the M6 note no longer points to it.
