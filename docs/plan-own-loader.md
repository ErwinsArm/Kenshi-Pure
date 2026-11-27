# Plan: KenshiPure foundation without RE_Kenshi (own loader, own address layer)

Goal: KenshiPure starts on its own and early (before the game window exists), with no other mod
needed, on the exe the player has: Steam 1.0.68 as shipped, RE_Kenshi's 1.0.65 copy, GOG. It is the
base for features that fix what players feel in AnarchyKenshi (AK): slow item moves and inventory,
slow world loading (AK lowered movement speed), broken shopkeepers. Model: AK's own approach. Both
critics reviewed this design twice. Labels: **M** measured, **D** documented, **I** inferred.

## Facts it rests on

1. KenshiLib 0.5.0 knows only the 1.0.65 exes (MD5 map; `Steam_1.0.65.br`, `GOG_1.0.65.br`, 7630
   entries: 7622 in `.text`, 8 globals) (**M**). RE_Kenshi's copy has KenshiLib's Steam 1.0.65 MD5
   `df4a5a7e…` (**M**). No KenshiLib release mentions 1.0.68; RE_Kenshi runs 1.0.68 installs through a
   1.0.65-compatible exe (**D**). [kenshi-build.md](findings/kenshi-build.md)
2. AK is an Ogre plugin in `Plugins_x64.cfg`; with tables of 795 sites (Steam 1.0.68 RVA, a second
   column of unknown build, masked pattern) it found 798/799 sites in 500 ms on the 1.0.65 exe, and
   drops hits far off the median shift (**M**, AK log, [hooks.md](findings/hooks.md)). AK's second
   column is not GOG 1.0.65 (43 of 756 hits, chance level, **M**).
3. AK also patches mid-function sites, redirects call sites, patches vtable slots, one IAT entry, a
   1001-site static-to-thread-local table, and hooks OgreMain/ParticleUniverse exports (**M**, hooks.md §3).
   About 173 of its 795 sites match no KenshiLib declaration (**M**, [ak-kenshilib-names.tsv](findings/ak-kenshilib-names.tsv)).
4. 1.0.65 → 1.0.68: `.text` grows by 0xF20 bytes; code moves in about 17–18 blocks with constant
   shifts from −0x7e0 to +0xed0; in a sample 346 of 348 neighbouring functions keep their order (**M**,
   critics' scripts). Unique-prologue matching alone bound wrong functions (e.g.
   `AppearanceManager::setHead` +0x6d9f10) (**M**).
5. Display: the game creates the swap chain windowed, `DXGI_MODE_SCALING_STRETCHED`; AK sets
   `UNSPECIFIED` 0.16 s before the call (**M**, AK log, [AnarchyKenshi.dll.md](findings/AnarchyKenshi.dll.md) §5).
   The user's game folder has ReShade (`dxgi.dll` 5.4.2) (**M**).
6. KenshiLib names are community guesses in places (AK's `AI_haveRepairKit` is KenshiLib's
   `haveFirstAidKit_robot`) (**M**); field values such as `knockoutTimer` are doubtful (docs/client.md).

## Design

### 1. Loader `KenshiPureLoader.dll` (VS2010, thin)
- Ogre plugin (`dllStartPlugin`/`dllStopPlugin`), line appended after `Plugin=RE_Kenshi` if present;
  pins itself against unload; single instance per process; logs pid, exe path, SHA-256.
- AK detection by reading `Plugins_x64.cfg` (AK may load after us), not by `GetModuleHandle`.
- Order: display hook → address layer → `KenshiPure.dll` through a versioned C interface
  (`kpStart(const KpHost*)`, the struct carries a version and the resolved slot table).

### 2. Display fix
Hook `IDXGIFactory::CreateSwapChain` on the factory object the game actually receives (ReShade may
wrap it); inside the first call hook `ResizeTarget` once. Full screen in `kenshi.cfg` → `Scaling =
UNSPECIFIED`; log before/after. Minimize-restore (`client/Display.cpp`) stays as fallback.

