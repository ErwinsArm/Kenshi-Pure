# Related projects and resources

Existing Kenshi multiplayer projects, the KenshiLib / RE_Kenshi plugin stack and other reverse-engineering or modding resources that could save work. Status was read from GitHub pages and the GitHub API; "verified" means seen on the repo page, API or file itself, "claimed" means only stated in a README or listing. Nothing was downloaded, built or run.

Status notes use `pushed_at` from the GitHub API unless stated. Star and commit counts are as seen on the page.

## 1. Plugin stack we build on

### RE_Kenshi
- Link: https://github.com/BFrizzleFoShizzle/RE_Kenshi (releases: https://github.com/BFrizzleFoShizzle/RE_Kenshi/releases, Nexus: https://www.nexusmods.com/kenshi/mods/847)
- Status: latest release 0.3.5, last push 2026-09-18, 173 stars, 15 open issues. Maintained (verified).
- Licence: GPL-3.0 (verified); dependencies carry their own licences.
- Language: C++, builds with the Visual C++ 2010 x64 compiler (README: "MUST be compiled using the Visual Studio 2010 compiler"); source is public, KenshiLib and other code come in as submodules.
- Versions: every release from 0.2.15 to 0.3.5 lists Kenshi 1.0.65 and 1.0.68, Steam and GOG (verified on the releases page). 0.3.0 added the plugin system; 0.3.1 made RE_Kenshi load by default; 0.3.2 fixed plugin dependency loading from plugin directories; 0.3.5 fixed plugin load error reporting.
- Offers us: the loader that starts our plugin DLL, crash handler, version matching for 1.0.65 and 1.0.68.
- Verdict: **use**. It is the host for our client plugin and already installed.

### KenshiLib
- Link: https://github.com/BFrizzleFoShizzle/KenshiLib (current home, branch `RE_Kenshi_mods`, last push 2026-09-05, 37 stars). Predecessor repo: https://github.com/KenshiReclaimer/KenshiLib (last push 2026-04-06, releases stop at v0.2.1).
- Releases: v0.5.1 (2026-09-05) is the newest; v0.5.0 is stated as the version shipped in RE_Kenshi 0.3.5. Release assets are a zip of headers plus `.lib` (verified).
- Licence: GPL-3.0 (verified). README states plugins built with it generally must also be GPL-3.0; it includes third-party parts with other licences.
- Provides (verified from headers and notes): many reconstructed Kenshi class headers (count not checked) under `Include/kenshi/` (e.g. `Character.h`, `Building/`, `gui/`, `Town.h`), Ogre and MyGUI headers, and a small exported API in `Include/core/Functions.h`: `GetRealAddress`, `GetRealFunction`, `AddHook(target, detour, &original)` and `HookStatus`. Releases add classes such as AI task classes, `HavokCharacter`, `NavMesh`, `TaskPathfinder`, a hook queue API (v0.4.1).
- Address handling: public headers have no per-version offset tables or pattern scanner (`Include/core/RVA.h` has the pattern constructor commented out as "TODO FIX COMPILER"). Addresses live inside the compiled `.lib`; how it picks them per game version is not documented on the pages read (not verified). The README does not state 1.0.65 or 1.0.68; support is inferred from RE_Kenshi's own support and from KenshiKin and KenshiCoop using it.
- Build rules (verified from README): VS2010 v100 x64 toolset required, Boost 1.60.0, Release only (Debug broken), the library's own `AddHook` must be used (no other detour library), do not touch MyGUI off the UI thread.
- Docs: only the README plus examples; no API reference.
- Forks (ignore): https://codeberg.org/iam/KenshiLib ("Kenshiken vendor fork", last commit 2026-06-19) and https://github.com/Erano01/KenshiLib (CMake/MinGW fork with a C API layer for Linux; its notes say ABI compatibility with the original binary is still constrained).
- Verdict: **use**. It is the only maintained source of struct layouts and a hook API for both game versions. Our plugin becomes GPL-3.0-bound if it links it.

