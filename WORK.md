# WORK

Goal: an own Kenshi multiplayer (own server + client mod) that works better than AnarchyKenshi and is fully under the user's control; everything is extracted from the client DLLs first (the original server is not available).

- Route: write own server and own client mod fresh, using AnarchyKenshi only as a reference (the goal is full control; reusing its DLLs would tie the project to closed, self-updating third-party code). First playable step is planned in `docs/plan-first-playable.md` (critic-reviewed); plan questions answered (own protocol, Steam now and GOG later, Kenshi 1.0.65 in the menu, relay server on the user's PC, RE_Kenshi allowed if it helps, MIT, the user tests, VS Build Tools ok), a friend tests as second player. Next: M0.
- Reference version is fixed at v0.133.1 (the repo copy). The mod updates almost daily (the Steam install was reportedly v0.136.1, protocol `0x88`, ABI `0x60`); later versions are not diffed or tracked unless the user asks.
- Host ABI (`AnarchyHost*` signatures from their call sites in AnarchyKenshi.dll): only needed if the route ever changes to reusing AnarchyKenshi's server code.
- Spine: layouts of all 48 topics are in `docs/findings/spine-topics.md` (open items per topic under "Open"). Before an own server relies on the digest: decode the exact row-hash combine `0x53d380` (a wrong one makes every client resync forever).
- Spine drafts in `tools/work/spine-notes/` (gitignored) can be deleted once nobody needs them; the condensed note replaces them.
- Runtime logs in `<Kenshi>\mods\AnarchyKenshi\` (`AnarchyKenshi*.log`, crash dumps; they contain other players' chat) could confirm the live build detection, scan result and installed hook count; the user allows reading them (not committing them), but `AnarchyKenshi-logins.txt` there likely holds credentials (never read or commit it).
- Knowledge map: `docs/knowledge-map.html` is updated (Spine, launcher, protocol tiles), but the published artifact belongs to another Claude account; republish it from that account (Artifact tool, url in CLAUDE.md).
