# WORK

Goal: an own Kenshi multiplayer (own server + client mod) that works better than AnarchyKenshi and is fully under the user's control, without depending on another mod (RE_Kenshi); everything is extracted from the client DLLs first (the original server is not available).

- Route: own server and own client mod, AnarchyKenshi only a reference; plan in `docs/plan-first-playable.md`. M0 done (plugin loads via RE_Kenshi/KenshiLib, frame hook and game calls work in game). Next: M0b (own loader), then M1 (read own position).
- In-game cheat menu for the user (their testing tool while we build, e.g. "take damage, then try X"): heal, damage, knock out, spawn item, teleport, as a window or hotkeys in game. Build it on the new foundation (M0b), not on KenshiLib. Known probe bugs to avoid there (`docs/client.md`): weapon spawn fails, quantity > 1 also drops extra single items, teleport is lost while paused. The user's Kenshi still has KenshiPure installed with `-ProbeWrite`.
- M0 run: one first-chance access violation in `RE_Kenshi.dll+0x381e9` at game exit (likely handled); check whether the game exits cleanly with KenshiPure.
- Reference version is fixed at v0.133.1 (the repo copy). The mod updates almost daily (the Steam install was reportedly v0.136.1, protocol `0x88`, ABI `0x60`); later versions are not diffed or tracked unless the user asks.
- Host ABI (`AnarchyHost*` signatures from their call sites in AnarchyKenshi.dll): only needed if the route ever changes to reusing AnarchyKenshi's server code.
- Spine: layouts of all 48 topics are in `docs/findings/spine-topics.md` (open items per topic under "Open"). Before an own server relies on the digest: decode the exact row-hash combine `0x53d380` (a wrong one makes every client resync forever).
- Spine drafts in `tools/work/spine-notes/` (gitignored) can be deleted once nobody needs them; the condensed note replaces them.
- Runtime logs in `<Kenshi>\mods\AnarchyKenshi\` (`AnarchyKenshi*.log`, crash dumps; they contain other players' chat) could confirm the live build detection, scan result and installed hook count; the user allows reading them (not committing them), but `AnarchyKenshi-logins.txt` there likely holds credentials (never read or commit it).
- Own loader (M0b, no RE_Kenshi, AK-style address layer, early DXGI fix): plan `docs/plan-own-loader.md`, critic-reviewed twice, user approved including committing our own byte patterns of Kenshi's exe. Not started. Interim display fix (`client/Display.cpp`) built, not installed or run.
