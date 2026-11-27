# AnarchyKenshi network protocol (client side, AnarchyCore.dll)

- File: `AnarchyCore.dll` from mod v0.133.1, SHA-256
  `502a74153fbb23962ff5a1367a3cd9820dced0c13ea5b4bb16de3c66ed49aec0`, PE32+ x64, image base
  `0x180000000`. All addresses are RVAs.
- Source of evidence: Ghidra 12.2 pseudocode of `.text` `0x1000`–`0x40000`, `llvm-objdump`
  disassembly of the whole file, strings and exports. Everything is static; nothing was run and
  no server was contacted.
- Field meanings marked "AK" come from `AnarchyKenshi.dll` v0.133.1 (SHA-256
  `c3d2419d1c02b516552032e9037d29ec1a1b88661199ddac7775f601d97b51bb`, image base `0x180000000`,
  RVAs prefixed `AK`): its client code that fills or reads a Core export, and its host-mode code
  that feeds the `AnarchyHost*` calls (present in AK although the shipped Core stubs them, so the
  hop from a host call to the wire is never seen: *inferred*). Kenshi offsets and functions are
  for the Steam `kenshi_x64.exe` (SHA-256 `a596ab4e…aeb1`, see [hooks.md](hooks.md)); "site
  table A n" is the 0-based row n of table A in [kenshi-sites.tsv](kenshi-sites.tsv). AK imports
  OgreMain by ordinal; ordinals were resolved against the Steam `OgreMain_x64.dll` (SHA-256
  `7d3888d21398647519f10a89b1f300d8606f761ad159e6021a797b1b9b3df5f6`).
- Ghidra names (`FUN_…`) and its parameter types are guesses. Where a float is passed in an XMM
  register Ghidra shows it as `uint`; checked in disassembly where it matters (see SendPosition).
- This is the **player build**: all 192 `AnarchyHost*` exports and `AnarchyCoreCanHost` are
  stubs (three bodies, see [AnarchyCore-abi.md](AnarchyCore-abi.md)), so everything below is what
  the client sends and accepts. The server side is inferred from the client's checks.
- Covers the repo copy of v0.133.1 only (protocol `0x85`). Later releases change the protocol
  and ABI numbers; diff before building against another version.
- Coverage: all message IDs and payload layouts both ways (field sizes, order and limits
  *measured*; most field meanings *inferred*), login, ENet use, HTTP browser and report. The
  export ABI is in [AnarchyCore-abi.md](AnarchyCore-abi.md). Open items are listed at the end.

## Versions (*measured*: export bodies)

| Export | RVA | Returns |
|---|---|---|
| `AnarchyProtocolVersion` | `0x13af0` | `0x85` (133); also hard-coded in Hello and HandoffHello |
| `AnarchyAbiVersion` | `0x13ae0` | `0x5f` (95), version of the C ABI towards AnarchyKenshi.dll |
| `AnarchyCoreVersion` | `0x13b00` | a string (builder not read) |

## 1. Transport: statically linked ENet

ENet 1.3.x is compiled in (no ENet DLL is imported). Evidence (*measured*):

- `enet_host_create` is inlined at `0x3d7a0`: allocates a `0x2b10`-byte host and `0x1c8`-byte
  peers, opens `socket(AF_INET, SOCK_DGRAM)`, sets non-blocking, `SO_BROADCAST`, `SO_RCVBUF` and
  `SO_SNDBUF` = 256 KiB, default `mtu` `0x570` (1392), `duplicatePeers` `0xfff`,
  `maximumPacketSize` and `maximumWaitingData` = 32 MiB. These are ENet's own defaults.
- `enet_host_connect` is inlined at `0x3dbf0`: connect command byte `0x82`
  (`CONNECT | ACKNOWLEDGE`), channel `0xff`, window size from outgoing bandwidth, channel count 5,
  connect data 0, and the connect ID from `seed += 0x6d2b79f5` plus an xor-shift multiply mix.
  The ENet version is not pinned (see "Version" below).
- `enet_initialize` (`WSAStartup` at `0x42223`, `timeBeginPeriod(1)` at `0x42243`), only
  `WSASendTo` (`0x42a36`) and `WSARecvFrom` (`0x42bf9`) touch the socket, `select` at `0x42d40`.
  The ENet protocol code itself lies at `0x3d000`–`0x43000`.

Client host (`0x39310`, *measured*):

- One peer, 5 channels, bandwidth unlimited.
- After creation the client sets `host->mtu = 1200` (`0x4b0`) and
  `host->maximumPacketSize = 0x10000`.
- `ANARCHY_BIND_LOOPBACK=1` binds the socket to `127.0.0.1`; otherwise it is unbound (ephemeral
  port).
Checksum and compression (*measured*, disassembly of `0x3d000`–`0x43000`): the ENet code contains the checksum and compressor
paths, but the client never enables them.
- `ENetHost` fields `+0xa90` (checksum callback) and `+0xa98..+0xab0` (compressor context/compress/
  decompress/destroy) are zeroed in the inlined `enet_host_create` (`0x3d9e1`..`0x3da5b`). There is no
  other store to `host+0xa90` in the whole file (disassembly has 6 references to `[reg+0xa90]` in
  total: the one store in create, and loads at `0x3e41f` (`enet_peer_send`'s fragment-size
  computation: `-0x20` or `-0x1c` depending on `host->checksum == NULL`), `0x408ea`, `0x409f8`,
  `0x40a41` (receive path: header size +4 and the checksum call `call rax` at `0x40a48`)). The
  compressor call is guarded by `test rcx, rcx / je` and `test r10, r10 / je` (`0x40975`, `0x40985`).
- No CRC-32 table (`0xedb88320`) and no range-coder constants appear in the image. So: **no ENet
  checksum and no compression**; a server must not enable either (if it did, packets would carry a
  4-byte checksum field the client would not expect: the receive path adds 4 header bytes when
  `host+0xa90 != 0`, `0x408ea`-`0x408f6`).
- The "peer id/header" size check `shr r8w,0xc; and r8b,3` at `0x408d9` is ENet's `sessionID` in the
  protocol header, i.e. the standard ENet 1.3 header layout (`peerID` 12 bits, `sessionID` 2 bits,
  compressed flag bit 14, sentTime flag bit 15; `bt r13w,0xe` at `0x40962` tests the compressed
  flag). Standard ENet 1.3.x wire format, so a stock ENet 1.3.x server (or enet-rs / enet.js
  compatible) is expected to interoperate (*inferred*).
