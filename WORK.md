# WORK

Goal: an own Kenshi multiplayer (own server + client mod) that works better than AnarchyKenshi and is fully under the user's control; everything is extracted from the client DLLs first (the original server is not available).

- Route: write own server and own client mod fresh, using AnarchyKenshi only as a reference (the goal is full control; reusing its DLLs would tie the project to closed, self-updating third-party code). Next: plan the first playable step (connect, see each other move).
- The installed mod in the Steam Kenshi folder is reportedly v0.136.1 (protocol `0x88`, ABI `0x60`, new export `AnarchyHostOfflineAccount`; seen by a critic, not yet re-measured); diff it against the repo's v0.133.1 before building against either.
- Host ABI (`AnarchyHost*` signatures from their call sites in AnarchyKenshi.dll): only needed if the route ever changes to reusing AnarchyKenshi's server code.
- Spine: only 5 of 48 topic layouts are decoded (`docs/findings/spine.md`); the large ones (Squads, Limbs, Machine, Stores, Groups) are open.
- Server-message field meanings (`u32 head`, Welcome `u16`, Redirect `u32`, snapshot flag bits, Position floats) are open (`docs/findings/protocol.md`).
- `AnarchyLauncher.dll` (launcher, server browser, self-updater) is not analysed.
- Runtime logs in `<Kenshi>\mods\AnarchyKenshi\` (`AnarchyKenshi*.log`, crash dumps; they contain other players' chat) could confirm the live build detection, scan result and installed hook count; the user allows reading them (not committing them), but `AnarchyKenshi-logins.txt` there likely holds credentials (never read or commit it).