### KenshiLib_Examples (and KenshiLib_Examples_deps)
- Link: https://github.com/BFrizzleFoShizzle/KenshiLib_Examples/
- Status: 49 commits, requires RE_Kenshi 0.3.1 or later. Licence GPL-3.0.
- Contents (verified): Hello World (debug log), Kill Button (MyGUI window that kills the selected character), Dialogue Extensions, World State Variables, Plugin Import/Export (plugin-to-plugin APIs, preload plugins), Character Highlight. The `_deps` repo bundles precompiled Ogre and Boost libraries and includes for building.
- Verdict: **use** as the build template (project settings, deps, plugin entry point).

### KenshiLua and other Lua layers
- Nexus page: https://www.nexusmods.com/kenshi/mods/2068 (could not be fetched, 403). Search listing claims: a Lua runtime plugin with bindings for about 7,000 KenshiLib functions, needs RE_Kenshi 0.3.3 or later. Repos found by API search: https://github.com/sizzlins/kenshihack (CLI RPC plus embedded Lua 5.4, last push 2026-09-12, no licence) and https://github.com/piepieonline/RE_Kenshi_Extensions (last push 2025-09-21, no licence). Not inspected.
- Verdict: **ignore** for the plugin itself; a Lua/RPC console could help interactive probing later.

## 2. Multiplayer and co-op projects

All were found by GitHub search (query "kenshi multiplayer" and similar). AnarchyKenshi is the closed-source reference we already have; the only related public repo seen is https://github.com/madscism-collab/AnarchyKenshi-Client (signed release files for its launcher, last push 2026-11-26), not inspected.

### KenshiCoop (nhoral)
- Link: https://github.com/nhoral/KenshiCoop
- Status: created 2026-07-08, last push 2026-08-10 (verified), 159 commits, 294 stars, 85 open issues; commits are by one author with Cursor as co-author (AI-assisted, verified in commit data).
- Licence: AGPL-3.0 (verified). README says it links KenshiLib under GPLv3 section 13.
- Approach: RE_Kenshi plugin (`KenshiCoop.dll`), ENet over UDP with an optional Steam P2P tunnel; no dedicated server; the host is authoritative for the world, each client for its own squad; wire-protocol headers are separate plain C++03 in `src/netproto/` (protocol version 55 at last commit). All engine mutation runs on the main thread.
- Synced (claimed in README): squads, NPCs, combat, inventory, trades, ground items, base building, a shared wallet, speed and saves. Two players is the design target. Known issues listed by the README: desyncs and crashes.
- Version: Kenshi 1.0.65 Steam with RE_Kenshi 0.3.1+ (README); nothing about 1.0.68 or GOG.
- Documentation (verified): `docs/API_REFERENCE.md` lists the engine calls used for characters: `Character::getPosition`, `teleport`, `setDestination`, `clearAllAIGoals`, `CharMovement::_setPositionDirectionAndTeleport` / `setDestination` / `halt` / `setDesiredSpeed`, `RootObjectFactory::createRandomCharacter`, `GameWorld` queries, `hand`, plus KenshiLib helpers (`FindPattern`, `RVAPtr`, `GetKenshiVersion`). `docs/REPLICATION_PITFALLS.md` covers dangling `RootObject*` pointers (re-resolve via `hand`), unreliable single reads, main-loop rate of about 100-125 Hz and 4608-unit zone cells. Also a scenario test harness (`scripts/regress.ps1`, `scripts/dev_cycle.ps1`).
- Offers us: the closest working proof that walking, spawning and teleporting characters through KenshiLib on 1.0.65 is possible, with pitfalls written down.
- Verdict: **read as reference**. AGPL means copied code would bind our whole project; read the docs and reimplement.

