# Plan: KenshiPure without RE_Kenshi (own loader, own address layer)

Goal: KenshiPure starts on its own, early (before the game window exists), on the Steam exe as Steam
ships it (1.0.68), on RE_Kenshi's 1.0.65 copy and on GOG 1.0.65, with or without RE_Kenshi installed,
as AnarchyKenshi does. Labels: **M** measured, **D** documented, **I** inferred.

## Facts the plan rests on

1. KenshiLib 0.5.0 knows exactly two exes, both 1.0.65 (Steam, GOG), by MD5 (`HashToVersionMap`,
   `Source/kenshi/Kenshi.cpp`, **M**); RVA files only `Steam_1.0.65.br`, `GOG_1.0.65.br` (**M**). No
   KenshiLib release up to v0.5.1 mentions 1.0.68; RE_Kenshi runs 1.0.68 installs by making a
   1.0.65-compatible exe in `<Kenshi>\RE_Kenshi\` (**D**, README and releases).
2. Steam's exe is 1.0.68 (`a596ab4e…`); with RE_Kenshi the game runs its 1.0.65 copy (**M**,
   [kenshi-build.md](findings/kenshi-build.md)). Without RE_Kenshi, KenshiLib cannot start.
3. The `.br` table has 7630 entries: 7622 into `.text`, the last 8 into `.data` (globals such as `ou`)
   (**M**). `InitRVAs` stores `image base + offset` (**M**, `Source/core/Functions.cpp`).
4. `KenshiPure.dll` uses about 25 game functions plus the global `ou` through KenshiLib (**M**, its
   imports); the rest of KenshiLib's table is unused by us.
5. Display cause and timing (**M**, AK's log, [AnarchyKenshi.dll.md](findings/AnarchyKenshi.dll.md) §5):
   Kenshi creates the swap chain windowed, 1920x1080 @ 0/0, `DXGI_MODE_SCALING_STRETCHED`; AK sets
   `UNSPECIFIED` in a `CreateSwapChain` hook and also hooks `ResizeTarget`. AK's hook was in place
   0.16 s before the game's call, so an Ogre plugin line in `Plugins_x64.cfg` is early enough; RE_Kenshi
   post-load plugins (~7.5 s) come after the swap chain (**M**, RE_Kenshi_log.txt).
6. RE_Kenshi restarts only when the running exe is not one it supports ("Version incompatible,
   restarting..." in `RE_Kenshi.dll`, **M** string; the condition is **I**).
7. The user's game folder has `dxgi.dll` = ReShade 5.4.2 (**M**); it wraps the same DXGI calls.
8. KenshiLib, RE_Kenshi and MinHook licences allow reuse in our AGPL-3.0 repo (**D**).

## Design

**A. Loader `KenshiPureLoader.dll`** — Ogre plugin (exports `dllStartPlugin`/`dllStopPlugin`), line
`Plugin=mods/KenshiPure/KenshiPureLoader` appended **after** `Plugin=RE_Kenshi` when that exists (a
process RE_Kenshi restarts exits before reaching us, **I**). Single instance per process; logs pid, exe
path and hash. Stays idle when AnarchyKenshi is loaded. Installs the display hook (B), resolves
addresses (C), then starts `KenshiPure.dll` via an exported `kpStart()`.

**B. Display fix** — hook `IDXGIFactory::CreateSwapChain` (factory from `CreateDXGIFactory1`) and
`IDXGISwapChain::ResizeTarget`: when `kenshi.cfg` says full screen, set `BufferDesc.Scaling =
UNSPECIFIED`; log the request before and after. The minimize-restore in `client/Display.cpp` stays as
the fallback when the client area still differs from the desktop mode. Test with ReShade on.

**C. Own small address layer instead of KenshiLib** — only the functions and globals KenshiPure uses
(about 25 + `ou`, grows with features):
- Per known exe (SHA-256): a committed table `client/rva/<build>.rva` with a header (magic, count,
  hash of the symbol list) and one RVA per symbol, by name.
- 1.0.65 Steam/GOG: taken from KenshiLib's `.br` files by symbol index.
- 1.0.68 Steam: generated offline (`scripts/gen-rva.mjs`) from the two exes on the user's PC; a
  function counts only if its masked body is unique in **both** exes and its shift agrees with its
  neighbours'; globals come from RIP-relative operands of matched code; every entry checked by hand
  in Ghidra/REA before it is committed.
- Calls: a small ml64 file defines each used mangled name as `jmp [table+i*8]` (KenshiLib's own
  mechanism, for our list only), so KenshiLib's headers keep compiling unchanged for types and calls.
- Missing entry → the slot points at a trap stub that logs the symbol and returns; the feature that
  needs it is switched off at start, never the game.
- Hooks: MinHook (upstream, pinned by SHA-256 in `setup-build.ps1`).
- Unknown exe: log which build it is and stay inactive (no run-time pattern scan for now).

**D. KenshiPure.dll changes** — version check reads our resolver, not `KenshiLib::GetKenshiVersion`;
the frame-hook address comes from our table, not `GetProcAddress(KenshiLib.dll, …)`; `startPlugin`
and `RE_Kenshi.json` go (no double start under RE_Kenshi); no `KenshiLib.dll` import (build check).

## Blind spots and how each is closed

1. **Struct layouts and vtable slots 1.0.65 → 1.0.68** for every field and virtual call we use: compare
   the instructions that use each offset/slot in both exes; until done, 1.0.68 runs read-only.
2. **RE_Kenshi on, RE_Kenshi off** — both need a run by the user; the loader's pid log line shows what
   happened in each process.
3. **Two hook engines** (RE_Kenshi's MinHook and ours) on the same function: we hook only the frame
   loop and DXGI; check that RE_Kenshi and the user's workshop plugins do not hook those.
4. **ReShade** wraps DXGI: our hook must sit on the factory the game actually gets; test with it on.
5. **Without RE_Kenshi** its shader cache and fast heightmap are gone (slower loading, **M** log).
6. **GOG 1.0.68**: no exe here; unsupported until someone provides one.
7. **Kenshi updates**: a new exe → "unknown build, inactive" with a clear log line, until a table is added.
8. **Anti-virus**: early in-memory patching can trip heuristics (AK warns about it).

Not doing: building all of KenshiLib (needs its MinHook fork, Boost libs, MSBuild; days of work for
7600 functions we do not use). Not committing byte patterns of Kenshi's exe (only RVAs).

## Steps and checks

1. Resolver + ml64 stub table + MinHook for today's 1.0.65; KenshiPure without KenshiLib, still started
   by RE_Kenshi → log identical to today (proves the layer).
2. `gen-rva.mjs` for 1.0.68 Steam, hand-checked entries.
3. Loader + DXGI hook + installer line in `Plugins_x64.cfg`.
4. User runs: RE_Kenshi on / off, each: log, full screen without Alt+Enter, probe `status`/`characters`.
5. Blind spot 1 before any write command is allowed on 1.0.68.
