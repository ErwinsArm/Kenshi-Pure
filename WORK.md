# WORK

Goal: an own Kenshi multiplayer (own server + client mod) that works better than AnarchyKenshi and is fully under the user's control; everything is extracted from the client DLLs first (the original server is not available).

- Route: write own server and own client mod fresh, using AnarchyKenshi only as a reference (the goal is full control; reusing its DLLs would tie the project to closed, self-updating third-party code). Next: plan the first playable step (connect, see each other move).
- Reference version is fixed at v0.133.1 (the repo copy). The mod updates almost daily (the Steam install was reportedly v0.136.1, protocol `0x88`, ABI `0x60`); later versions are not diffed or tracked unless the user asks.
- Host ABI (`AnarchyHost*` signatures from their call sites in AnarchyKenshi.dll): only needed if the route ever changes to reusing AnarchyKenshi's server code.
- Spine: `docs/findings/spine.md` lays out only 5 of 48 topics. Draft layouts for most of the rest are in `tools/work/spine-notes/` (gitignored, this machine only; static, not yet critic-reviewed; `BRIEF.md` there = conventions). Still open, one at a time to spare usage: groups (13; rest of `A-handover2.md`, Opus), base (39; intent and the rest after `B4.md`, Opus), trade remainder (25; `C-handover2.md` item 1, Sonnet). Then: critics, rewrite `spine.md`, knowledge map, commit.
- Server-message field meanings (`u32 head`, Welcome `u16`, Redirect `u32`, snapshot flag bits, Position floats) are open (`docs/findings/protocol.md`).
- `AnarchyLauncher.dll` (launcher, server browser, self-updater) is not analysed.
- Runtime logs in `<Kenshi>\mods\AnarchyKenshi\` (`AnarchyKenshi*.log`, crash dumps; they contain other players' chat) could confirm the live build detection, scan result and installed hook count; the user allows reading them (not committing them), but `AnarchyKenshi-logins.txt` there likely holds credentials (never read or commit it).