### 3. Address layer
One committed table `client/sites.tsv`; columns: name, kind, module (exe, OgreMain, …), required or
optional, per-build value (builds named by SHA-256; GOG 1.0.65 marked "MD5 only, unmeasured"),
pattern, offset of the site inside the pattern, expected bytes (patches), evidence (where it was
proven). Kinds: `code` (function), `global` (from a RIP-relative operand of a matched anchor),
`field`, `vslot` (or RTTI vtable lookup), `patch` (mid-function bytes), `callsite`, `data`.
- **Known build** (SHA-256 match): check every row's pattern at its recorded address; mismatch →
  row unresolved.
- **Shift check**: a 1.0.68 address must equal the 1.0.65 address plus its neighbours' block shift
  and keep the order of its neighbours; anything else is rejected, even a unique hit.
- **Unknown build**: scan the exe file for patterns, apply the same shift/order check, but run only
  the display fix and read-only features: layouts are unproven there.
- Every slot is pre-filled with a trap stub that logs the name; nothing ever jumps to 0 or the image base.
- `required` rows missing → KenshiPure stays inactive (log says which). `optional` rows → the feature
  is off, and the feature set is sent to the server at login so no client silently desyncs.
- Patterns are made by us from the exes, never copied from AK (AK's tables only tell us *where* to
  look). Patterns of Kenshi's exe are committed (user's approval).

### 4. Calling and reading the game
- Functions: KenshiLib headers for declarations; a generated ml64 file defines each used mangled name
  as `jmp [slot]`. `ou` and other globals: our own slot, KenshiLib's `Globals.h` shadowed.
- **Fields only through `KP_FIELD(Class, member)` accessors** that read the per-build offset from the
  table and register the row; the build fails on raw member access to game structs (checked by a
  script over the source). Writes need a row whose evidence says "both builds proven".
- Build of `sites.tsv` rows: `scripts/sites.mjs` (address from KenshiLib's table or an RVA, pattern with
  operands masked by a real x64 decoder, pinned like other tools; uniqueness in both exes; shift
  check); `sites.mjs check` re-proves all rows against both exes before a build that changes them.

### 5. Hooks
Own hook dispatcher: one detour per site, a list of listeners behind it (several features on one
function). Engine: MinHook pinned by SHA-256 (its `multihook` branch or upstream behind our
dispatcher), proven to build with cl 16.00; code-cave allocator for `patch`/`callsite` kinds.

### 6. Split by compiler
Only the game adapter (loader, address layer, hooks, game calls, accessors) is VS2010 (v100, Kenshi's
CRT). Protocol, sync, prediction and later server-shared logic go into a modern-compiler DLL behind a
plain C interface (as [plan-first-playable.md](plan-first-playable.md) already intends).

### 7. Using AnarchyKenshi as a map
AK's decompile (REA/Ghidra) stays our guide for *where* to intervene for each player problem
(loading and movement speed, inventory, shops, the thread-local patch table) and how AK did it;
each site we need is then rebuilt and proven in our own table.

## Blind spots still open (need runs or data)

- RE_Kenshi on/off and the plugin order: needs a user run with the loader's pid log.
- ReShade with the DXGI hook: needs a user run.
- 1.0.68 layouts: proven per field as features need them; until then 1.0.68 is read-only for that field.
- GOG: no exe here; best effort through the unknown-build path.
- Redistributing our built DLLs (VS2010 / SDK 7.1 runtime terms) before a public release: check then.
- Without RE_Kenshi its shader cache and fast heightmap are gone (slower loading than with it).

## Steps

1. Toolchain: pinned MinHook and x64 decoder; build check for the new DLLs.
2. Address layer + accessors + trap stubs for 1.0.65; KenshiPure without KenshiLib.dll, still started by
   RE_Kenshi → same log as today.
3. `sites.mjs` with shift check; 1.0.68 rows for everything KenshiPure uses; fields proven.
4. Loader + display hook + installer line; RE_Kenshi `startPlugin` path removed.
5. User runs: RE_Kenshi on / off, ReShade on: logs, full screen without Alt+Enter, probe works.
6. Split out the modern-compiler core before M2 (server) starts.