- Version: standard ENet 1.3.x command numbering and sizes: the `commandSizes` table
  `{0,8,48,44,8,4,6,8,24,8,12,16,24}` (8-byte entries) is at file offset `0x71270` (*measured*).
  The exact 1.3.x release is not pinned (no version string; `enet_linked_version` is not exported).
  That a stock ENet 1.3.x peer interoperates follows from this and the header layout (*inferred*,
  untested).

Connecting (*measured*): `AnarchyClientConnect(client, host, port, name, password)` (`0x14b60`)
stores `name` (default `"player"`) and `password`, clears the handoff state and calls `0x14c60`.
That creates the host, applies the lag settings, starts the network thread, resolves `host` with
`gethostbyname` (fallback parser `0x42290` for numeric addresses) and calls `enet_host_connect`.
The port is the caller's (the game cfg uses 27800; no default port in the core).

Network thread (`0x383e0` → `0x3cf10` → `0x385c0`, *measured*): on unless
`ANARCHY_NETTHREAD=0`. Loop: wait on the socket up to 2 ms (`0x42c70`), flush the delayed-send
queue, run `enet_host_service(host, &ev, 0)` (`0x41fb0`) up to 256 times, and queue
connect (1) / disconnect (2) / receive (3) events for `AnarchyClientUpdate`.

Sending (`0x3a2a0` → `0x3a6d0`, *measured*):

- Payload 1–65536 bytes; channel 0–4 (anything above 4 becomes 0).
- Reliable → `ENET_PACKET_FLAG_RELIABLE`. Unreliable → flag 0, or `UNRELIABLE_FRAGMENT` (8) when a
  global (`0x81b21`, set by the host-only `AnarchyHostSetLossyFragments`) is on.
- Per-message-type byte counters are indexed by the **first payload byte**, which is the
  message type. Unreliable packets over `0x554` (1364) bytes are counted as "sent unreliable over
  one ENet fragment".
- `AnarchyClientSetLag(latency, jitter, loss%)` (`0x3a1e0`, loss capped at 100) routes sends
  through a delay queue; a test feature.

Channels used by the client (*measured* from the send calls):

| Channel | Mode | Messages |
|---|---|---|
| 0 | reliable | almost everything: Hello, auth, Chat, orders, notices, Spine intents ≤1200 B |
| 1 | reliable | BagLayout `0x39`, BagResync `0x38` |
| 2 | unreliable | Position `0xb`, `SpineSendIntentLossy` |
| 3 | reliable | Spine intents >1200 B when `AnarchyClientSetLanes(c, 1)` was called |
| 4 | — | not used by the client in the code read |

Which channel the server uses for each server message is not visible in the client.

Wine: strings show a Winsock workaround (`ANARCHY_WINESOCK`, `wine_server_handle_to_fd`, a raw
syscall stub page and per-thread probe). Not analysed (*documented by strings only*).

## 2. Framing

Every ENet packet carries exactly one message (*measured*: dispatcher `0xe340` switches on byte
0; every parser checks its own type byte and the 65536-byte limit):

- byte 0: message type (`u8`).
- All integers and floats are **little-endian, unaligned**, written with plain byte copies.
- `str16` = `u16` byte length + bytes, no terminator; each field has its own maximum.
- Parsers require the payload to be consumed **exactly**; trailing bytes make it "malformed".
- Handled server→client types: exactly the IDs in the server→client table below. The dispatcher
  `0xe340` has two stages: a pre-filter for `0x01`–`0x3e` and `0x40`–`0x45` (`0x3f` rejected), then
  a jump table at `0x13124` that sends the client→server IDs and unused IDs to the "unknown type"
  default (*measured*).

### Message IDs

Server→client (*measured*: dispatcher `0xe340` case → parser; names from the "malformed X"
strings or the effect):

| ID | Name | Parser | | ID | Name | Parser |
|---|---|---|---|---|---|---|
| `02` | Welcome | `0x2bf70` | | `22` | Behaviour | `0x32180` |
| `03` | Reject | `0x2c1f0` | | `24` | Players | `0x33cd0` |
| `04` | Chat | `0x2c790` | | `25` | UseStates | `0x306a0` |
| `05` | Snapshot | `0x2a0d0` | | `27` | OwnSkills | `0x2d160` |
| `07` | Intents | `0x2a800` | | `28` | Healings | `0x300d0` |
| `08` | Spawns | `0x2abb0` | | `29` | WorldClock | `0x2fff0` |
| `0a` | YourCharacter (name *inferred*) | `0x2b7d0` | | `2a` | Chained | `0x2dd20` |
| `0c` | Appearance | `0x2b5e0` | | `2b` | Owners | `0x2dee0` |
| `0d` | Despawns | `0x2c980` | | `2c` | Speech | `0x2eaf0` |
| `0e` | Vitals | `0x31280` | | `2d` | DoorStates | `0x2efb0` |
| `10` | Census | `0x31e00` | | `2f` | Notice | `0x2e120` |
| `13` | Engage | `0x32930` | | `30` | Recall | `0x2e320` |
| `14` | Hits | `0x32c10` | | `31` | Rules | `0x2e900` |
| `15` | CombatStates | `0x330e0` | | `33` | Structures | `0x2e4d0` |
| `16` | CombatAnims | `0x334e0` | | `34` | Crates | `0x2f2c0` |
| `17` | Kit | `0x2cb40` | | `37` | ItemMoveResult | `0x2fef0` |
| `1a` | Carries | `0x2da30` | | `3a` | Actions | `0x30340` |
| `1c` | Loot | `0x30a10` | | `3b` | Challenge | `0x2bb90` |
| `20` | Shots | `0x32e90` | | `3d` | Spine (server→client) | `0x2d350` |
| `21` | Squad | `0x31fc0` | | `40` | SpeedVotes | `0x2d7e0` |
| | | | | `41` | Builds | `0x2d650` |
| | | | | `43` | Redirect | `0x2c3f0` |
| | | | | `45` | LeadOrder | `0x33a80` |

Client→server (*measured*: type byte written in the export or its encoder):

