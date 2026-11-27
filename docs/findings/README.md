# Findings index

Mod in the repo: AnarchyKenshi v0.133.1. One file per binary or topic; each note names the version it was made from.

| File | What it covers |
|---|---|
| [protocol.md](protocol.md) | (v0.133.1) AnarchyCore.dll wire protocol: ENet use, all message IDs and layouts, login crypto, machine fingerprint, HTTP browser and report. |
| [AnarchyCore-abi.md](AnarchyCore-abi.md) | (v0.133.1) AnarchyCore.dll exports used by AnarchyKenshi.dll; the AnarchyHost* stubs. |
| [AnarchyKenshi.dll.md](AnarchyKenshi.dll.md) | (v0.133.1) Main mod DLL: startPlugin flow, build detection, AnarchyCore loading and ABI check, server mode (present but disabled by the client-only Core), RCON. |
| [hooks.md](hooks.md) | (v0.133.1) How AnarchyKenshi.dll finds Kenshi code (site tables, live-image scan) and hooks it (own detour engine, gates, patches). |
| [kenshi-sites.tsv](kenshi-sites.tsv) | (v0.133.1) The 795 Kenshi addresses from the site tables: name, GOG/Steam RVA, kind, detoured or not. |
| [spine.md](spine.md) | (v0.133.1) The Spine sync layer: 48 topic registrations (47 classes), blob format, which state goes which way. |
| [spine-topics.md](spine-topics.md) | (v0.133.1) Per-topic Spine layouts: server -> client rows and client -> server intent payloads for all 48 topics, shared key/primitive conventions. |
| [AnarchyLoader.dll.md](AnarchyLoader.dll.md) | (v0.133.1; code byte-identical to v0.103.2) Ogre-plugin bootstrapper: start flow, env vars, log file, how the other DLLs are loaded. |
| [AnarchyLauncher.dll.md](AnarchyLauncher.dll.md) | (v0.133.1) Replacement for Kenshi's start-up launcher (news, mods, settings, SP/MP switch) and the signed self-updater; no server browser. |
| [remote-bodies.md](remote-bodies.md) | Showing another player as a body: spawn, move order, AI suppression, removal, world lifecycle; AK v0.133.1 compared with KenshiLib 0.5.0, KenshiCoop and WorldOfKenshi (plan gaps G3, G4, G5, G7). |
| [ak-kenshilib-names.tsv](ak-kenshilib-names.tsv) | (v0.133.1) AK site-table names matched to KenshiLib 0.5.0 declarations by address offset (622 rows, inferred per row). |
| [kenshi-build.md](kenshi-build.md) | Which Kenshi exe the game runs (RE_Kenshi's 1.0.65), how RE_Kenshi plugins load, KenshiLib and the v100 toolchain. |
