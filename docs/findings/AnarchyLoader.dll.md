# AnarchyLoader.dll

- File: `AnarchyLoader.dll`, mod v0.103.2, 38 400 bytes,
  SHA-256 `0d7c25887f11b1c27020a7ef5e39927a6c45bbb50caa8b66eab1d139ce347c69`.
- PE32+ x64 DLL, image base `0x180000000`. All addresses below are RVAs.
- Built against the VC++ 2010 runtime (imports `MSVCR100.dll`, `MSVCP100.dll`), the same runtime
  Kenshi uses (*measured*: import table; Kenshi's runtime *inferred*).
- Version resource: "Kenshi Multiplayer - starts the mod as an Ogre plugin", FileVersion 0.103.2,
  CompanyName "Anarchy" (*measured*: strings).
- No TLS callbacks, no load-config directory (*measured*: REA `inspect_pe_load_config`).

## Role

A small bootstrapper. Kenshi's Ogre loads it from `Plugins_x64.cfg`
(`Plugin=mods/AnarchyKenshi/AnarchyLoader`, written by `anarchy_enable.ps1`). It decides which
other mod DLL to load from its own folder and starts it. It contains no game logic and no network
code (*measured*: imports are only KERNEL32, USER32 `MessageBoxW` and the C/C++ runtime).

## Exports (*measured*: export table)

| RVA | Name | Behaviour |
|---|---|---|
| `0x4390` | `dllStartPlugin` | Ogre plugin entry point; does all the work below. |
| `0x10b0` | `dllStopPlugin` | Empty (`ret`). |

`DllMain` (`0x1090`, *measured*): on `DLL_PROCESS_ATTACH` stores its own `HMODULE` in a global
(`0xa290`) and calls `DisableThreadLibraryCalls`.

## Start flow (`dllStartPlugin`, `0x4390`; *measured* from decompile unless marked)

1. Runs once (guard byte `0xa298`).
2. `ANARCHY_LOADER` env var equal to `off` → logs "loading nothing (control arm)" and returns.
3. Logs "start as an Ogre plugin".
4. Checks whether `AnarchyPreload.dll` exists next to the loader (`GetFileAttributesA`). If yes,
   loads it like a plugin (step 6) and remembers the flag (`0xa299`); if not, logs
   "no AnarchyPreload.dll beside - player package". The shipped package has no
   `AnarchyPreload.dll`, and `anarchy_enable.ps1` deletes one if present, so it is a
   developer/test build component (*inferred*).
5. Chooses whether to go through `AnarchyLauncher.dll`:
   - `ANARCHY_UI` = `ours` → launcher.
   - else Preload present → no launcher.
   - else `ANARCHY_UI` = `vanilla` → no launcher.
   - else `ANARCHY_MODE` = `server` → no launcher.
   - else → launcher (the normal player case).

   All comparisons are exact and case-sensitive (`memcmp` + length check).
6. **Launcher path** (`0x4220`): `LoadLibraryA(<dir>\AnarchyLauncher.dll)`, `GetProcAddress`
   `launcherInstall`, calls it with one argument: a callback at `0x4200`. If the DLL does not load,
   the export is missing, or `launcherInstall` returns 0, it falls back to the direct path.
   The callback is three instructions (*measured*: bytes hand-decoded, Ghidra has no function
   there): `movzx edx, byte [0xa299]` (Preload flag); `lea rcx, "AnarchyKenshi.dll"`;
   `jmp 0x3e20`. So AnarchyLauncher decides **when** AnarchyKenshi.dll starts (*inferred*: e.g.
   after its own window, see below).
7. **Direct path / plugin load** (`0x3e20`, `loadPlugin(name, preloadPresent)`, name *inferred*):
   - path = `<loader dir>\<name>`; reuses an already-loaded module (`GetModuleHandleA`) or
     `LoadLibraryA`s it.
   - Resolves the export `?startPlugin@@YAXXZ` (MSVC mangling of `void startPlugin(void)`) and
     calls it. Missing → logs "no startPlugin - not our plugin".
   - Load failure → logs the Win32 error and shows a `MessageBoxW` (`0x3170`), unless Preload is
     present or `ANARCHY_MODE=server`. Error codes 5, 225, 226, 577, 1260 and 4550–4599 get the
     "Windows blocked … Smart App Control or an antivirus" text, others the "run Enable
     AnarchyKenshi.bat again" text (*measured*). Those codes are access denied, virus
     detected/deleted, invalid image hash, blocked by policy and the code-integrity range
     (*inferred*: standard `winerror.h` values, not re-checked against the header).
   - Kenshi then starts without multiplayer; the loader never aborts the game.

The loader directory comes from `GetModuleFileNameA(own HMODULE)`, cut at the last `\` or `/`;
fallback `.` (`0x3600`, *measured*).

## Environment variables read

| Variable | Effect in the loader |
|---|---|
| `ANARCHY_LOADER` | `off` disables everything. |
| `ANARCHY_UI` | `ours` forces AnarchyLauncher, `vanilla` skips it. |
| `ANARCHY_MODE` | `server` skips AnarchyLauncher and error boxes; also names the log file. |
| `ANARCHY_NAME` | Appended to the log file name. |

Values over 255 bytes are treated as unset (`0x30b0`, *measured*). The names suggest the same
DLLs also run as a dedicated/headless server instance (`ANARCHY_MODE=server`) (*inferred*).

## Log file (`0x3c90`, `0x3860`; *measured*)

`<loader dir>\AnarchyKenshi.log`, or `<loader dir>\AnarchyKenshi-<MODE>[-<NAME>].log` when
`ANARCHY_MODE`/`ANARCHY_NAME` are set. Each message is reopened and appended (`std::ofstream`,
open mode `8` = `ios::app` in MSVC, *inferred* from the constant) as
`HH:MM:SS.mmm [AnarchyLoader] <message>`.

## How the four DLLs relate

- Loader → `AnarchyPreload.dll` (optional), `AnarchyLauncher.dll`, `AnarchyKenshi.dll`, all by
  `LoadLibraryA` at runtime; none in its import table (*measured*).
- `AnarchyLauncher.dll` (SHA-256 `037b7cdb…b7bb`) exports `launcherInstall` and contains the
  strings `AnarchyKenshi.dll`, `AnarchyCore.dll`, `AnarchyLoader.dll` (*measured*: string grep).
  Its imports point at a custom window UI (USER32/GDI/GDI+ drawing, layered window), HTTP
  (WinHTTP), file replace/delete (`MoveFileExW`, `DeleteFileW`), reading an INI-style config
  (`GetPrivateProfileStringA`, likely `AnarchyKenshi.cfg`) and code patching (`VirtualProtect`,
  `FlushInstructionCache`) (*measured*: imports; purposes *inferred*). Likely a pre-game
  launcher / server browser / updater; not analysed yet.
- `AnarchyCore.dll` is not referenced by the loader; its name appears in AnarchyKenshi.dll and
  AnarchyLauncher.dll (*measured*: string grep). Whether it is a static import or loaded at
  runtime is not checked yet.
- `AnarchyKenshi.dll` exports `?startPlugin@@YAXXZ` (*measured*: string present; export table
  not yet read) and also mentions `AnarchyPreload.dll`.

## Open questions

- What `launcherInstall` does with the callback and its return value (AnarchyLauncher.dll).
- What `startPlugin` in AnarchyKenshi.dll sets up first (hooks, network, Ogre/Kenshi objects).
- How AnarchyCore.dll is loaded and what it provides.