### WorldOfKenshi (Blast-Pop)
- Link: https://github.com/Blast-Pop/WorldOfKenshi
- Status: created 2026-11-14, last push 2026-11-16 (verified), 195 commits, 0 stars; README says "alpha 0.3.3" and "Made with ... Claude" (AI-written, verified in README).
- Licence: MIT (verified).
- Approach: C++ client DLL loaded as an Ogre plugin (`wok_client.dll`), standalone dedicated server for Windows and Linux (`wok_server`) with authoritative world and server-side characters (position, skills, inventory, appearance), Valve GameNetworkingSockets, CMake and vcpkg. KenshiLib is not mentioned in the README (not verified either way). Each area is simulated by one player's game.
- Version: Kenshi 1.0.68 Steam only. The `docs/re/` notes (verified, small files: `character.md`, `spawning.md`, `orders.md`, `game-world.md`, `inventory.md`, `health.md`, `stats.md`, `mygui.md`, `ogre.md`, `towns-1.0.68.txt`) list struct offsets, vtable slots and RVAs for `kenshi_x64.exe` 1.0.68 (36,718,592 bytes), with hashes. Examples read: `CharacterHuman` position at `+0x48`; spawn = squad creation RVA `0x7f4420` then character creation RVA `0x5836e0` on `RootObjectFactory` (`GameWorld+0x4a0`); walking = add a `Task_Move` through the AI order list (RVA `0x5086d0`), because calling `CharMovement::setDestination` directly gets overridden by the AI; `GameWorld::destroy` at `0x799af0`. The notes say spawning a default character is verified in game, the remote-player avatar flow is still a plan, and RVAs are unverified on other builds. All of this is claimed by the notes; it was not re-checked against a binary.
- Offers us: the only public project with the same architecture as our plan (dedicated server, own protocol, remote avatar); a concrete recipe for the walk task path; MIT licence allows reuse.
- Verdict: **read as reference**. Offsets are 1.0.68, not 1.0.65; every address must be re-derived on our binary. The repo is two weeks old and AI-written, so treat the notes as hypotheses.

### KenshiMP (xemou) and KenshiKin (Fisl-Gaming)
- Links: https://github.com/xemou/KenshiMP (last push 2026-10-15, 36 commits, 3 stars, GPL-3.0) and https://github.com/Fisl-Gaming/KenshiKin (fork, last push 2026-11-19, 28 commits, GPL-3.0).
- Approach (README): host-authoritative co-op over Steam P2P or TCP; no dedicated server. KenshiMP has a `core/` part (networking, protocol, sessions, movement interpolation, engine-independent) and a `plugin/` part built against KenshiLib 0.5.1. KenshiKin syncs other players' movement, combat, equipment, NPCs, time, doors and more; tested by three players on 1.0.68; its README says it is written by Claude via Claude Code and credits KenshiCoop, Shared-Wastelands and LRaverdy/KenshiCoop as approaches re-implemented without copying code.
- Version: KenshiKin states Steam 1.0.68, or GOG/1.0.65 with RE_Kenshi; KenshiMP states none.
- Verdict: **read as reference** for the `core`/`plugin` split and the movement interpolation; ignore the rest.

### Kenshi-Online (The404Studios) and forks
- Link: https://github.com/The404Studios/Kenshi-Online (created 2024-11-08, last push 2026-06-04, 192 stars, 5 commits, MIT). Forks: https://github.com/NerChyS/Kenshi_mp (3 commits, MIT, Steam 1.0.68), https://github.com/im-blatnoyua/kenshi-online-simplified, https://github.com/vinicius3232/Kenshi-Online-Launcher.
- Approach (README): injector exe (edits `Plugins_x64.cfg`), `KenshiMP.Core.dll` client with 14 hook modules, `KenshiMP.Server.exe` dedicated server, ENet; 16 players claimed. Hosting vendor guides (citadelservers.com) describe the same mod (vendor pages, not verified).
- Quality: footer says built by Claude AI; 5 commits with messages like "Autonomous loop complete: All multiplayer fixes verified"; the README lists known bugs (combat damage sync crashes, AI not synced, reconcile stubbed) next to feature claims, and the "20+ verified offsets" and test claims could not be checked. The NerChyS fork says the original crashed at game load and when a second player joined, and uses byte-pattern signatures plus MinHook with hardcoded struct offsets.
- Verdict: **ignore** the original (claims exceed visible evidence). NerChyS's pattern signatures could be read as a hint list, but they are 1.0.68-only and unverified.

