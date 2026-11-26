# AnarchyCore.dll: C ABI towards AnarchyKenshi.dll

- File: `AnarchyCore.dll` from mod v0.133.1, SHA-256
  `502a74153fbb23962ff5a1367a3cd9820dced0c13ea5b4bb16de3c66ed49aec0`, image base `0x180000000`;
  all addresses are RVAs.
- Evidence: Ghidra 12.2 pseudocode and `llvm-objdump` disassembly; nothing was run. Parameter
  lists are Ghidra's unless marked *measured*; names and types are guesses.
- ABI version `AnarchyAbiVersion()` = `0x5f`; AnarchyKenshi.dll refuses any other value (see
  [AnarchyKenshi.dll.md](AnarchyKenshi.dll.md)). Wire formats: [protocol.md](protocol.md).

`AnarchyHost*` (192 exports): all are stubs of three bodies (*measured*, disassembly):
`0x1870` = `xor eax,eax; ret` (127 exports, return 0 / false / NULL), `0x29b20` = `ret` (63 exports,
no-op), `0x29b30` = `xorps xmm0,xmm0; ret` (2: `AnarchyHostDepartedSilence`, `AnarchyHostVotedSpeed`,
return 0.0f). `AnarchyCoreCanHost` (`0x1870`) returns 0. Nothing about the server is implemented in
this DLL.

Convention: `client` = object from `AnarchyClientCreate`; return `u8/u32` = bool/count as noted;
`f` marks a `float` parameter or out-pointer (floats in XMM; *measured* where marked, otherwise from
the finite-check on it). Parameter lists are Ghidra's guess unless noted ("AK call" = a call site in
AnarchyKenshi.dll was read, `ak_decomp.c`). RVAs below are of the export body.

