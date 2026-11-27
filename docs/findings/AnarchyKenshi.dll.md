# AnarchyKenshi.dll: start, AnarchyCore loading, server mode, RCON

- File: `AnarchyKenshi.dll` from mod v0.133.1, SHA-256
  `c3d2419d1c02b516552032e9037d29ec1a1b88661199ddac7775f601d97b51bb`, PE32+ x64, image base
  `0x180000000`. All addresses are RVAs in this file unless they are marked as Kenshi RVAs.
- Kenshi reference: the Steam `kenshi_x64.exe`, SHA-256
  `a596ab4e407c67b58599c54ffb32dc1bf2b64510cdebd3fa9359ef05a576aeb1` (read statically).
- Evidence: Ghidra 12.2 decompile of the whole DLL, `llvm-objdump` disassembly, strings, `.pdata`
  function bounds. Nothing was run. Labels: **M** = measured (decompile, disassembly, strings,
  xrefs, bytes), **I** = inferred. Ghidra `FUN_…` names and every function name given here are
  guesses. In the decompile, `HMODULE + N` is scaled by 4 (`sizeof(HMODULE__)`), so the byte offset
  is `4*N`.
- Related notes: [hooks.md](hooks.md) (how Kenshi code is found and hooked, with
  [kenshi-sites.tsv](kenshi-sites.tsv)), [spine.md](spine.md) (the Spine state sync),
  [protocol.md](protocol.md) (AnarchyCore.dll wire protocol).

## 1. startPlugin (`0x1b20`–`0x3ac9`) in order

1. Run-once guard (`0x1970388`). Log file opened as `AnarchyKenshi` + suffix; suffix = `ANARCHY_MODE`
   value, replaced by `-<ANARCHY_NAME>` when that is set (`0x555b0`, `0x555e0`, `0xa900`). **M** (suffix use: **I**)
2. Logs `plugin loaded`, then `build: GOG|Steam|unknown` from `0xcd4000` → `0xcd3e00`. **M**
   - `0xcd3e00` compares masked canary bytes in `kenshi_x64.exe` (via `GetModuleHandleA(NULL)`):
     GOG column = exe RVA `0x8b9630` (28 B) **and** `0xba9b90` (33 B); Steam column = `0x8b9ef0` (28 B)
     **and** `0xbaa450` (33 B); same patterns (`0xee54b0/0xee54d0`, `0xee4a00/0xee4a28`) via `0xcd3fc0`.
     GOG-only match → 1, Steam-only → 2, neither/both → 0 with "canary bytes match neither column …
     addresses come from the live-image scan". Result cached (`0x195a894`). **M**
3. `0xcd9370` "scan": sweeps the game's `.text`, checks table addresses ("scan: %s found at 0x%IX, the
   table says 0x%IX"), writes a summary into `.data` `0x15a5870` ("scan: not run" is the initial text);
   startPlugin logs it. **M**
4. `0xcd8a90` binds 5 game globals (`Global_gameWorld`, `gameGui`, `gameInput`, `gameSettings`,
   `renderMaster`), summary into `0x15a5970` ("globals: bound %u/5 …"), logged. **M**
5. `0x4e3d0` reads config into a stack struct (`local_c88`): `AnarchyKenshi.cfg` next to the DLL plus
   env overrides (decompile timed out; read from disassembly). Field 0 = mode: `ANARCHY_MODE` equal to
   `server` (6 chars) or `host` (4 chars) → 1 (server), else 0 (client) (compares at `0x524ee`,
   `0x52517`, strings `0x11af5f4`, `0x11af5dc`). Field `+0x30` = game port. **M**
   Env vars read there: `ANARCHY_MODE, NAME, PORT, STAND, BROWSER, JOINPASSWORD, RCONPASSWORD,
   RCONPORT, WORLDSAVE, MISSION, SNAPRATE, MAXPLAYERS, SHARD, SHARDSECRET, SHARDPEERS, SHARDINIT,
   SHARDIP, FIGHTJOURNAL, RENDER, CONSOLE, AUTOLOAD, WORLDKEEP, START`. Elsewhere on the startPlugin path:
   `ANARCHY_WINPOS/CONPOS/FOCUS` (`0x18d0`), `ANARCHY_FPS` (`0x1700`), `ANARCHY_SERVERLOCK` (`0xc1c580`),
   `ANARCHY_TLSSTATICS` (`0x2c52`), `ANARCHY_TEXSTUB` (`0x14800`), `ANARCHY_PROPERTY` (`0x1535b0`). **M**
