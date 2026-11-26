# WORK

Goal: an own Kenshi multiplayer (own server + client mod) that works better than AnarchyKenshi and is fully under the user's control; everything is extracted from the client DLLs first (the original server is not available).

- Analyse `AnarchyKenshi.dll` v0.133.1 (network use via AnarchyCore, message catalogue, Kenshi hooks, startPlugin) and write `docs/findings/AnarchyKenshi.dll.md` (+ `protocol.md`, `hooks.md` if warranted).
- Analyse `AnarchyCore.dll` v0.133.1 (UDP transport, login handshake and password hashing, message IDs/layouts, `/v1/servers` browser) into `docs/findings/protocol.md`.
- Re-check `docs/findings/AnarchyLoader.dll.md` against the v0.133.1 loader (SHA-256 `a05816a8…94ab5f`, was written from v0.103.2).
- Open question: the user's runtime logs in `<Kenshi>\mods\AnarchyKenshi\` (`AnarchyKenshi*.log`, ~200 MB, with `net:` lines) could confirm the protocol; reading them needs the user's OK, and `AnarchyKenshi-logins.txt` there likely holds credentials (never read or commit it).