### Other repos found, not worth deeper work
- https://github.com/codiren/KServerMod: README says "not in a playable state"; 13 commits, 26 stars, no licence shown. **Ignore.**
- https://github.com/Melaus123/Shared-Wastelands: MIT, 22 commits, last push 2026-11-23; README content not visible in the fetch, so approach unknown. **Ignore until checked.**
- https://github.com/LRaverdy/KenshiCoop: host-runs-the-world co-op for 1.0.68, last push 2026-11-23, no licence (all rights reserved by default). **Ignore.**
- https://github.com/zeroit789/kenshi-coop (MIT, 1.0.68), https://github.com/EnjouZeratul/KenshiMultiplayer, https://github.com/vinicius3232/KenshiMP (research only, no code language), https://github.com/IamNotDaniil/KenshiLocalMultiplayerMod, https://github.com/KavaMan1337/KenshiNetworkLauncher (Radmin VPN launcher): descriptions only, not inspected. **Ignore.**
- Steam forum threads claiming "no multiplayer possible" are from 2013 and are outdated.

## 3. Other reverse-engineering and modding resources

### Kenshi wiki and FCS manual
- Links: https://kenshi.wiki/w/Imports:Introduction_to_Modding, https://kenshi.wiki/w/Imports:Using_the_FCS, https://www.kenshi.wiki/w/Forgotten_Construction_Set
- Content: import of the official "Using the FCS" PDF (game version 1.0.57) and modding intro; how `.mod` and `.base` files and mod folders work. Documented, not about code.
- Verdict: **read as reference** for game data concepts (templates, factions, squads) that character spawning needs.

### OpenConstructionSet (lmaydev)
- Link: https://github.com/lmaydev/OpenConstructionSet. MIT, C#, 26 stars, last push 2025-02-11 (stale).
- Offers: a managed reader/writer for the game's folders and `.mod` data files, similar to the FCS.
- Verdict: **read as reference** for the `.mod` format if the server needs game data; low value for the first goal.

### MeitouClient (VlcekM) and OpenKenshi (engival)
- Links: https://github.com/VlcekM/MeitouClient (GPL-3.0-or-later, C# on .NET, custom Vulkan renderer, 7,819 commits, 158 stars, last push 2026-11-26) and https://github.com/engival/OpenKenshi (MIT, C++20, 43 commits, "early preview", last push 2026-11-26).
- Both are engine reimplementations that run original game content; MeitouClient also ships a C# modding API. Neither mentions multiplayer. OpenKenshi's credits list KenshiLib/RE_Kenshi, Ogre format docs and the Kenshi wiki as its sources; no Ghidra or IDA database is published in either.
- Verdict: **ignore** for now. They may help later for data formats (mesh, skeleton, save files), not for hooking the real game.

### Kenshi Virtual Simulation Engine (Problemchild1500)
- Link: https://problemchild1500.itch.io/kenshi-virtual-simulation-engine (devlogs on itch.io). An external simulation engine for a persistent world; devlog says player coordinate pointer was still being narrowed down; no offsets published.
- Verdict: **ignore**.

### Ghidra or IDA databases, struct dumps
- Searched for public Ghidra/IDA databases, Cheat Engine tables or struct documents for 1.0.65: none found. The public layout knowledge is KenshiLib (headers), the WorldOfKenshi `docs/re/` notes (1.0.68) and KenshiCoop's API reference. Absence in search results does not prove none exist (Discord-only material is invisible to this research).

## Recommendation

- Use RE_Kenshi 0.3.5 plus KenshiLib v0.5.x as the client base and `KenshiLib_Examples` as the project template; accept that the plugin must be GPL-3.0 compatible and built with the VS2010 v100 toolset.
- Read KenshiCoop's `docs/API_REFERENCE.md` and `docs/REPLICATION_PITFALLS.md` before writing the walk/spawn code (it targets 1.0.65 like us); do not copy code (AGPL-3.0).
- Read WorldOfKenshi's `docs/re/` notes (spawn, move order through the AI task list, `hand` identity) as hypotheses for our dedicated-server design; re-derive each RVA and offset on 1.0.65, since they are for 1.0.68 only. MIT allows reuse after that check.
- Treat Kenshi-Online and its forks as low trust; skip the other small repos.
- Check AnarchyKenshi's own use of the same engine calls (character create, move order) against these notes; agreement across independent sources is the strongest evidence we can get without running anything.
