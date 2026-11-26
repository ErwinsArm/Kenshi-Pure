# AnarchyKenshi.dll: how it finds and hooks Kenshi code

- File: `AnarchyKenshi.dll` from mod v0.133.1, SHA-256
  `c3d2419d1c02b516552032e9037d29ec1a1b88661199ddac7775f601d97b51bb`, PE32+ x64, image base
  `0x180000000`. All addresses are RVAs in this file unless they are marked as Kenshi RVAs.
- Kenshi reference: Kenshi 1.0.68 (the version AnarchyKenshi.dll names, string `0x12b9d18`), the Steam `kenshi_x64.exe`, SHA-256
  `a596ab4e407c67b58599c54ffb32dc1bf2b64510cdebd3fa9359ef05a576aeb1` (read statically).
- Evidence: Ghidra 12.2 decompile of the whole DLL, `llvm-objdump` disassembly, strings, `.pdata`
  function bounds. Nothing was run. Labels: **M** = measured (decompile, disassembly, strings,
  xrefs, bytes), **I** = inferred. Ghidra `FUN_…` names and every function name given here are
  guesses. In the decompile, `HMODULE + N` is scaled by 4 (`sizeof(HMODULE__)`), so the byte offset
  is `4*N`.
- The decoded site tables are in [kenshi-sites.tsv](kenshi-sites.tsv): 373 table-A and 422
  table-B rows with GOG and Steam RVAs, kind, and whether the row is detoured. Every row's byte
  pattern matches the Steam exe at the Steam RVA (**M**, checked by script); GOG RVAs are unchecked
  (no GOG exe here).

## 1. How the two tables are read

Pipeline at startup (M, `startPlugin` RVA 0x1b20): build log -> RunScan (0xcd9370) -> BindGlobals (0xcd8a90) -> ~40 subsystem
init functions, each of which resolves addresses and installs hooks.

| Piece | RVA | What it does (M unless marked) |
|---|---|---|
| Build check | 0xcd4000 (cached, calls 0xcd3e00) | Compares 2 canary byte strings, both rows of table B: Havok_MemoryRouterDtor (B4, 33 bytes, mask at 0xee4a28) and CharacterStatsWindow_buildSkillRows (B54, 28 bytes, mask at 0xee54d0), at the GOG address (0xba9b90 / 0x8b9630) and at the Steam address (0xbaa450 / 0x8b9ef0). Only GOG matches -> 1, only Steam -> 2, else 0 and a log line "build: canary bytes match neither column ... addresses come from the live-image scan, GOG-only cross-checks are skipped". On the Steam exe file: both Steam canaries match, both GOG ones do not, so the check would return 2 (*measured* on the file; the running image is not measured). |
| Column choice | 0xcd4130 and inline copies | `build==1 ? gogRva : build==2 ? steamRva : 0`. 0 means "no static address". |
| RunScan | 0xcd9370 (once, flag 0x195a892) | (a) For all 373 A rows and 422 B rows it builds a runtime row (0x50 bytes at RVA 0x18d8f70, max 0x800) via 0xcd8d20 and inserts it in a hash table (RVA 0x18d4f70 buckets, 0x18d2f70 chain; key = the first 4 bytes of the longest unmasked run of the pattern). Table A is read at 0xec4570, table B at 0xece680 (copy 1). (b) Sweeps the whole `.text` of the running game image once (0xcd8e50): for each byte position, hash 4 bytes, compare candidates with the masked pattern. (c) Takes the median of (found - gog address) over unique hits (0xcd8f80) and drops hits more than 0x8000 off that shift ("dropped/off-block"). (d) Logs `scan: %u/%u recipes found in the live image in %u ms (missing, ambiguous, DISAGREE, dropped, off-block, globals)` and up to 5 lines `scan: <name> found at 0x.., the table says 0x..`. (e) 0xcd9170: for rows still unresolved and flagged, looks for E8/E9 call chains to find them (inferred purpose). |
| Accessors | 0xcd9930 (A, idx, gog, steam) and 0xcd9b10 (B, idx, gog, steam) | Every use site passes its own copy of the static pair as constants. Result = scanned address if the row has exactly one hit (state==1) and (static arg and table row agree, else a one-time log "scan: recipe index %u does not match its table row" and the static arg wins); for B rows without a direct hit but with insnEnd!=0 the target is decoded from the found site (0xcd9ad0: found + insnEnd + disp32 read at found + dispOff). Otherwise the static column for the detected build; unknown build and no hit -> 0, and the caller skips its feature. |
| BindGlobals | 0xcd8a90, reads table-B copy 2 (0xedc580) | For each of 422 rows calls the accessor; for the 5 `Global_*` rows (gameWorld, gameGui, gameInput, gameSettings, renderMaster) stores base + RVA into 0x195a898/918/908/910/920. If the row has no scan hit it checks the masked pattern at base + anchor of the build's column (0xcd3fc0) and refuses to bind on mismatch. Log: `globals: bound %u/5 (...)`. |
| Address report | 0xc95600, reads table-B copy 3 (0xf01130) | Diagnostic only: for every row checks the pattern at base + target, counts ok / "hooked" (first byte 0xE9, or FF 25, or 48 B8 = already jumped away) / MISMATCH / no-column; log `addresses: ok=%u hooked=%u MISMATCH=%u no-column=%u`. |

