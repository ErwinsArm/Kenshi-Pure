# AnarchyLauncher.dll

- File: `AnarchyLauncher.dll` from mod v0.133.1, SHA-256
  `3322a71888d45ca0e7e6ad8ade211bd64e342bbf7e63a20c74e7f0e687432efa`, 441 856 bytes, PE32+ x64,
  image base `0x180000000`. All addresses are RVAs.
- Built with VC++ 2010 (linker 10.0, imports `MSVCR100.dll`/`MSVCP100.dll`), like the Loader.
- Evidence: Ghidra pseudocode of the whole file, disassembly, strings, import/export/resource
  tables. Static only: the DLL was not run (a run would self-update the mod folder) and no URL in
  it was contacted. *M* = measured, *I* = inferred; Ghidra function names are guesses.

**In one line:** a replacement for Kenshi's own start-up launcher window (news, mods, video
settings, singleplayer/multiplayer switch) plus a signed self-updater. It has **no** server
browser, login or server address; it only decides whether and when AnarchyKenshi.dll starts.

## Tables (*M*)

- Exports: `launcherInstall` (`0x6ec0`); two stray `std::` exports at `0x44eac`.
- Imports: WinHTTP (only network API; no Winsock/WinINet); no OS crypto; USER32/GDI/GDI+ layered
  window, `dwmapi` loaded at runtime (no D3D, no ImGui); COMDLG32 file dialogs; SHELL32
  `ShellExecuteW` (opens URLs); ADVAPI32 registry (`HKCU\Software\Valve\Steam` → `SteamPath`);
  VERSION.dll; KERNEL32 incl. `MoveFileExW`, `DeleteFileW`, `VirtualProtect`,
  `FlushInstructionCache`, `GetPrivateProfileStringA`, `CreateThread`.
- Resources: version info and an `asInvoker` manifest only.

## Start (*M*)

AnarchyLoader loads it and calls `launcherInstall(cb)` (see [AnarchyLoader.dll.md](AnarchyLoader.dll.md));
`cb` is the Loader stub that starts AnarchyKenshi.dll.

1. Stores `cb` (`0x69428`), the module folder (`0x69050`), opens the log.
2. `0xbf30` finds 8 code patterns ("recipes", table `0x4f480`) in `kenshi_x64.exe`:
   ConfigureLauncher, ModScanCall, VideoMapInsert, VideoCurrentAssign, NvidiaProfile,
   ConfigOptionFind, LanguageSet, SteamUgcSlot.
3. `0xab00` takes over Kenshi's launcher window: a 14-byte `jmp [rip]` patch in Kenshi's code
   (`VirtualProtect`) and one pointer swap (`DoModal` slot `0x69aa0` → `0xa8e0`). That the patched
   function is Kenshi's launcher dialog is *I* (from the recipe name).
4. Returns 1, or 0 when the game is not recognised or the patch fails; on 0 the Loader starts
   AnarchyKenshi.dll directly.

## Launcher window (*M*)

- Tabs News / Mods / Settings. Mods: load order, enable/disable, search, Steam Workshop
  subscribe, modpack save/load as HTML ("Kenshi Multiplayer modpack"). Settings: resolution,
  monitor, window mode, vsync, card, language, an NVIDIA profile. Buttons: switch
  Singleplayer/Multiplayer, Patreon link, PLAY / UPDATE.
- Reads `kenshi.cfg` ("Video Mode"), `data/mods.cfg`, `data/__mods.list`, Steam
  `libraryfolders.vdf` and the Workshop folder of app 233860.
- No `AnarchyKenshi.cfg`, host/port/password strings or `/v1/` browser paths are in the file;
  the server browser lives in AnarchyCore ([protocol.md](protocol.md)).
- Env vars: `ANARCHY_UI_TEST`, `ANARCHY_UI_AUTOPLAY`, `ANARCHY_UI_SNAP`, `ANARCHY_UI_INTRO`.
- Log (`0x44090`): the same `AnarchyKenshi[-MODE[-NAME]].log` as the Loader.