6. Server only: single-server lock `0xc1c580` (unless `0x1955f1c` overrides): `CreateFileW` on
   `<accounts path>.lock`, writes "pid …, started …, port …, server …"; held → "lock: ANOTHER SERVER
   ALREADY RUNS ON THESE FILES"; `ANARCHY_SERVERLOCK=0` turns it off. Failure sets `refused`
   (`cVar19`). **M**
7. Crash handling: server → `0xa880` + `0xa6e0` (hooks `MessageBoxW`, "last words … hangs the server");
   client → `0xa530`. **M**
8. ~60 feature inits, most take `isServer`; **hooks are installed per feature inside them** (e.g.
   `0x52f7f0` localsim, `0xb9bb10` prison/bed, `0xcdad30` `Dialogue::say`), there is no single
   "install hooks" call. Server-only inits: `0x7304e0, 0x780220, 0x780140, 0x793eb0, 0xbde230, 0xc09760,
   0x8fcee0`; client-only: `0x1b9ad0, 0x908210, 0xcc5f30, 0xcc7660, 0xc09930`. **M** (feature meaning: **I**)
9. Window/console title `Kenshi [SERVER|CLIENT] <name>` (+ " - REFUSED: another server holds these
   files" when the lock failed) → `0x15ac820`. **M** (that it is a title: **I**)
10. Registers `HeadlessWindowUpdate`; `0x18d0` (window/console placement env vars — not hooks). **M**
11. **Core load** `0x55850` (section 2). If it fails (`je 0x37bc` at `0x2653`) the whole mode block
    (steps 12–13) is skipped, only the common tail (14) runs. **M**
12. Logs `plugin version 0.<protocol>.1 (protocol <protocol>)` — built from `AnarchyProtocolVersion()`
    with a literal `1` (`0x266e`–`0x26b0`), i.e. "0.133.1" with this Core. **M**
13. Mode branch:
    - **Server** (`mode==1`): `0x5a900` host start (only if not refused and Core loaded); per-frame
      tasks `CoreHostUpdate (0x5aea0), PlayerFactionsSafePoint, PlayerResearchSafePoint, BountySafePoint,
      ServerVersionTick (0xcd1d70), FurniturePeelerPump`; host setters JoinPassword/ServerName/Browser
      and log "browser: announcing to <url> every <n>s" (default 30); headless: "AnarchyKenshi server - "
      title, `HeadlessUpdate (0xc101a0)`, `TexStubTick, MatSweepTick, FxThrottleTick, InstSweepTick`;
      `PhysQueueLocksUpdate, EditorSkipUpdate`; if not refused: accounts file check `0x14fc00`,
      `AnarchyHostSetAccountFile(host, path)`, shard setup `0x5cb90`; world/mission load
      (`0xcaad80`, "world: this server runs a mission …"); `ServerControlUpdate (0xcabaa0)`,
      `ServerProbeUpdate (0xccc680)`. **M**
    - **Client**: `LocalSimUpdate, CoreUpdate (0x5a270), MenuUiUpdate`, optional FPS cap/HeadlessUpdate,
      ~27 UI/pump tasks (`ChatUiUpdate` … `InputGuardLockDebugKeys`), `ServerControlUpdate`, artificial
      lag ("net: ARTIFICIAL LAG …"), then autoconnect `0x5a080` or "core: autoconnect disabled, waiting
      for the MULTIPLAYER menu"; logs "sync: snapshot <Hz> Hz, world drawn <ms> ms behind". **M**
    - Both: `WorldSyncUpdate, StructureWireUpdate, BuildNavTick`. **M**
14. Common tail: `ZoneProbeUpdate, LoaderWatchUpdate, SquadHandlesUpdate, DataPrintPump`; server-only
    adds `PhysWatchUpdate, CellCensusUpdate, PropertyBookTick, LootRenewTick, RuinRenewTick,
    PropertyTradeTick, MeshSweepTick, RagdollCensusWatch, DeleteDrainUpdate, ItemSlotsTick,
    SkelWatchUpdate, ThreadCensusUpdate`; client adds `PropertyTradeTick`; then `ZoneKeeperUpdate,
    GearLimbGuardPump`, final `0x3ad0(config)`. Tasks are registered via `0x807e0(fn, name)`. **M**

## 2. How AnarchyCore.dll is loaded (`0x55850`)

- Path = directory of this DLL (`GetModuleFileNameA` on the handle saved at `0x1b09` → `0x4dc50`) +
  `\AnarchyCore.dll`; `LoadLibraryA`; handle `0x1942050`. Fail → "core: AnarchyCore.dll not loaded
  (<err>), networking disabled". **M**
- Resolver is **not a table walk**: 347 unrolled `GetProcAddress` calls with inline names, each result
  stored in its own global (`0x1942068`…`0x1942b40`). A miss logs "core: export not found: <name>"
  (`0x5fb70`) and sets a flag; after all 347, flag set → `0x59fa0` (destroy client, `FreeLibrary`,
  clear pointers). **M**
- 347 = 191 `AnarchyHost*` + 152 `AnarchyClient*` + `AnarchyAbiVersion, AnarchyCoreVersion,
  AnarchyProtocolVersion, AnarchyCoreCanHost`. All 191 `AnarchyHost*` exports are resolved; every
  resolved name exists in Core's export list. Core exports not resolved: `AnarchyHostDestroy` (+ 3
  C++/startPlugin leftovers). **M**