Row layouts (*measured*: read by RunScan/accessors, and every row's pattern checked against the
Steam exe by script; a mask byte of 1 means compare, 0 means any byte):

```
Table A row, 48 bytes (0xec4570, 373 rows)
  +0x00 char* name      +0x08 u64 gogRva      +0x10 u64 steamRva
  +0x18 u8*  pattern    +0x20 u8* mask        +0x28 u32 patLen    +0x2c i32 o (<= 0)
  site = pattern match + o   (the pattern starts -o bytes after the site)

Table B row, 80 bytes (0xece680; copies at 0xedc580 and 0xf01130, 422 rows)
  +0x00 char* name      +0x08 u64 gogRva      +0x10 u64 steamRva
  +0x18 u64 gogAnchor   +0x20 u64 steamAnchor
  +0x28 i32 siteOff     +0x2c u32 dispOff     +0x30 u32 insnEnd   +0x34 u32 (0)
  +0x38 u8* pattern     +0x40 u8* mask        +0x48 u32 patLen    +0x4c i32 (never read)
  anchor == target: site = pattern match + siteOff (e.g. WitnessCrime_attackSite: 54)
  anchor != target: pattern at the anchor; target = anchor + insnEnd + disp32 at anchor + dispOff
                    (34 rows checked); the 5 Global_* rows have dispOff = insnEnd = 0
```

- Every pattern occurs exactly once in the `.text` of the Steam exe file (*measured*: 795/795,
  masked search by script). The build canaries (rows B4, B54) match the Steam column of the file.
  This is the file on disk; what the scan finds in the running, possibly already patched image
  is not measured.
- Does it verify the pattern at the expected address? Yes, two ways: (1) the live-image scan (finds the pattern anywhere in `.text`, no fixed address
  needed) and (2) for globals, a direct masked compare at the table's address. Mismatch handling: the scanned address wins over the table when the hit is unique and within 0x8000 of
  the median shift; ambiguous (>=2 hits) or missing rows fall back to the static column; disagreement is logged (cap 5/8 lines). Nothing aborts the plugin; a hook whose address is 0 is simply not installed.
- 5 Global_* rows: dispOff = insnEnd = 0 and anchor != target, so the scan cannot decode them; the target is the static data RVA (e.g. 0x2133040 GOG / 0x2134110 Steam for gameWorld) and the code anchor (a function in .text) is only the check that the column is right.
- Other `siteOff` rows (LabelSite_*, WitnessCrime_attackSite, 4+1 rows): direct rows (anchor == target), found by the scan like function rows.
  They are patch/stop sites (see section 3), not call targets: LabelSite_* are the UI sites of Character::isCampaignLeader / isDiplomat ("labels:" strings, 0xb2ae0...), I.
- Table A has one copy (only RunScan 0xcd9418 and accessor 0xcd99ac read it); table B has three byte-identical copies referenced by three different code units: copy 1 (0xece680) by RunScan and accessor, copy 2 (0xedc580) by BindGlobals, copy 3 (0xf01130) by the address report. Same header compiled into three translation units (I).
- Table A rows are the "api" side: ~1500 tiny wrappers `FUN_180cd7000...` call `FUN_180cd9930(idx, gog, steam)` then `GetModuleHandleA(0)+result` as a function pointer (typical: 0xcd7000, 0xcd7070); null result -> the wrapper returns without calling. So an A row is first of all a callee. 90 A rows are also detoured (below).

## 2. Hooking mechanism (own engine, not MinHook)

No MH_ strings. Three installers, one shared table (max 0x400 entries, RVA 0x1956890 targets, 0x1958890 detours, critical section 0x18d2ee8):