## Hand-over to the other DLLs (*M*)

- PLAY (`0x6ab0`): applies the chosen settings (`0x1180`, which files exactly is open), then
  `0x35b0` calls `cb` → AnarchyKenshi.dll starts.
- No window → still calls `cb` (Kenshi's own launcher appears).
- File `<dir>\AnarchyLauncher.singleplayer` present → `cb` is never called ("the multiplayer
  plugin stays out").
- No server data, env vars, shared memory or callbacks go to the other DLLs (no such strings or
  APIs). Outputs are files only: Kenshi config and mod list (*I*), updated DLLs, state file, log.

## Self-updater (*M* unless marked)

Runs in a worker thread.

- Source: `https://github.com/madscism-collab/AnarchyKenshi-Client/releases/latest/download/`
  `manifest.txt` and `manifest2.txt` (built at `0x32280`); fallbacks
  `…/releases/download/v<ver>/<file>` and the GitHub API
  `https://api.github.com/repos/madscism-collab/AnarchyKenshi-Client/releases/` (`tags/v…`,
  `latest`, assets by name). `ANARCHY_UPDATE=off` disables it, any other value replaces the base
  URL; `ANARCHY_UPDATE_API` replaces the API base.
- HTTP client (`0x30b80`): User-Agent `AnarchyKenshi-Launcher`, timeouts 5/8/15 s, TLS 1.2 only,
  plain http only for `127.0.0.1`, certificate checks never disabled; GitHub API headers
  `Accept: application/vnd.github+json`, `X-GitHub-Api-Version: 2022-11-28`.
- Manifest (parser `0x342c0`, ≤ 16 KiB):

  ```
  AnarchyManifest 1|2
  seq <decimal>
  version <digits and dots, ≤32 chars>
  file <name> <size ≤128 MiB> <sha256 hex>
  ...
  sig <128 hex>
  ```

- Signature over every byte before `sig `, checked at `0x3a9c0`: S < L check (constant
  `0x58f20`), SHA-512(R‖A‖M), point decode, compare; public key (32 bytes) at `0x56eb0`. Exactly
  Ed25519 is *I* from this structure (not tested against RFC 8032 vectors). Failure: "Update list
  failed verification and was ignored".
- Allowed file names (`0x308b0`): AnarchyKenshi.dll, AnarchyCore.dll, AnarchyLoader.dll,
  AnarchyLauncher.dll, AnarchyNews.txt, AnarchyKenshi-shapes-base.txt. Others fail a v1 list and
  are skipped in a v2 list (*I*, decompile only).
- Anti-rollback: `seq` must be ≥ the one in `AnarchyLauncher.state` (`seq %I64u`, `version %s`)
  and ≥ the built-in floor `0x6b0671fe` (1795584510; 14 s before the PE timestamp 1795584524, so
  probably the build time, *I*).
- Download (`0x35300`): each file whose size or SHA-256 differs goes to `<name>.new`, is checked
  for size and SHA-256 ("downloaded file does not match the signed list"), then swapped
  (`0x327e0`): `MoveFileExW(REPLACE|WRITE_THROUGH)`, else `name→name.old`, `.new→name` with
  rollback. Marker `AnarchyLauncher.swapping` → "The last update did not finish"; the next start
  deletes `AnarchyLoader.dll.old` and `AnarchyLauncher.dll.old` (`0x32e70`). No forced restart
  ("Restart the game", *I*).
- AnarchyNews.txt is installed at once; the DLLs wait for UPDATE unless auto-install is on.
- Effect: whoever holds the signing key can replace all four DLLs on every player's machine at
  the next start, with one click or none (*I*). A redirected `ANARCHY_UPDATE` still needs that key.

## Open

- What PLAY writes (`0x1180`: kenshi.cfg, mods.cfg).
- Formats of `.state` / `.swapping` (`0x31d40`, `0x31e50`, `0x31f60`).
- The recipe byte patterns and the `jmp` target in Kenshi.
- Workshop subscribe, NVIDIA profile, modpack HTML import parser.