| Group | Export (suffix of `AnarchyClient`) | RVA | Parameters -> return |
|---|---|---|---|
| Lifecycle | `Create` | `13cb0` | () -> client* (0x8d0 B) |
| | `Destroy` | `14710` | (client) |
| | `Connect` | `14b60` | (client, host, u16 port, name, password) -> bool |
| | `Disconnect` | `15620` | (client) (immediate, no wait) |
| | `Leave` | `15660` | (client) -> u32 ms or `0xffffffff` (graceful) |
| | `Update` | `15740` | (client) (AK call; Ghidra shows 4 params, only `client` is used) |
| | `State` | `15ca0` | (client) -> 0..4 (see protocol.md) |
| | `PlayerId` | `15d40` | (client) -> u32 |
| | `SilenceMs`, `TransportSilenceMs` | `15cb0`, `15d10` | (client) -> ms since last packet / transport packet |
| | `Ping` | `1b460` | (client) -> u32 ms |
| | `SetLag` | `15fd0` | (client, latency, jitter, u32 loss%) test hook |
| | `SetLanes` | `1c240` | (client, bool) channel 3 for large spine intents |
| | `SetServerPassword` | `1c650` | (client, char*) |
| | `Dropped`, `SpineDropped` | `1c0a0`, `1c4b0` | (client, u32 type/kind) -> u32 drop count |
| Chat / text | `SendChat` | `15d50` | (client, char*) -> bool |
| | `PollMessage` | `16020` | (client, char* buf, int cap) -> bool (client log/status lines, AK call cap `0x400`) |
| | `TakeChat` | `19550` | (client, u32* playerId, char* buf, cap) |
| | `TakeNotice` | `18e70` | (client, u32* kind, u32* x, char* text, cap) |
| | `TakeSpeech` | `18d40` | (client, u32* entity, u32* kind, char* text, cap) |
| | `Rules` | `194a0` | (client, u32* v, char* buf, cap) -> bool |
| Position/snapshot | `SendPosition` | `15200` | see protocol.md |
| | `SnapshotTick`, `SnapshotGameTimeMs` | `1b890`, `1b8a0` | (client) -> u32 |
| | `SnapshotSpeed` | `1b8b0` | (client) -> f32 |
| | `SnapshotVoters`, `SnapshotPlayers` | `16000`, `16010` | (client) -> u8 |
| | `SnapshotCount` | `1b8d0` | (client) -> n |
| | `SnapshotEntity` | `1c560` | (client, i, 10 out ptrs: id, x, y, z, vx, vy, vz, dirA, dirB, flags) |
| | `RequestSpeed` | `15ed0` | (client, f32) -> bool |
| | `SpeedVotes` | `1b260` | (client, out ptr, out ptr, cap, u32*, u32*) (Ghidra's guess, not read) |
| | `Clock` | `17470` | (client, f32* out) -> bool |
| | `RequestResync` | `1b910` | (client, const u32* ids, n) -> bool |
| Entities | `SpawnCount/At/ClearSpawns` | `16220`, `16260`, `16df0` | see B |
| | `DespawnCount/At/Clear` | `16e70`, `16e90`, `16ed0` | (client, i, u32* id) |
| | `VitalsCount/At/Clear` | `16ef0`, `16f10`, `1b7f0` | At: 20 args |
| | `CensusFresh/Count/At/Clear` | `1c080`, `1c4d0`, `1c4f0`, `1c530` | (client, i, u32* id) |
| | `Squad`, `ClearSquad` | `18b70`, `18bf0` | (client, u32* out, cap, ...) copies the id list |
| | `Chained`, `ClearChained` | `18c00`, `18c90` | same |
| | `Owners` | `18ca0` | (client, out pairs, cap(0x400), u32* n) |
| | `MainCharacter`, `HasCharacter` | `16dc0`, `16dd0` | (client) -> u32 / bool |
| | `SendCharacter` | `16440` | (client, name, s2, s3, f32, blob, len) -> bool |
| | `TakeAppearance` | `16630` | (client, u32* entity, char* blob, cap, ...) |
| | `TakeKit`, `Word`, `DictSize`, `DictGaps` | `16990`, `16d00`, `16d90`, `16db0` | `TakeKit(client, u32* entity, u32* x, char* blob, cap, u32* len)` |
| | `TakeCharacterRefusal` | `19520` | (client) -> bool |
| | `OwnSkills` | `1add0` | (client, u32* entity, char* blob, cap, u32* len) |
| Combat | `SendOrder` / `SendAttackOrder` / `SendMedicOrder` / `SendCarryOrder` | `1bad0` / `1bc50` / `1bf30` / `176e0` | see A |
| | `SendBehaviourOrder` | `1b490` | see A (AK call) |
| | `Behaviour Count/At/JobAt/Clear` | `1b5d0`, `1b5f0`, `1b680`, `1b790` | At: (client, i, 4 out ptrs); JobAt: (client, i, job, 9 out ptrs) |
| | `Engage Fresh/Count/At/Clear` | `17320`, `17340`, `17360`, `1b480` | (client, i, u32* a, u32* b) |
| | `TakeHit` | `188c0` | (client, 6 out ptrs) (AK call) |
| | `TakeShot` | `18ad0` | (client, u32*, u32*, u32* u8) |
| | `Healings Fresh/Count/At/Clear` | `174a0`, `174c0`, `17500`, `175a0` | At (client,i,u32*,u32*,u32* bool) |
| | `States Fresh/Count/At/Hand/Clear` | `18720`, `18740`, `18760`, `18880`, `188b0` | CombatStates (At 9 out ptrs; `StateHand(client, i)` -> bool) |
| | `Anims Fresh/Count/Fingerprint/At/Clear` | `18540`, `18560`, `185a0`, `185b0`, `18710` | CombatAnims |
| | `TakeLeadOrder` | `189c0` | (client, 6 out ptrs: kind u8, entity, ..., f32 x3...) (AK call) |
| | `Intent Count/At/Clear` | `16130`, `16170`, `1b870` | Intents: (client, i, 6 out ptrs) (AK call) |
| Items | `Carries Fresh/Count/At/Clear` | `173c0`, `173e0`, `17400`, `17460` | (client, i, u32*, u32*) |
| | `Loot Count/At/Clear` | `17b20`, `17b60`, `17d30` | At: 15 args |
| | `SendDropNotice`, `SendPickupNotice`, `SendStashNotice`, `SendRobNotice` | `17ec0`, `17db0`, `180e0`, `18320` | see A |
| | `SendUseReport`, `UseState Count/At/Clear` | `17970`, `178a0`, `178b0`, `17950` | |
| | `Actions Fresh/Count/At/Clear` | `175b0`, `175d0`, `17610`, `176d0` | At (client, i, u32* id, char* name, cap) |
| | `TakeCrate`, `TakeCrateItem`, `SendCrateStash` | `19720`, `199b0`, `19b00` | `TakeCrate(client, 4 out)`, `TakeCrateItem(client, char* name, cap, u32* count, u32* slot)` (AK call) |
| | `SendItemMove`, `TakeItemMoveResult` | `19c60`, `1a810` | `TakeItemMoveResult(client, u32* id, u32* a, u32* b)` |
| | `SendBag`, `SendBagLayout`, `SendBagResync` | `1bda0`, `1a9b0`, `1a8d0` | see A |
| | `SendSkillGains` | `1ac40` | (client, u32 id, blob, len<=0x400) |
| World | `Structures Fresh/Count/At/Clear` | `18fa0`, `18fc0`, `19000`, `19100` | At (client, i, char* name, cap, f32* x3, i32*) |
| | `TakeBuilds`, `SendBuildIntent` | `19170`, `19260` | `TakeBuilds(client, char* buf, cap, u32* len)` -> u32 |
| | `TakeRecall`, `TakeDoor`, `SendDoorReport` | `193e0`, `19660`, `1ab70` | `TakeDoor(client, f32*, f32*, f32*, u32* state)` |
| Players | `PlayersVersion/Count/At`, `AmAdmin`, `PlayerIsAdmin`, `PlayerPing` | `1b060`, `1b070`, `1b0b0`, `1b180`, `1b1c0`, `1b210` | |
| Spine | `SpineSendIntent`, `SpineSendIntentLossy`, `SpineTake` | `1c250`, `1c390`, `1c0d0` | `SpineTake(client, u32* kind, char* buf, cap, u32* len)` |
| Browser/report | `Browser Refresh/Watch/Unwatch/State/Version/Count/At/Status` | `1c690`, `1c780`, `1c800`, `1c8e0`, `1c960`, `1c9e0`, `1ca80`, `1cb60` | see D |
| | `ReportSend/State/Status` | `1cc50`, `1ce40`, `1cec0` | see D |
| Misc | `AnarchyAbiVersion`, `AnarchyProtocolVersion`, `AnarchyCoreVersion` | `13ae0`, `13af0`, `13b00` | () -> 95, 133, version string |

Each `...At` export returns 0 when `i` is out of range; the `Take...` exports pop one queued item
(ring buffers at `client+0x8a8...` for Kit etc.; Kit queue size 0x400) and return 0 when empty.
Ghidra's parameter counts for `At` functions are as shown in `sigs`; sizes were not re-verified
for every export.