| Function | RVA | Mechanism (M) |
|---|---|---|
| InstallDetour | 0xcd35d0 (wrapper with counters/log 0xcd9cd0; only caller of 0xcd35d0) | Own x64 length decoder (0xcd31c0) over the first >= 5 bytes of the target (max 8 instructions; handles rel8/rel32 jmp/jcc/call and RIP-relative operands). Stub page: VirtualAlloc 0x1000, RWX, within +-0x70000000 of the target (0xcd33f0, max 16 blocks). In the stub: `FF 25 00000000 <detour 64-bit>` (14 bytes), then the relocated prologue, then `E9` back to target+n = the trampoline. Target gets `E9 rel32` to the stub, rest NOPped (0x90). VirtualProtect(0x40) + FlushInstructionCache, bytes journaled by 0x7f2a50 / 0x7f2b80 (before/after bytes in a locked list, I). Output: trampoline address written to the caller's slot (`&DAT_18194....`). Same target twice with other detour = chained (counter), same pair = error 5; error text table at 0xcd3550 ("no page within reach of a 5-byte jump", "prologue has an instruction the decoder does not know", ...). Log: `hooks: own detour refused %p (%s) - this hook is NOT installed; there is no fallback path left`; stats line `hooks: own=%ld failed=%ld chained=%u (trampoline blocks %u)` (0xcd9e90). |
| InstallGate | 0xcd3a70 (wrapper 0xcd9db0, helper 0xba1430) | Patches an exact 8-14 byte site `48 83 B8 <disp32> 00` + `74/75 rel8` or `0F 84/85 rel32` (= `cmp qword [rax+X],0; jz/jnz`), replacing it with `E8 rel32` to a 0x2b-byte stub, `test al,al` and the jcc: the stub does the original cmp and, when the pointer is null, calls the detour(rax, rsi, rbp) and returns its bool. Same table. This is a call-site gate, not a function hook. |
| Raw patchers | 0xba23b0 (generic bytes, RWX), 0x18001fd00, 0x1ea0e0, 0x1ea750, 0x1eb3a0, 0x246e0 | See section 3. |