| ID | Name | Export / encoder | Channel |
|---|---|---|---|
| `01` | Hello | `AnarchyClientUpdate` `0x15740` → `0x29d00` | 0 |
| `04` | Chat | `AnarchyClientSendChat` `0x15d50` | 0 |
| `0b` | Position | `AnarchyClientSendPosition` `0x15200` → `0x2b890` | 2, unreliable |
| `11` | Order | `AnarchyClientSendOrder` `0x1bad0` | 0 |
| `12` | AttackOrder | `0x1bc50` | 0 |
| `18` | Bag | `0x1bda0` (blob ≤ `0xc000`) | 0 |
| `19` | MedicOrder | `0x1bf30` | 0 |
| `1b` | CarryOrder | `0x176e0` | 0 |
| `1d` | DropNotice | `0x17ec0` | 0 |
| `1e` | PickupNotice | `0x17db0` | 0 |
| `1f` | StashNotice | `0x180e0` | 0 |
| `26` | UseReport | `0x17970` | 0 |
| `27` | SkillGains | `0x1ac40` (blob ≤ `0x4000`) | 0 |
| `2e` | DoorReport | `0x1ab70` → `0x2ecf0` | 0 |
| `32` | RobNotice | `0x18320` | 0 |
| `38` | BagResync | `0x1a8d0` | 1 |
| `39` | BagLayout | `0x1a9b0` (blob ≤ `0xc000`) | 1 |
| `3c` | AuthProof | dispatcher case `0x3b` → `0x29eb0` | 0 |
| `3e` | SpineIntent | `0x1c250` / `0x1c390` → `0x2d550` | 0 / 3 / 2 |
| `42` | BuildIntent | `0x19260` (blob ≤ `0x400`) | 0 |
| `44` | HandoffHello | `AnarchyClientUpdate` `0x15740` | 0 |

### Client→server: remaining IDs and full layouts

Common to all `AnarchyClientSend*` (measured): refuse (return 0) unless client pointer valid and
`client+0x1c == 2` (connected) and `client+0x18 != 0` (peer id). Sent through `0x3a2a0(host, peer,
packet, channel, reliable)`; all reliable. "ch" = channel.

#### New IDs

| ID | Name | Export | Encoder | ch |
|---|---|---|---|---|
| `06` | RequestSpeed | `0x15ed0` | inline | 0 |
| `09` | Character (SendCharacter) | `0x16440` | `0x2b3e0` | 0 |
| `0f` | Resync (RequestResync) | `0x1b910` | inline | 0 |
| `23` | BehaviourOrder | `0x1b490` | `0x326f0` | 0 |
| `35` | CrateStash | `0x19b00` | `0x2f8b0` | 0 |
| `36` | ItemMove | `0x19c60` | `0x2fbe0` (+ Place encoder `0x2fa70`) | 1 |

`06 09 0f 23 35 36` are free in the **server->client** dispatcher (`0xe340`), they are only used
client->server. IDs share one numbering per direction: `04` Chat and `27` (SkillGains c->s,
OwnSkills s->c) occur in both directions with different layouts.

**Leave** (`0x15660`) and **Ping** (`0x1b460`) send no application message (*measured*):
- Leave: if connected, `0x3ac80` does an ENet-style graceful disconnect of the peer (`0x3aaa0`: flush
  queued commands, then peer disconnect), waits for the ACK (event loop `0x39510`), then
  `0x3af70` flushes the host. Returns elapsed ms, or `0xffffffff` if the peer was already gone.
  Not connected: `0x3aaa0` only. State -> 0.
- Ping returns `u32` read from the peer object (`peer+0xc8`; ENet `roundTripTime`, *inferred*),
  looked up by peer id in the host's hash map (`0x3b130`; with the net thread on, via `0x3b010`).

#### Layouts

`f32` fields are shown by the sign of use: every float passes the finite check unless noted.

```
RequestSpeed   06   u8 06, f32 speed                                         (0x15ed0)
Character      09   u8 09, str16 (<=0x20), str16 (<=0x200), str16 (<=0x200), f32,
                    str16 (<=0x4000)                                         (enc 0x2b3e0)
                    export args: (client, name, s2, s3, f32 v, blob, blobLen); NULL s2/s3 -> "";
                    f32 default 25.0 (0x41c80000); blob only if len>0.
                    Names inferred: character name, 2 texts, scale/height?, appearance blob.
Resync         0f   u8 0f, u16 n (<=0x400), n x u32 entityId                 (0x1b910)
                    export (client, const u32* ids, count); count clipped to 0x400; ids==NULL -> n=0.
Order          11   u8 11, u32 entity (!=0), f32 x, f32 y, f32 z             (0x1bad0)  move order, 17 B
AttackOrder    12   u8 12, u32 attacker (!=0), u32 target (!=0, != attacker), u8 mode (clamped 0..255)
                    (0x1bc50)
Bag            18   u8 18, u32 id (!=0), str16 blob (1..0xc000)              (0x1bda0)
MedicOrder     19   u8 19, u32 medic (!=0), u32 patient, u8 (bool: arg4!=0), u8 mode (<=2)
                    refused if arg4==0 and patient==0                        (0x1bf30)
CarryOrder     1b   u8 1b, u32 carrier (!=0), u32 target, u8 flag (bool), u8 slot,
                    f32 x, f32 y, f32 z                                      (0x176e0)
                    flag==0: target must be !=0 and != carrier, slot forced 0;
                    flag!=0: slot clamped to 0..255 (>0xff -> 0); 19+... = 1+4+4+1+1+12 = 23 B
DropNotice     1d   u8 1d, u32 id (!=0), str16 itemName (<=0x60, non-empty), u16 count (0->1),
                    u8 slot (default 0xff)                                   (0x17ec0)
PickupNotice   1e   u8 1e, u32 a (!=0), u32 b (!=0)                          (0x17db0)
StashNotice    1f   u8 1f, u32 a (!=0), u32 b (!=0), str16 itemName (<=0x60, non-empty),
                    u16 count (0->1), u8 slot (default 0xff)                 (0x180e0)
UseReport      26   u8 26, u32 id (!=0), u8 kind (<=4), f32 x, f32 y, f32 z, u8 flag
                                                                             (0x17970)  18 B
SkillGains     27   u8 27, u32 id, str16 blob (export accepts 1..0x400 B; encoder cap 0x4000)
                                                                             (0x1ac40)
DoorReport     2e   u8 2e, f32 x, f32 y, f32 z, u8 state                     (0x1ab70 -> 0x2ecf0)  14 B
                    export is (client, f32, f32, f32, u8): floats arrive in XMM1-3 (disassembly
                    `0x1abbc..0x1abd7`), Ghidra shows them as `uint`.
RobNotice      32   u8 32, u32 a (!=0), u32 b (!=0), str16 itemName (<=0x60, non-empty),
                    u16 count (0->1)                                         (0x18320)
CrateStash     35   u8 35, u32 crate, f32 x, f32 y, f32 z, str16 itemName (<=0x60), u16 count,
                    u8 slot                                                  (enc 0x2f8b0)
                    export (client, crate, f32,f32,f32, const char* name, count, slot); count
                    clipped to 0xffff (default 1); slot clipped to 0xff; name must be non-NULL.
ItemMove       36   u8 36, u32 A (!=0), u32 B, Place from, Place to,
                    str16 s1 (<=0x60), str16 s2, str16 s3, str16 s4  (s1 non-empty),
                    u8 b0, u8 b1, u8 b2, u16 count (0->1), [str16 tail (<=0x60), only present
                    if non-empty]                                            (enc 0x2fbe0)
                    Place = u8 kind (0..3), u32 ref, f32 x, f32 y, f32 z, str16 name (<=0x60),
                    u8 a, u8 b                                               (0x2fa70)
                    The parser on the server must accept a message that ends after `count`.
                    Sent on channel **1**. Field meanings *inferred* (move item between
                    inventory / ground / crate / character).
BagLayout      39   u8 39, u32 a (!=0), u32 b (!=0), str16 blob (1..0xc000)  (0x1a9b0), ch 1
BagResync      38   u8 38, u32 id (!=0)                                      (0x1a8d0), ch 1
BuildIntent    42   u8 42, str16 blob (1..0x400)                             (0x19260)
BehaviourOrder 23   u8 23, u32 entity (!=0), u8 a, u8 b, u8 c, u16 d, u32 e, u8 f,
                    f32 x1..x6 (finite)                                      (enc 0x326f0)  39 B
                    The record after `entity,a,b,c` is the same job record that Behaviour `22`
                    carries (`u16, u32, u8, 6 x f32`).
```

