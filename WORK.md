# WORK

Goal: an own Kenshi multiplayer (own server + client mod) that works better than AnarchyKenshi and is fully under the user's control, without depending on another mod (RE_Kenshi); everything is extracted from the client DLLs first (the original server is not available).

- Route: own server and own client mod, AnarchyKenshi only a reference; plan in `docs/plan-first-playable.md`. M0 done (plugin loads via RE_Kenshi/KenshiLib, frame hook and game calls work in game). Next: M1 (read own position), after the G2 calling-convention sheet, now for the 1.0.65 exe through KenshiLib headers.
- Live probe, second half untested in game: `health`, `inventory`, `find_items` and the write commands `heal`, `teleport`, `spawn_item` (`docs/client.md`). The user's Kenshi still has KenshiPure installed with `-ProbeWrite` and AnarchyKenshi off; after the test run `scripts/install-client.ps1 -GameDir "<Kenshi>" -Uninstall`. Open questions: `fleshStun` meaning (pick `heal` stun mode from healthy NPCs' values), `money` per faction or person, `knockoutTimer` stayed 0.
- M0 run: one first-chance access violation in `RE_Kenshi.dll+0x381e9` at game exit (likely handled); check whether the game exits cleanly with KenshiPure.
- Reference version is fixed at v0.133.1 (the repo copy). The mod updates almost daily (the Steam install was reportedly v0.136.1, protocol `0x88`, ABI `0x60`); later versions are not diffed or tracked unless the user asks.
- Host ABI (`AnarchyHost*` signatures from their call sites in AnarchyKenshi.dll): only needed if the route ever changes to reusing AnarchyKenshi's server code.
- Spine: layouts of all 48 topics are in `docs/findings/spine-topics.md` (open items per topic under "Open"). Before an own server relies on the digest: decode the exact row-hash combine `0x53d380` (a wrong one makes every client resync forever).
- Spine drafts in `tools/work/spine-notes/` (gitignored) can be deleted once nobody needs them; the condensed note replaces them.
- Runtime logs in `<Kenshi>\mods\AnarchyKenshi\` (`AnarchyKenshi*.log`, crash dumps; they contain other players' chat) could confirm the live build detection, scan result and installed hook count; the user allows reading them (not committing them), but `AnarchyKenshi-logins.txt` there likely holds credentials (never read or commit it).
- Own loader (no RE_Kenshi, runs on 1.0.68 too, early DXGI display fix): plan reviewed by both critics in `docs/plan-own-loader.md`; not started. The interim display fix (minimize-restore, `client/Display.cpp`) is built but not installed or run in game.