- ABI check: `AnarchyAbiVersion() == 0x5f` (`cmp eax,0x5f` at `0x59c53`). Mismatch → "core: ABI
  mismatch - core v<n>, plugin expects v95. Networking disabled." and unload. Then
  `AnarchyClientCreate()` must return non-null (handle `0x1942058`, also in server mode) else
  "core: could not create the client" and unload. Success → "core: loaded, version <s>, ABI v95,
  plugin dll <path>". **M**
- Protocol `0x85` is **not checked at load** by this DLL. It is only used for the version string
  (step 12) and compared to server-browser entries (`0xaff5d`, shows " - update"). The real
  protocol gate is in Core's Hello (protocol.md). **M**/**I**

## 3. Server mode — is the server in the package?

**Answer: the server *game logic* is in AnarchyKenshi.dll, but it can't run, because the shipped
AnarchyCore.dll has no host. So the package is client-only in practice.**

Strongest evidence:
1. `0x5a900` (only caller: startPlugin `0x2752`): if `AnarchyCoreCanHost` is null or returns 0 →
   logs **"host: this build has no server in it - the player package ships a client-only
   AnarchyCore.dll. Nothing to start, and no setting can change that."** (`0x11b2840`) and returns
   without calling `AnarchyHostCreate`. Shipped Core's `AnarchyCoreCanHost` is the 3-byte stub at Core
   `0x1870` returning 0 (core.c). **M**
2. The host handle `0x1942128` is written **only** at `0x5a99a` (result of `AnarchyHostCreate(port,
   maxPlayers default 64, name)`), and the 1187 reads of it in 723 functions all gate on it being non-null
   (e.g. `CoreHostUpdate 0x5aea0`, RCON start `0x23d6f0`). With the stub Core it stays 0, so every
   host path is dead. **M** (that *every* read is a null gate: **I**, checked on samples)
3. That dead code is real server logic, not thin wrappers: 189 of the 191 resolved `AnarchyHost*`
   pointers are called from ~530 call sites (only `AnarchyHostSendBehaviourTo`, `AnarchyHostShardCanHold`
   are never called). Examples: snapshot build `0x429af0` (`BeginSnapshot/AddEntity/SendSnapshotTo`,
   "server: world up - N characters, serving"), client input `TakePlayerMove 0x3d2660`, `TakeOrder
   0x3dc220`, `TakeAttackOrder 0x4087a0`, `TakeBag 0x332320`, accounts `RememberSkills/RememberCharacter
   0x3ac2f0`, `SavedSkills 0x5e0e0`, `SaveAccounts` (6 callers), shards `0x5cf10–0x5d1d0`, world save
   `0xc903d0` ("world: saving \"…\""), `HostPeerCount` (106 callers). **M**

Split of responsibilities (**I**, from the call pattern): the server is a headless Kenshi instance with
this DLL — Kenshi's own simulation is the authority; this DLL reads game state, builds snapshots,
applies client orders, saves the world. AnarchyCore owns the transport (ENet), message encode/decode,
and the accounts file contents (`HostSetAccountFile`, `Remember*/Saved*`, `SaveAccounts`). In this
package those Core parts are stubs.

What `ANARCHY_MODE=server` still does with the shipped Core: lock file, server-only hooks and per-frame
tasks, headless window, world/mission load; `0x5a900` returns 0 (ignored by startPlugin), no port is
opened, `CoreHostUpdate`/RCON/browser calls stay inert. **I** (from the code path, not run)

Server-feature strings: "AnarchyKenshi server - " (`0x11a6ef0` area), "browser: announcing to ",
"host: listening on port … up to N player(s)", "host: could not bind port …", "server: world up - ",
"world: saving \"", "save-guard: SERVER swallowed the game's own autosave", "shard: region mode ON",
"lock: ANOTHER SERVER ALREADY RUNS ON THESE FILES", "rcon: listening on TCP ". **M**

## 4. RCON over TCP