#### AnarchyItemMoveWire (measured, `0x19c60`)

`AnarchyClientSendItemMove(client, const AnarchyItemMoveWire*, ?, ?)` requires `wire->size == 0x2f4`
(756 bytes) as the first `u32`; otherwise it builds the log text "AnarchyClientSendItemMove: the
caller's AnarchyItemMoveWire is N bytes, ours 756" and returns 0. Requires `wire+4 != 0` and
`byte wire+0xac != 0`. Offsets (copied into the encoder's struct):

| Off | Field |
|---|---|
| `+0x00` | u32 size = `0x2f4` |
| `+0x04` | u32 A (message u32 #1) |
| `+0x08` | u32 B |
| `+0x0c` | u32 Place(from).kind (>3 -> 0) |
| `+0x10` | u32 Place(from).ref |
| `+0x14..0x1c` | f32 x, y, z of from |
| `+0x20` | u32 Place(to).kind (>3 -> 0) |
| `+0x24` | u32 Place(to).ref |
| `+0x28..0x30` | f32 x, y, z of to |
| `+0x34`, `+0x38` | u32 -> u8 (to.a, to.b), clamped 0xff |
| `+0x3c`, `+0x40`, `+0x44` | u32 -> u8 b0, b1, b2 (clamped 0xff) |
| `+0x48` | u32 count (0 -> 1, >0xffff -> 0xffff) |
| `+0x4c` | char[0x60] to.name |
| `+0xac`, `+0x10c`, `+0x16c`, `+0x1cc` | char[0x60] s1..s4 |
| `+0x22c` | char[0x60] from.name |
| `+0x28c`, `+0x290` | u32 -> u8 (from.a, from.b), clamped 0xff |
| `+0x294` | char[0x60] tail string |

(All string buffers are scanned with a `strlen` capped at `0x60`.)

### Login, chat, position, snapshot and Spine layouts (*measured* unless marked)

**Hello `01`** (`0x29d00`): `u8 01`, `u16 protocol = 0x85`, `str16 name` (≤32), `u8 n` (≤8),
`n × str16 fingerprint` (≤18 each, section 4). Sent on connect when no handoff ticket applies.

**HandoffHello `44`** (`0x15740`): `u8 44`, `u16 protocol = 0x85`, `str16 name` (≤32),
`str16 ticket` (≤ `0xc000`). Sent on connect instead of Hello when the address equals the one
the last Redirect named.

**Challenge `3b`** (`0x2bb90`): `u8 3b`, `u8 mode`, `str16 salt` (≤160), `u32 rounds`,
`str16 newSalt` (≤160), `u32 newRounds`, `str16 serverNonce` (≤160), `u8 needServerPassword`.

**AuthProof `3c`** (`0x29eb0`): `u8 3c` + five `str16` (≤160 each): clientNonce, clientProof,
serverPasswordProof, newStoredKey, newServerKey (empty when not used). Section 3.

**Welcome `02`** (`0x2bf70`): `u8 02`, `u32 playerId`, `str16 serverName` (≤64), `u16 tick`
(only formatted into the log line ", tick N" at `0xeb6d`, never stored: *measured*; what the server
puts there is *unknown*), `u32 mainCharacterEntity` (0 = none),
`u8 characterOnFileBeingRebuilt`.

**Reject `03`** (`0x2c1f0`): `u8 03`, `u8 reason`, `str16 text` (≤512). Reasons (`0x29c40`):
1 ProtocolMismatch, 2 ServerFull, 3 BadName, 4 Banned, 5 WrongPassword, 6 WrongServerPassword,
7 TooManyAttempts, 8 BadNewAccount, 9 SignedInElsewhere, 10 StorageUnavailable,
11 HandoffRefused, 12 ModsDiffer, other Unknown.

**Redirect `43`** (`0x2c3f0`): `u8 43`, `u8 kind` (0 "your character is there", 1 "crossing
over", 2 "signing in at the gate"), `str16 host` (≤253, only `[A-Za-z0-9.:-]`), `u16 port`
(≠0), `str16 region` (≤32), `str16 ticket` (≤ `0xc000`), `u32` (parsed to struct+0x70 at `0x2c750`,
then never read by the dispatcher or any export: the client ignores it, *measured*; meaning
*unknown*). At most 5
redirects per 60 s; a ticket without a 32-byte region key is refused. The client then
disconnects and reconnects to `host:port` on the next `AnarchyClientUpdate`.

**Chat `04`** (both directions, `0x15d50` / `0x2c790`): `u8 04`, `u32 playerId` (the sender;
the client fills in its own id from Welcome; 0 = server), `str16 text` (≤256). Server text
starting with `"[Admin] "` from id 0 is shown as admin.

**Position `0b`** (`0x2b890`): `u8 0b`, `f32 × 6`, `u8 bool`, `f32 × 6`, `u8`, `u8` (52 bytes).
Non-finite floats become 0; the export refuses to send if any of the 12 is non-finite. Core passes
the arguments straight through (order *measured* from the AK call `AK 0x2f65bf` via wrapper
`AK 0x5b400`, which also normalises the bool). Meanings from AK: client sources *measured*
(sender `AK 0x2f5a30`); "server use" is AK host-mode code (`AK 0x3d2660`) reading
`TakePlayerMove` out-parameters, assumed to be in wire order (*inferred*: the host export is a stub;
the slot types fit, e.g. the bool in slot 7 is tested at `AK 0x3d294a`); names *inferred*:

| # | Field | Client source | Server use |
|---|---|---|---|
| 1–3 | position x, y, z | `Character_positionDirect` (site table A 138) | per-player position map `AK 0x195fc40` |
| 4–6 | destination x, y, z | copy of position, overwritten from movement (`char+0x640`) `+0xe8`; reset to position in furniture or inside a zone (`AK 0x567fe0`), the reset only when config byte `AK 0x15a69a5` is set | stored as the move intent (`AK 0x3dd8f0`) when 7 is set and bit `0x04` clear |
| 7 | walking (bool) | xz distance² position→destination > 4.0 (`AK 0x2f6048`) | keeps or clears the move intent |
| 8 | wanted speed | movement `+0xb8` | if > 0.1: `AbstractMovementBase_wantSpeed` with min(v, top speed) (`AK 0x3d4064`) |
| 9–11 | motion vector (velocity) | `AbstractMovementBase_motionNow` (A 12) | stored in map `AK 0x195f860` (further use not traced) |
| 12–13 | facing x, z | the orientation's rotated local **+Z** axis `(2(wy+xz), 1−2(x²+y²))` (`AK 0x2f6110`–`0x2f6195`; not Ogre's −Z `getDirection`), from `RootObject_orientationDirect` (A 322), normalised in xz, default (1, 0) | stored as (x, 0, z) in `AK 0x195ff68` |
| 14 | state bits | built at `AK 0x2f6481`–`0x2f6527`, see below | see below |
| 15 | floor | max(0, `Character+0xa4`) | setter `AK 0x2c4340` if changed; feeds the snapshot floor |

State bits (field 14; client sources *measured*, names *inferred*): `0x01` sneaking
(`Character_isSneaking`); `0x02` shackled (`Character_isShackled`); `0x04` the server leads this
body (globals `AK 0x1945ec0`/`0x1945ec1`, log "slave: the server leads our own body now"; which
global is which mode is not traced; the server then skips the move intent); `0x08` pose upright
(Character vtable `+0x368`, pose names `AK 0x1201678`: upright, low, crawling, playing dead, KO);
`0x10` standing idle (`0x08`, not walking, globals `AK 0x1945ec0/ec1/ee9` clear, callee
`AK 0x2d6af0` false; "no AI task, not carrying" is *inferred*); `0x20` down and cannot rise
(playing dead/KO, ragdoll, and the original `Character_safeToRise` called at `AK 0xc00fb0` returns
false; only when config byte `AK 0x15a695b` is set, tested by client `AK 0x2c1f40` and server
`AK 0x3d2cc1`; the server puts the copy in a 128-slot table `AK 0xc00fe0` whose slots expire after
12000 ms). Server-side use is seen only for `0x04`, `0x10` (stored at per-player record +0x38)
and `0x20`; `0x01`, `0x02`, `0x08` are named from the client side only.
`Character+0xa4` (floor), movement `+0xe8`/`+0xb8` have no Kenshi name yet. The OgreMain import
in the sender (`AK` IAT `0xeb0278`) is ordinal 1839 = `Ogre::Vector3::operator=` (resolved against
the Steam OgreMain export table): a plain copy.

**Snapshot `05`** (`0x2a0d0`):

```
u8  05
u32 tick
u32 gameTimeMs        older snapshots are dropped unless >10 s behind (clock reset)
f32 speed             finite
u8  voters            -> AnarchyClientSnapshotVoters
u8  players           -> AnarchyClientSnapshotPlayers
f32 originX, originY, originZ
u16 count             <= 1024
count x entity:
  u32 id
  i16 dx              if dx == -32768: f32 x, f32 y, f32 z absolute follow
  i16 dy, i16 dz      else position = origin + d / 8   (0.125 units)
  i16 vx, vy, vz      velocity = v / 64
  u16 dir             low 13 bits: angle = n * 2*pi / 8190 (0x1fff = none),
                      high 3 bits: floor index 0..7 (decoded `dir >> 13` at 0x2a6de)
```

Decoded entity is 40 bytes (`id, x, y, z, vx, vy, vz, dirA, dirB, floor`); `dirA`/`dirB` are
the facing vector's x and z: the AK client stores `dirA` at entity record +0x170 and `dirB` at
+0x178 with +0x174 = 0 (`AK 0x4fe4c4`–`0x4fe4d3`, *measured*), and the server record
(`AK 0x429af0`, 0x2c B each) holds facing x at +0x1c and z at +0x24. `0x5ee10` is sin and
`0x5f390` cos (*inferred* from their small-argument paths), so an encoder must send
θ = atan2(x, z), n = round(θ·8190/2π) mod 8190, `0x1fff` = no facing (decode at `0x2a6c0`–`0x2a739`).

The high 3 bits are a **building floor** (both ends *measured*, the server→wire hop *inferred*
since the encoder is in the stubbed Core; the name *inferred* from logs
`" floor="`, `" building="`, "a body indoors carries its floor with it"). Server: record +0x28 =
the player's floor from Position field 15 (map `AK 0x195f768`), else `Character+0xa4` via
`AK 0x2c4320`, clamped ≥ 0. Client: `AK 0x4fdd50` keeps it per entity (value +0x228, i.e. map node +0x248); `AK 0x502241`
copies it into the local body's `Character+0xa4` (setter `AK 0x4fb800`, behind a config byte);
for a remote body whose floor is above 0, `AK 0x502f90` returns 0 without a walk
and only records a place target (`AK 0x4f9440`, Kenshi-free bookkeeping); the Kenshi place call is `AK 0x4fba20` ([remote-bodies.md](remote-bodies.md)). How floors ≥ 8 are encoded is *unknown* (the server encoder is in the stubbed
Core).

**Spine** (opaque channel for AnarchyKenshi.dll): client→server `3e` (`0x2d550`): `u8 3e`,
`u16 kind`, `str16 blob` (≤ `0x2000`; the export refuses >`0x2000` or kind ≥ `0x10000`).
Server→client `3d` (`0x2d350`): `u8 3d`, `u16 kind` (*inferred* from the shared struct),
`str16 blob` (≤ `0x8000`); up to `0x4000` queued for `AnarchyClientSpineTake`.

### Server→client layouts

Common frame (measured, every parser): `u8 id`, whole message <= `0x10000`; payload must be
consumed exactly. "head" = a `u32` stored as the first field of the parser's output struct; the
client discards it: dispatcher `0xe340` copies only the entry vector of each batch into the client
object (e.g. Engage at `0x1125d`–`0x11270`), no export returns it, and the `...Fresh` exports only
read a "batch arrived" byte (*measured*). In host mode AK passes the game clock in ms
(`(u32)` of the double at `AK 0x1945d90`, advanced at `AK 0x429e22` by tick period × 1000 × speed;
the period at `AK 0x15a69b0` is 1/configured rate, default 0.1) to 17 of the 21
`AnarchyHostBegin<X>` call sites (e.g. `AK 0x38e6f0`, `0x2b3211`, `0x42dee4`). Exceptions:
`BeginDespawns`/`BeginCensus` get no clock (their messages have no head); `BeginStructures`,
`BeginDoors`, `BeginCrates` are never called; `BeginKits` gets the clock although Kit `17` has no
head (unexplained).
`BeginSnapshot(host, tick, clock, speed)` at `AK 0x42e5ae` and `BeginAnims(host, clock,
fingerprint)` match their wire order, so head = the server's game time in ms when the batch was
built, the same for all batch types and equal to Snapshot `gameTimeMs` of that tick (*inferred*:
Core's host builders are stubs). Since the client ignores it, any value works for our server. A header of
`u32 head, u16 count` is read as `size >= 6` else the parser returns false.

```
Welcome 02, Reject 03, Chat 04, Redirect 43, Challenge 3b, Snapshot 05, Spine 3d: see above

Intents        07   u32 head, u16 n (<=0x400), n x { u32 id, f32 a, f32 b, f32 c, f32 d, u8 flag }
                    (21 B each)                                              (0x2a800)
Spawns         08   u32 head, u16 n (<=0x20), n x { u32 id, str16 s1 (<=0x200),
                    f32 a, f32 b, f32 c, f32 d (default 25.0), str16 s2, str16 s3, str16 s4
                    (each <=0x200) }                                         (0x2abb0)
                    entry struct 0x98 B; a,b,c are probably x,y,z (*inferred*).
YourCharacter  0a   u32 entity, u8 flag                                      (0x2b7d0)
                    name inferred; Welcome's mainCharacterEntity may be the same thing.
Appearance     0c   u32 entity, str16 blob (<=0x4000)                        (0x2b5e0)
Despawns       0d   u16 n (<=0x400), n x u32 id                              (0x2c980)
Vitals         0e   u32 head, u16 n (<=0x400), n x {
                      u32 id, u16 a, u8 b, u8 c, u8 d, u8 e, u16 f, f32 g (finite, default 1.0),
                      u16 h (default 10000),
                      u8 n1 (<=0x10), n1 x u8, u8 n2 (<=0x10), n2 x u8,
                      u8 n3 (<=0x10), n3 x u8, u8 n4 (<=0x10), n4 x u16 }    (0x31280)
                    entry struct 0x80 B. `AnarchyClientVitalsAt` unpacks them into 20 out
                    params and bit-packs a,b,c,d,f into one value (`0x16f10`); names unknown.
Census         10   u16 n (<=0x400), n x u32 id                              (0x31e00)
Engage         13   u32 head, **u32** n (<=0x400), n x { u32, u32 }          (0x32930)
Hits           14   u32 head, **u32** n (<=0x400), n x { u32, u32, u16, u16, u8, u8 }
                                                                             (0x32c10)
CombatStates   15   u32 head, **u32** n (<=0x400), n x { u32 id, u8, u8, u16, u16, u16, u16,
                    u8, u8 } (16 B each)                                     (0x330e0)
CombatAnims    16   u32 head, u32 fingerprint (-> AnarchyClientAnimsFingerprint), u32 n (<=0x400),
                    n x { u32 id, u8, u8, u8, str16 (<=0x40), str16 (<=0x20) }  (0x334e0)
Kit            17   u32 entity, u16 dictBase, u16 nw (<=0x4000), nw x str16 (<=0x80) [dictionary
                    words], u16 ni (<=0x20), ni x { u32 id, u16 x, str16 blob (<=0x4000) }
                                                                             (0x2cb40)
                    Dictionary (dispatcher `0xe340` case `17`, *measured*): the client keeps a
                    word table (max 0x4000 words, `client+0x878`, `AnarchyClientDictSize`);
                    `dictBase` is the table index of the message's first word; words below the
                    client's current size are skipped; `dictBase > size` leaves a gap
                    (`AnarchyClientDictGaps`, client+0x890). Overflow log: "kit dictionary is
                    full at N words". The ni records are queued (<=0x400) for
                    `AnarchyClientTakeKit` ("kit queue is full - dropped", "... outfit(s); that
                    body keeps its own dice roll"); a record's blob/ x meaning is *inferred*
                    (outfit/appearance data compressed with the dictionary).
Carries        1a   u32 head, u16 n (<=0x400), n x { u32, u32 }              (0x2da30)
Loot           1c   u32 head, u16 n (<=0x60), n x { u32 id, str16 (<=0x60), u16, u8, u8,
                    str16 (<=0x60), str16 (<=0x60), f32 x, f32 y, f32 z },
                    u16 g (<=0x60), g x u32 id   [ids that are gone]         (0x30a10)
Shots          20   u32 head, **u32** n (<=0x400), n x { u32, u32, u8 }      (0x32e90)
Squad          21   u16 n (<=0x400), n x u32 id                              (0x31fc0)
Behaviour      22   u32 head, u16 n (<=0x400), n x { u32 id, u8 a, u8 b, u8 nj (<=0x18),
                    nj x { u16, u32, u8, f32 x6 } }  (job = 31 B)            (0x32180)
Players        24   u16 n (<=0x400), n x { u32 playerId, u32 characterEntity, str16 name (<=0x20),
                    u8 flags (admin, *inferred*), u16 ping }                 (0x33cd0)
UseStates      25   u32 head, u16 n (<=0x400), n x { u32 id, u8 kind, f32 x, f32 y, f32 z, u8 }
                    (18 B each)                                              (0x306a0)
OwnSkills      27   u32 entity, str16 blob (<=0x4000)                        (0x2d160)
Healings       28   u32 head, u16 n (<=0x400), n x { u32, u32, u8 bool }     (0x300d0)
WorldClock     29   f32 value (finite, else the message is rejected)        (0x2fff0)
                    5 B message; `AnarchyClientClock(client, float*)` returns it (0x17470).
Chained        2a   u16 n (<=0x400), n x u32 id                              (0x2dd20)
Owners         2b   u16 n (<=0x400), n x { u32, u32 }                        (0x2dee0)
Speech         2c   u32 entity (!=0), u8 kind, str16 text (<=0x100)          (0x2eaf0)
DoorStates     2d   u32 head, u16 n (<=0x400), n x { f32 x, f32 y, f32 z, u8 state }
                    (13 B each, exact size check)                            (0x2efb0 / 0x2edd0)
Notice         2f   u8 kind, u16 x, str16 text (<=0x100)                     (0x2e120)
Recall         30   f32 x, f32 y, f32 z (all finite), u16                    (0x2e320)
Rules          31   u32 version?, str16 text (<=0x100)                       (0x2e900)
                    `AnarchyClientRules(client, u32* out, char* buf, int cap)` returns both.
Structures     33   u16 n (<=0x3e8), n x { str16 (<=0x40), f32, f32, f32, i16 }  (0x2e4d0)
Crates         34   u32 head, u16 n (<=0x80), n x { f32 x, f32 y, f32 z, u16 ni (<=0x40),
                    ni x { str16 item (<=0x60), u16 count (default 1), u8 slot (default 0xff) } }
                                                                             (0x2f2c0)
ItemMoveResult 37   u32 id (!=0), u8 a, u8 b                                 (0x2fef0)
Actions        3a   u32 head, u16 n (<=0x400), n x { u32 id, str16 name (<=0x64) }  (0x30340)
SpeedVotes     40   u16 n (<=0x400), n x { u32 playerId, f32 speed (non-finite rejects the
                    message) }                                               (0x2d7e0)
Builds         41   str16 blob (<=0xf000)                                    (0x2d650)
LeadOrder      45   u32 entity (!=0), u8 kind, u32, f32 x, f32 y, f32 z (finite)   (0x33a80)
                    exactly 22 B.
```

Notes:
- `n` counts are `u16` everywhere except Engage `13`, Hits `14`, CombatStates `15`, CombatAnims
  `16`, Shots `20`, which use `u32` (measured: the parser reads `*(uint*)(msg+5)`).
- Snapshot `05` (above) already lists `u16 count <= 1024`; Intents `07` repeats the
  `u32 head, u16 count` shape.
- Which channel/mode the server uses for each message is not visible from the client.

#### Spawn/Kit/Vitals field meanings that the client exports expose (*measured*, names inferred)

- `AnarchyClientSpawnAt(client, i, u32* id, char* s1,int, f32* a, f32* b, f32* c, f32* d, char* s2,int,
  char* s3,int, char* s4,int)` copies the Spawns entry 1:1.
- `AnarchyClientPlayerAt(client, i, u32* playerId, u32* entity, char* name, int cap)`;
  `PlayerIsAdmin(client,i)`, `PlayerPing(client,i)`, `PlayersVersion` (bumps on each Players message),
  `AmAdmin(client)`.

## 3. Login handshake

Flow (*measured*: `0x15740`, dispatcher cases `0x3b`, `2`, `3`, `0x43`):

1. ENet connect. Client sends Hello (or HandoffHello after a Redirect).
2. Server sends Challenge `3b`, or Reject `03`.
3. Client answers AuthProof `3c`, or disconnects with a local refusal ("Nothing was sent").
4. Server sends Welcome `02` (state 2 = connected) or Reject `03`.

Challenge modes:

| mode | Meaning (*inferred* from the client's reaction) | Client checks |
|---|---|---|
| 0 | create a new account | refused if this connection followed a Redirect; `newRounds` ≥ 20000 |
| 1 | sign in | `rounds` ≥ 20000; `salt` length ≥ 16 |
| 2 | old fast password check | always refused |
| 3 | region sign-in with a handoff ticket | needs a ticket and a 32-byte region key |

Crypto (*measured*; SHA-256 recognised by its IV and K constants, HMAC by the `0x36`/`0x5c`
pads in `0xc8e0`):

- `clientNonce` = 32 lowercase hex chars of 16 random bytes (`0xc800`; bytes are SHA-256 of
  `std::random_device`, time, a counter and a stack address, `0xc4a0`).
- `AuthMessage` = `serverNonce + ":" + clientNonce + ":" + lowercase(name)` (`0xd0e0`, ASCII
  A–Z lowered only, `0xda40`).
- `SaltedPassword` = PBKDF2-HMAC-SHA256(password, salt bytes as sent, rounds, 32 bytes)
  (`0xccc0`; rounds clamped to 1…1 000 000).
- Mode 1 (`0xd2c0`): `ClientKey = HMAC(SaltedPassword, "Client Key")`,
  `StoredKey = SHA256(ClientKey)`, `ClientSignature = HMAC(StoredKey, AuthMessage)`,
  `clientProof = hex(ClientKey XOR ClientSignature)` (64 chars). This is SCRAM-SHA-256 without
  the GS2 header. The client does **not** verify a server signature (*inferred*: "Server Key"
  is only used in the next point).
- If `newSalt` is non-empty (modes 0 and 1; `0xce50`): with `SP2 = PBKDF2(password, newSalt,
  newRounds)`, `newStoredKey = hex(SHA256(HMAC(SP2, "Client Key")))`,
  `newServerKey = hex(HMAC(SP2, "Server Key"))`. The server stores these; the password never
  leaves the client.
- If `needServerPassword` (`0xd830`): `serverPasswordProof =
  hex(HMAC(key = hex(SHA256(serverPassword)) as 64 ASCII chars, AuthMessage))`.
- Region key: mode 1 sets `regionKey = HMAC(ClientKey, "anarchy-handoff:" + AuthMessage)`
  (32 raw bytes). Mode 3 sends `clientProof = hex(HMAC(regionKey, AuthMessage))` (`0xd760`);
  after the Welcome the key rotates to `HMAC(regionKey, "anarchy-handoff-next:" +
  AuthMessage)` (`0xd5e0`).
- Passwords are cleared from the client object after the proof is sent.

Account name and password come from `AnarchyClientConnect`; the server password from
`AnarchyClientSetServerPassword` (`0x1c650`). Mods are not sent in Hello (reject reason 12
ModsDiffer exists, so mods are compared another way; *open*).

## 4. Machine fingerprint (`0x28650`, `0x27240`, *measured*)

Built once, sent in Hello (max 8 entries, each 18 chars). Each entry:

```
tag + ":" + first 16 hex chars of SHA256("anarchykenshi-machine-v1:" + tag + ":" + lowercase(value))
```

Order and sources:

1. `g`: `HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid` (REG_SZ).
2. Windows: `s`: serial of the physical disk holding the Windows drive
   (`IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS` on `\\.\C:` → `\\.\PhysicalDrive%lu` →
   `IOCTL_STORAGE_QUERY_PROPERTY`, alphanumerics kept) (`0x27750`).
   Wine (`ntdll!wine_get_version` present): `l`: `Z:\etc\machine-id` or
   `Z:\var\lib\dbus\machine-id`, hex only (`0x27ce0`).
3. `m`: one per network adapter MAC (`GetAdaptersAddresses`, `0x28160`): 6-byte address,
   Ethernet (6) or 802.11 (71), not locally administered, name not containing any of `virtual
   vmware vbox hyper-v vethernet vpn tap- "tap " tun wintun wireguard npcap loopback zerotier
   tailscale pseudo bluetooth miniport teredo isatap radmin hamachi openvpn nord proton
   "cisco anyconnect" fortinet docker wsl` (list at `0x71c20`); formatted `%02x` ×6.

## 5. HTTP browser and report

Plain HTTP/1.1 over TCP (`0x2660`: `socket(AF_INET, SOCK_STREAM)`, `gethostbyname`, `connect`).
Headers `User-Agent: AnarchyKenshi/1 (browser)`, `Accept: text/tab-separated-values,
text/plain`, `Connection: close`. Paths `/v1/servers` (TSV with a `#fields ` header line),
`/v1/announce` (host side), `/v1/report` (`application/octet-stream`, magic `AKBR1`, JSON keys
`category`, `text`, `facts`, `logs[{name,size,sent}]`). Details below.

### `GET /v1/servers` (TSV; parser `0x5a20`, request `0x7320`, *measured* from compares)

- Plain HTTP/1.1; response split on `\n`; the line starting `#fields ` (8 bytes, `memcmp` at `0x5a20`)
  names the columns (the rest of that line is split into names; separator not read: *inferred* tab).
  Data lines are split into cells the same way. Columns are looked up **by name**, so order is free.
- Column names recognised (decoded from the 32-bit / 16-bit constant compares in `0x5a20`):
  `addr`, `name`, `players`, `max`, `pass`, `proto`, `mission`, `build`, `region`, `age`
  (lengths 4, 4, 7, 3, 4, 5, 7, 5, 6, 3). Unknown columns are skipped. Semantics are *inferred* from
  the names (address `host:port`, display name, current/max players, password flag, protocol
  version (compare with 133), mission/world name, game build, region, age in seconds).
- `/v1/announce` is the host-side registration (not used by this player build).

### `POST /v1/report` (AKBR1; `AnarchyClientReportSend` `0x1cc50` -> `0xae30` -> body builder `0x9070`)

- `Content-Type: application/octet-stream`; `Connection: close`; user agent as above.
  Success text: "sent - report <id>. Thank you!"; error: "could not send (HTTP N: <reason or `no reason
  given`>)". Status lines are shown through `AnarchyClientReportStatus`.
- Body (`0x9070`, *measured* from the string/constants it assembles):

```
"AKBR1\n"                         (bytes 41 4b 42 52 31 0a)
JSON text:
  {"category":"<c>","text":"<t>","facts":{ ... },
   "logs":[{"name":"<file>","size":<n>,"sent":<n>}, ... ]}
"\n"
for each log file:  "\n===== file <name>: last <N> bytes =====\n" + the last N bytes of that file
```
  `text` is capped to 8000 characters (`0x1f40`); total log bytes are capped to 10 MiB (`0xa00000`)
  or the caller's byte budget (argument 7), whichever is smaller; the profile directory (the
  `USERPROFILE` environment variable) is replaced by `%USERPROFILE%` in the text before sending
  (`0x9070`, string `"%USERPROFILE%"`). The export is
  `AnarchyClientReportSend(client, category, text, facts?, logs?, ?, maxBytes)` (Ghidra's guess of
  the middle arguments; not read).
- Where the facts and logs come from is decided by AnarchyKenshi.dll (not read here).

## 6. C ABI used by AnarchyKenshi.dll

See [AnarchyCore-abi.md](AnarchyCore-abi.md).

## Open questions

- What the server writes into the Welcome `u16` and the Redirect `u32` (the client ignores both);
  Kenshi's names for `Character+0xa4` and the movement offsets used by Position.
- Field semantics of Vitals, Hits, CombatStates, CombatAnims, Kit records, ItemMove strings (sizes are
  measured, names are guesses): readable from the AnarchyKenshi.dll code that consumes them.
- `AnarchyClientUpdate`/`PollMessage` ABI argument counts and the argument semantics of `SpeedVotes`,
  `ReportSend`, `Squad/Chained/Owners` are Ghidra's guess only.
- Server-side channel/mode per message; whether stock ENet 1.3.x accepts this client unmodified (only
  *inferred* from the header layout).
- How mods are compared (reject reason ModsDiffer): the Spine `Mods` topic (kind `0x14`, see
  [spine.md](spine.md)) is the likely carrier (*inferred*).
