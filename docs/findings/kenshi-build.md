# Which Kenshi build runs, and how our plugin gets into it

Steam install with RE_Kenshi 0.3.5 (KenshiLib 0.5.0) and AnarchyKenshi v0.136.1, read on the
user's PC. Labels: **M** measured, **D** documented, **I** inferred.

## Two exes, the game runs the 1.0.65 one

| File (game folder) | Size | SHA-256 | Version strings |
|---|---|---|---|
| `kenshi_x64.exe` (Steam's) | 36,718,592 | `a596ab4e407c67b58599c54ffb32dc1bf2b64510cdebd3fa9359ef05a576aeb1` | only "1.0.68 - x64 (Newland) (Steam)" |
| `RE_Kenshi\Kenshi_x64.exe` (RE_Kenshi's copy) | 36,713,984 | `504b362cde850d56afb1cea6f5b7b0ee014d9dd7b47e188599d91c804502cd3e` | only "1.0.65 - x64 (Newland) (Steam)" |

Sizes, hashes and strings **M** (read-only, `sha256sum`, string search). Steam has the "Default
Public Version" branch selected, no beta (**M**, the user's Steam page), so Steam's exe is 1.0.68. The
running game is the 1.0.65 copy:
- `kenshi_info.log`: "Version: 1.0.65 - x64 (Newland) (Steam)"; `RE_Kenshi_log.txt`: "--norestart",
  "KenshiLib 0.5.0", "Kenshi version: 1.0.65", "Kenshi platform: Steam", "Version supported" (**M**).
- AnarchyKenshi.log of the same run: "build: unknown", then its pattern scan finds 798/799 sites
  (**M**): its fixed 1.0.68 Steam / GOG canaries do not match the running exe.
- How the switch happens (RE_Kenshi starting its own copy, `--norestart` on the restarted process)
  is **I**; our plugin logs the path and hash of the running exe on every start, which settles it.

Consequence: the Steam RVAs in [kenshi-sites.tsv](kenshi-sites.tsv) and [hooks.md](hooks.md) are
for the 1.0.68 exe (`a596…`) and do **not** apply to the game as the user runs it. Our plugin finds
Kenshi code through KenshiLib (below), which maps addresses for 1.0.65 and 1.0.68, Steam and GOG.

## RE_Kenshi plugins

- An RE_Kenshi plugin is a mod folder `mods/<Name>/` with `<Name>.mod`, `RE_Kenshi.json`
  (`{"Plugins": ["X.dll"]}`) and the DLL; the mod must be enabled in the game's mod list
  (`data/mods.cfg`, one `<Name>.mod` per line, CRLF) (**D**, KenshiLib_Examples HelloWorld; **M**,
  the user's mods.cfg and three workshop plugins).
- RE_Kenshi calls the export `?startPlugin@@YAXXZ` (`void startPlugin()`, C++ name) (**M**: string
  in `RE_Kenshi.dll`, by the critic; **D**: KenshiCoop `Plugin.cpp`), after mods are loaded,
  about 14 s after start ("Loading post-load plugins…", **M**, RE_Kenshi_log.txt).
- RE_Kenshi itself is an Ogre plugin line `Plugin=RE_Kenshi` in `Plugins_x64.cfg` (**M**). AK's own
  backup `Plugins_x64.cfg.before-anarchy` lacks that line, so restoring it would remove RE_Kenshi (**M**).
- AK is an Ogre plugin line `Plugin=mods/AnarchyKenshi/AnarchyLoader`; its folder has no
  `RE_Kenshi.json` (**M**). Removing that line keeps all AK code out of the game; its
  `AnarchyKenshi.mod` (46 bytes) can stay in the mod list (**I**).

## KenshiLib and the toolchain

- Installed `KenshiLib.dll` = the one in the `KenshiLib_v0.5.0.zip` release
  (`5b6240622d06801dc39896e385efb834b8e1fe2fcdf86067d071ae7a04f6c83b`, **M**). The release zip holds
  only DLL, `.lib`, `.pdb`, RVA files and licences; the headers come from the git tag `v0.5.0`
  (commit `11cd65d5…`, **M**).
- KenshiLib must be used with the VC++ 2010 (v100) x64 compiler (**D**, README). The headers need
  Boost 1.60 (`GameDataManager.h` includes `boost/unordered_map.hpp`, **M** by compile) and `UNICODE`
  (`PerfTimer.h` passes `L""` to `CreateMutex`, **M**).
- The `// RVA = …` comments in KenshiLib headers are not the 1.0.65 addresses (e.g.
  `GameWorld::getFrameSpeedMultiplier` comment `0x66BFE0` is padding in the 1.0.65 exe, by the
  critic, **M**); calls go through KenshiLib's exports, which map the address at run time (**D**).
- Our setup: `scripts/setup-build.ps1` unpacks the compiler from Microsoft's Windows SDK 7.1 ISO with
  `msiexec /a` (files only). Result: `cl.exe` 16.00.30319.01 (VS2010 RTM), and a plugin that imports
  `MSVCR100.dll` and `KenshiLib.dll` and exports `?startPlugin@@YAXXZ` (**M**, `dumpbin`).
- `KenshiLib::GetRealAddress(&Class::method)` only works with whole-program optimisation; without
  it the compiler passes a jump stub in the plugin's own DLL and KenshiLib stops the game with an
  assertion box (**M**, first M0 run). Our plugin takes the address from KenshiLib.dll's export
  instead (`GetProcAddress(KenshiLib.dll, "<mangled name>")`), which does not depend on that option.
- Not used: the VS2010 SP1 compiler update; KenshiLib.lib is an import library (**I**: version-independent).
