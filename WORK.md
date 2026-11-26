# WORK

Goal: an own Kenshi multiplayer (own server + client mod) that works better than AnarchyKenshi and is fully under the user's control; everything is extracted from the client DLLs first (the original server is not available).

- Decide the route: reuse AnarchyKenshi.dll's built-in server logic behind an own server-capable AnarchyCore.dll, or write server and client fresh from the notes (decides all further work).
- The installed mod in the Steam Kenshi folder is reportedly v0.136.1 (protocol `0x88`, ABI `0x60`, new export `AnarchyHostOfflineAccount`; seen by a critic, not yet re-measured); diff it against the repo's v0.133.1 before building against either.
- Host ABI: derive the signatures of the 191 `AnarchyHost*` imports from their call sites in AnarchyKenshi.dll, plus the minimum set a server must provide (needed for the own-Core route).
- Spine: only 5 of 48 topic layouts are decoded (`docs/findings/spine.md`); the large ones (Squads, Limbs, Machine, Stores, Groups) are open.
- Server-message field meanings (`u32 head`, Welcome `u16`, Redirect `u32`, snapshot flag bits, Position floats) are open (`docs/findings/protocol.md`).
- `AnarchyLauncher.dll` (launcher, server browser, self-updater) is not analysed.
- Runtime logs in `<Kenshi>\mods\AnarchyKenshi\` (`AnarchyKenshi*.log`, crash dumps; they contain other players' chat) could confirm the live build detection, scan result and installed hook count; using them needs the user's OK, and `AnarchyKenshi-logins.txt` there likely holds credentials (never read or commit it).
- Pushing the findings (incl. `docs/findings/kenshi-sites.tsv`, 795 named Kenshi addresses) to the public GitHub repo is the user's call.