Who calls the installer: 243 call sites of 0xcd9cd0 (direct ones in each feature's init function, plus ~110 through 11 small wrappers such as 0x18001ea90, 0x1800207e0, 0x18072cd70, 0x180919fb0 that take (name string, target RVA, detour, &trampoline)). The target almost always comes from `FUN_180cd9930/9b10(idx, gog, steam)` + module base; some come from GetProcAddress (exports).
Rule (M): a row is detoured when its accessor result reaches 0xcd9cd0 (directly or via a wrapper). Resolved this way with a script over the full decompile (every resolved site had inline gog/steam constants equal to the table row: 0 mismatches over 342 sites):
- 90 distinct table-A rows are detoured; 235 distinct table-B rows are detoured; plus 14 B rows via InstallGate; the 8 `AiTick_*` B rows (idx 88-95) go through 0x18003c000, which additionally compares an 8-byte prologue from a table at 0x115c0a8 and logs "REFUSING to hook it" on mismatch (so they are detoured, count them as installed-with-check; I, from the strings and the loop at 0x18003c230).
- Per-row classification: column `detoured` / `installer_rva` in [kenshi-sites.tsv](kenshi-sites.tsv) (325 rows `yes`; an upper bound: it includes hooks installed only in server mode or behind feature switches); the 14 gate rows and 8 AiTick rows are counted with the `no` rows there.

Examples detoured (A): AI_thinkDirect, AI_rateFleeDirect, Character_giveOrder, Character_goTo, Character_isEnemyDirect, CharMovement_goToDirect_*, GameWorld_flipPause, GameWorld_setWorldSpeed, SaveManager_save, SaveManager_load_*, TitleScreen_constructDirect, Inventory_addItemDirect, GunClass_shoot, DoorStuff_swingOpen, BountyLedger_addBounty.
Examples detoured (B): Character_takeMeleeHit, CombatClass_checkSwingHit, MainBarGUI_update, PlayerInterface_handleMouse, GameWorld_deleteLater, FactionRelations_save, FactionRelations_affectRelations, Building_addConstructionProgress, ProductionBuilding_Operate, CraftingBuilding_addToQueue, ZoneManager_activateZone, NavMesh_gatesApply, MedicalSystem_acidRainTick, Pop_SpawnWorldSquad, ActivePlatoon_restockTrader, TaskBrain_plan, GameCamera_update.
Naming pattern of rows that are NOT detoured (B): `Data_*` (read: .data variables, decoded from a code site by dispOff/insnEnd), `Global_*` (bound by BindGlobals), `Havok_*` ctor/thread-memory rows (canary/called), most `Research_*`, `BuildMode_*`, `Ownerships_*`, `NavMesh_*Site` / `NavGen_*Site` / `PatchSite_*` / `*CallSite` (patched, see 3), `GateSite_*` (InstallGate), `LabelSite_*`, `AiStopSite_*`, `CombatStopSite_*`, `Ai_Guard*`/`Code_*` (probe/guard rows, I), `*Hook`-suffixed rows with 0 refs (e.g. CharacterStatsWindow_fillSkillsHook). Table A rows not listed as detoured are called only.

## 3. Hooks outside the tables

M unless marked.
1. Export detours via GetProcAddress + the same InstallDetour: user32!MessageBoxW (0xa530, 0xa6e0: "last words" crash line), user32!SetCursorPos and a DXGI vtable function (from CreateDXGIFactory1, 0xb8f4a0), OgreMain_x64.dll: Texture::_loadImages (0x14800), ResourceGroupManager::resourceExistsInAnyGroup (0x192c0), InstanceBatch::isBatchUnused (0x19ef0, I), Root::_updateAllRenderTargets (2 overloads) and Root::renderOneFrame (2 overloads) (0x4d2d0), Entity::getSkeleton family: OldSkeletonInstance ctor/dtor, createTagPointOnBone, freeTagPoint x2, Entity::shareSkeletonInstanceWith / stopSharingSkeletonInstance / _deinitialise (0x29210); Plugin_ParticleUniverse_x64.dll ParticleSystem::_update (0x17940).
2. IAT patch: 0x142b0 walks the import directory of RenderSystem_Direct3D11_x64.dll, finds the import of `D3DX11CreateTextureFromMemory` from d3dx11_43.dll and overwrites the 8-byte IAT slot with FUN_1800140c0 (original saved at 0x19015e8); 0x14410 restores it (strings "D3DX11CreateTextureFromMemory patched/NOT patched"). No IAT patch on MyGUI found (the module name MyGUIEngine_x64.dll is only looked up at 0x13a20).
3. Vtable slot patches (8 bytes, VirtualProtect RW): physics scene object slot +0x38 -> FUN_1800c8ef0 (0xc8ff0, toggled by 0xcb150, original at 0x1943af8); an object's vtable slot +0x258 -> FUN_18058a590 (0x58a720, original at 0x19486c0). Which Kenshi classes: not identified (open).
4. Inline patches with hard-coded bytes / patched call sites: PhysicsActual_destroyCallSite (B299, 0x18001fd00 writes `mov rcx,r9; mov rax,<FUN_18001fc90>; call rax; nop x3`, 18 bytes); NavMesh instance and give-back sites (B124/B125, 0x1ea0e0, 13 bytes, expected bytes checked, stub within 2 GB); stitch-lock site (0x26 bytes, 0x1ea750: E9 + NOPs, plus 2 detours); NavGen_interiorNodesSite (B126, 0x1eb3a0: redirects one `call` rel32, 5 bytes); atlasfix PatchSite_PreUpdateTickTime (B252, 8 bytes via 0xba23b0, expected bytes checked) and a 40-byte compare/jump block toggled at 0xba25b0 (NOP block vs original 0x1818bccc8); PatchSite_TownZoneLoad (B359).
5. TLS statics patch (0x1800246e0, log prefix `tlsstatics:`): a table of 1001 patch rows (RVA, length, original bytes) at 0x1167b50 and 29 atexit destructor sites at 0x1173700; the table was written for the GOG build (the whole routine runs only when the build check returns 1, else it logs the build name and does nothing; I for the else branch). Each of the 1001 instructions that touch engine scratch statics is replaced with `E9` to a 0x40-byte stub in a 0xfa80-byte RWX block allocated below the image (VirtualAlloc, <2 GB), and the 29 destructors get `C3`. Effect: those statics become per-thread (0x6d0 bytes a thread, TLS slot) so the AI/lane threads do not share them (I, from the log text).
6. Not hooks, same API: page-protection guards (0x1055c0, 0x1256f0, 0x25870: VirtualProtect on the "aipar" data block, read-only toggling).

## Open questions
- Whether the Steam build ever uses the tlsstatics patch (rows are GOG RVAs; the run-time check is `build==1`, I). If Steam runs without it, which lanes are off (the string at 0x11ae3e0 says some lanes refuse when `tlsstatics = 1` is off).
- Per-row classification is done by automatic backward resolution of the accessor result; ~13 direct hook sites without a table row (exports, HandleList::add through 0x12220, limbguard destructor, a few vtable/DXGI cases) are classified by hand, not exhaustively. AiTick rows (88-95): confirm the 8 hooks install on Steam (prologue table at 0x115c0a8 is 16 bytes per entry, which half is which build unverified).
- The vtable patches (3): which Kenshi class vtable, and the identity of FUN_1800c8ef0 / FUN_18058a590.
- Table B field +0x4c: never read by the paths found; if some other consumer (e.g. 0xcd9170) reads it, no evidence yet.
- Whether a scanned address ever differs from the static column on the user's Steam exe (needs runtime evidence: the scan log line `scan: ... DISAGREE` would show it; in the file every pattern matches uniquely at its Steam address).
- 0xcd9170 (E8/E9 chain match for unresolved rows) and the journal at 0x7f2a50 are described from the decompile only.
