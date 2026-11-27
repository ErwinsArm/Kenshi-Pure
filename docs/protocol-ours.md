# KenshiPure protocol (ours, version 1)

Our own wire protocol between the KenshiPure client, the relay server and the test bot. It is
**not** compatible with AnarchyKenshi; only AK's framing rules are reused
([findings/protocol.md](findings/protocol.md) §1–2, *measured* in AnarchyCore.dll v0.133.1).
Code: `net/proto/` (codec), `net/server/` (relay), `net/client/` (client used by the bot, later by
the client core), `net/test/` (tests that pin every layout below). Build: [setup.md](setup.md).

## Transport

- ENet 1.3.18 (MIT), built from source into each program (`scripts/setup-modern.ps1`). No ENet
  checksum, no compression (as AK).
- UDP, default port **27850** (AK uses 27800; ours differs so both can run on one PC). Configurable.
- **2 channels**: 0 = reliable, ordered (Hello, Welcome, Reject, PlayerJoined, PlayerLeft);
  1 = unreliable (Pose, Snapshot). ENet discards an unreliable packet older than one already
  received on the same channel (`peer.c` 910–912 of the pinned source, *measured*), so a receiver
  does not go back in time. The two channels are **not** ordered against each other.
- One message per ENet packet. A message on the wrong channel or with the wrong reliability is
  malformed (checked by `net/common/Enet.cpp`, tested in `LoopbackTest.cpp`).
- Size: every message ≤ **1200 bytes** (AK's MTU), so nothing is fragmented. The server caps
  players at **32**; a Snapshot holds at most the 31 others (6 + 31 × 33 = 1029 bytes).

## Framing (AK's rules)

- Byte 0: message type (`u8`).
- Integers and floats little-endian, unaligned; `f32` is IEEE 754 binary32.
- `str16` = `u16` byte length + bytes, no terminator; each field has its own maximum. Names are
  meant as UTF-8 but only checked for length and control bytes.
- A parser consumes the payload **exactly**: too short, too long (trailing bytes), unknown type, an
  out-of-range value or any non-finite float (NaN, ±inf) makes the whole message malformed. Nothing
  of a malformed message is used. Encoders refuse to write what the decoder would reject.

## Pose (29 bytes, used in Pose and Snapshot)

| Offset | Type | Field | Meaning (M1, [client.md](client.md) "Own pose (M1)") |
|---|---|---|---|
| 0 | f32 ×3 | position x, y, z | world units, Kenshi's character position; finite, \|v\| ≤ 10 000 000 |
| 12 | f32 ×3 | velocity x, y, z | `AbstractMovementBase::currentMotion`: world units per second, length = `currentSpeed`, 0 standing (*measured* at game speed ×1); per second of game time, not real time (*inferred*, short walks at ×5); finite, \|v\| ≤ 100 000 |
| 24 | f32 | yaw | degrees, 0 = +z, 90 = +x (*measured*); finite, −360 ≤ yaw ≤ 360 (senders normalise to [0, 360)) |
| 28 | u8 | flags | bit 0 = moving (`currentlyMoving`, *measured*), bit 1 = paused (sender's game is paused); bits 2–7 must be 0 |

The limits only reject garbage; they are not a model of the game world.

## Messages

`C→S` client to server, `S→C` server to client. Sizes include the type byte.

| Type | Name | Dir | Ch | Payload after the type byte | Size |
|---|---|---|---|---|---|
| `01` | Hello | C→S | 0 | `u16` protocol version (= 1), `str16` name (1–32 bytes, no byte < 0x20 or = 0x7f) | 6–37 |
| `02` | Welcome | S→C | 0 | `u16` protocol version, `u16` your player id (1–65535), `u16` snapshot rate in Hz (1–60) | 7 |
| `03` | Reject | S→C | 0 | `u8` reason (1–5, below), `u16` server's protocol version | 4 |
| `04` | Pose | C→S | 1 | `u32` sequence (sender's counter, +1 per Pose), Pose | 34 |
| `05` | Snapshot | S→C | 1 | `u32` tick, `u8` n (0–31), n × {`u16` player id (≥ 1), `u16` age in ms, Pose} | 6 + 33 n |
| `06` | PlayerJoined | S→C | 0 | `u16` player id (≥ 1), `str16` name (as in Hello) | 6–37 |
| `07` | PlayerLeft | S→C | 0 | `u16` player id (≥ 1), `u8` reason (0 = left, 1 = timed out, 2 = removed by the server) | 4 |

Reject reasons: 1 = protocol version differs, 2 = bad name (reserved: a bad name currently fails the
decoder and gives 4), 3 = server full, 4 = malformed or unexpected message, 5 = no Hello first or
in time. Hello and Reject keep this layout in every protocol version, so a mismatch is always
reported.

Snapshot age: milliseconds since the server received that player's pose (capped at 65535). A
player whose game stalls (e.g. ~60 s while loading) keeps being relayed with a growing age, so a
receiver can tell a frozen pose from a live one.

## Session

1. Client connects (ENet, 2 channels) and sends **Hello** at once.
2. Server answers **Welcome** with a fresh player id (never reused while the server runs), then one
   **PlayerJoined** for every player already in, and sends **PlayerJoined** for the newcomer to all
   others. Or it sends **Reject** and closes with `enet_peer_disconnect_later` (the Reject is
   delivered first).
3. Client sends **Pose** at 10 Hz. The server keeps the newest Pose per player by sequence
   (serial-number comparison, so the counter may wrap); an older or equal sequence is dropped.
4. Every 100 ms (`tick` +1 each time) the server sends each welcomed player a **Snapshot** with the
   latest pose of every **other** player that has sent one. A player without a Pose yet is left
   out; an empty Snapshot is not sent.
5. A client that leaves on purpose disconnects with ENet data 1 → **PlayerLeft** reason 0 to all
   others; any other disconnect (ENet timeout) → reason 1; a player the server rejected after
   Welcome → reason 2.

Server rules: the first message of a peer must be Hello and must come within 5 s (else Reject 5);
a second Hello, any S→C type, or a malformed message → Reject 4 and disconnect. The host has 16
spare ENet peer slots beyond the player cap, so a full server can still answer Reject 3 (ENet
itself refuses connects beyond its peer count without a word). The server is a relay: it checks
the format, not whether a pose is plausible. There is **no authentication** and no encryption
(any name, duplicates allowed); fit for a LAN or private VPN, not for a public server.

Client rules (`net/client/NetClient.cpp`): before Welcome only Welcome or Reject is accepted. A
Snapshot entry is used only for a player announced by PlayerJoined and not yet left: since the
channels are not ordered against each other, a Snapshot can name a player before its PlayerJoined
or after its PlayerLeft, and ids are never reused. Anything malformed or unexpected is dropped and
counted; the client stays connected.

## Timeouts

ENet's defaults (`ENET_PEER_TIMEOUT_LIMIT` 32, `_MINIMUM` 5000 ms, `_MAXIMUM` 30000 ms, `enet.h`
230–232 of the pinned source, *measured*) disconnect a peer that stops acknowledging for 5–30 s.
Kenshi's main thread stalls ~60 s while loading, so from M3 on the client services ENet on its own
network thread (plan G14); the server keeps ENet's defaults.

## Changing the protocol

Any change to a layout or meaning raises the protocol version; the server rejects other versions
(Reject 1).