- Only Winsock user group: `0x250d90` (start: `WSAStartup`, `socket(AF_INET,SOCK_STREAM,TCP)`,
  `SO_REUSEADDR`, bind `rconbind` or `0.0.0.0`, `listen`, `CreateThread(0x250720)`), `0x250720`
  (thread: `select` 50 ms, `accept`, `recv` 8 KiB, split on `\n`, line > 4096 → "0!line too long"),
  `0x250230` (new connection: max 8 clients else "AKRCON busy", greeting **`AKRCON 1 <nonce>\n`**;
  nonce = 16 bytes from `RtlGenRandom` (`SystemFunction036`) hex-encoded, `0x2434c0`). **M**
- Started only from `0x23d6f0` (caller `0x23db59`), which returns unless host handle `0x1942128 != 0`
  **and** mode (`0x195c880`) == 1 → **server mode only, and never with the shipped Core**. Needs
  `rconpassword` (cfg) / `ANARCHY_RCONPASSWORD` ≥ 12 chars ("rcon: off - … shorter than 12
  characters"). Port = `rconport`/`ANARCHY_RCONPORT` if > 0, else the game port (`0x195c8b0`, same
  config offset `+0x30` passed to HostCreate). **M** (game-port default: **I**, offset match)
- Auth (`0x24f2f0`): first line must be `auth <hex>`; expected = lowercase hex of
  **HMAC-SHA256(key = rcon password, msg = nonce)** (`0x243210`: ipad `0x36`/opad `0x5c`, 64-byte block;
  SHA-256 IV at `0x124b988`); input lowercased (`0xce0460`), constant-time compare. OK → `ok\n`, log
  "rcon: <ip> signed in". Wrong → `denied\n`, close, "rcon: wrong password from <ip> (n in a row)";
  5 failures within 10 min → that IP silenced 15 min. **M**
- After auth (`0x24cc30`): request `<id> <op> [args]` (id up to 9 digits; errors `0!…`). Ops (help text
  `0x11d3070`): `ping, quit, cmd <console line>, cmdas <admin>\t<console line>, state` (JSON for a
  panel), `events <since> [max], journal <account> [n], skills <account> [n], judge <account>, known,
  world, structures <x,z>, owned <account>, banlist, history <account>, help`. `cmd` lines go to the
  in-game server console via a game-thread queue (max 64 jobs); console refuses some verbs from RCON
  (`load`, `buildnav regen`, `passwd` with a password). Events are also written to
  `<name>-player-events.tsv`. **M**

## 5. Display fix (full screen in the wrong resolution)

Known from log strings and AK's own log of a run on the user's PC (v0.136.1, RE_Kenshi's 1.0.65
exe); not decompiled. Explains why plain Kenshi can start full screen stretched from a smaller mode
until Alt+Enter, and AK does not.
- Cause (**M**, AK log): Kenshi (Ogre D3D11) creates its swap chain as "1920x1080 @ 0/0 format 28
  scaling STRETCHED, windowed"; AK's log: "scaling set to UNSPECIFIED (with it DXGI picks a smaller
  scaled mode)". Hook on `IDXGIFactory::CreateSwapChain` (string RVA `0x127db80`) rewrites
  `BufferDesc.Scaling`; also hooks `IDXGISwapChain::ResizeTarget` (`0x127d9b0`).
- Timing (**M**, AK log): hook installed 2.3 s after AK's first log line; the game's
  `CreateSwapChain` call came 0.16 s after that. So an Ogre plugin from `Plugins_x64.cfg` is early
  enough; RE_Kenshi post-load plugins are not.
- About 5 s later it compares window, client, monitor and picture size; all 1920x1080 → "nothing to
  do". Only on a mismatch it minimizes and restores the window (0.7 s, `0x127dcd0`, `0x127dd68`) (**I**
  from the strings).
- Guards `SetCursorPos`: the game moves the cursor only while its window is in front (`0x127da10`).
- Reads `Full Screen=` from `kenshi.cfg`; update function name string `DisplayFixUpdate`.

## Open questions

- Full `0x4e3d0` config reader (decompile timed out): cfg key names ↔ struct offsets, defaults
  (game port, max players 64 is from `0x5a900`).
- What `0x1955f1c` (lock override) and `local_9e8` (`srv`/`cli` + name → `0xb94c70`) are.
- Whether any host-handle reader runs real work without the null gate (only samples checked).
- `AnarchyHostSendBehaviourTo` / `AnarchyHostShardCanHold` resolved but never called; `AnarchyHostDestroy`
  exported but never resolved — leftover or used elsewhere?
- Whether a server-capable AnarchyCore.dll (CanHost ≠ 0) exists anywhere; the string says the
  player package is deliberately client-only.
