// KenshiPure protocol v1: message structs and the codec (spec: docs/protocol-ours.md).
// Plain C++20, no ENet, no Kenshi. Every decode consumes the payload exactly and rejects
// out-of-range values and non-finite floats; nothing of a malformed message is returned.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace kp::proto {

inline constexpr std::uint16_t kProtocolVersion = 1;
inline constexpr std::uint16_t kDefaultPort = 27850;
inline constexpr std::size_t kMaxMessage = 1200;   // no ENet fragmentation
inline constexpr std::size_t kMaxNameBytes = 32;
inline constexpr std::size_t kMaxPlayers = 32;           // server cap
inline constexpr std::size_t kMaxSnapshotEntries = kMaxPlayers - 1;  // the recipient is left out
inline constexpr std::uint32_t kDisconnectLeft = 1;   // ENet disconnect data of a client that leaves on purpose
inline constexpr std::size_t kPoseBytes = 29;
inline constexpr std::size_t kSnapshotEntryBytes = 2 + 2 + kPoseBytes;  // id, age, pose
inline constexpr float kMaxPosition = 10000000.0f;
inline constexpr float kMaxVelocity = 100000.0f;
inline constexpr float kMaxYaw = 360.0f;

enum class Channel : std::uint8_t { Reliable = 0, Unreliable = 1 };
inline constexpr std::size_t kChannelCount = 2;

enum class Type : std::uint8_t {
    Hello = 0x01, Welcome = 0x02, Reject = 0x03, Pose = 0x04,
    Snapshot = 0x05, PlayerJoined = 0x06, PlayerLeft = 0x07,
};

enum PoseFlags : std::uint8_t { kFlagMoving = 0x01, kFlagPaused = 0x02, kFlagsKnown = 0x03 };

enum class RejectReason : std::uint8_t {
    Version = 1, BadName = 2, Full = 3, Malformed = 4, NoHello = 5,
};

enum class LeaveReason : std::uint8_t { Left = 0, TimedOut = 1, Removed = 2 };

struct Pose {
    float pos[3]{};
    float vel[3]{};
    float yaw = 0;          // degrees, 0 = +z, 90 = +x
    std::uint8_t flags = 0; // PoseFlags
    bool operator==(const Pose&) const = default;
};

struct Hello { std::uint16_t version = kProtocolVersion; std::string name; bool operator==(const Hello&) const = default; };
struct Welcome { std::uint16_t version = kProtocolVersion; std::uint16_t playerId = 0; std::uint16_t rateHz = 10; bool operator==(const Welcome&) const = default; };
struct Reject { RejectReason reason = RejectReason::Malformed; std::uint16_t version = kProtocolVersion; bool operator==(const Reject&) const = default; };
struct PoseMsg { std::uint32_t seq = 0; Pose pose; bool operator==(const PoseMsg&) const = default; };
struct SnapshotEntry { std::uint16_t playerId = 0; std::uint16_t ageMs = 0; Pose pose; bool operator==(const SnapshotEntry&) const = default; };
struct Snapshot { std::uint32_t tick = 0; std::vector<SnapshotEntry> entries; bool operator==(const Snapshot&) const = default; };
struct PlayerJoined { std::uint16_t playerId = 0; std::string name; bool operator==(const PlayerJoined&) const = default; };
struct PlayerLeft { std::uint16_t playerId = 0; LeaveReason reason = LeaveReason::Left; bool operator==(const PlayerLeft&) const = default; };

using Message = std::variant<Hello, Welcome, Reject, PoseMsg, Snapshot, PlayerJoined, PlayerLeft>;

using Bytes = std::vector<std::uint8_t>;

// Encoders return an empty vector if the message itself is invalid (bad name, NaN, too many
// entries...): an encoder never writes what the decoder would reject.
Bytes encode(const Message& m);

// Decodes one whole message; std::nullopt if malformed in any way.
std::optional<Message> decode(std::span<const std::uint8_t> payload);

// The channel each type travels on (reliable or not follows from it).
Channel channelOf(Type t);
Type typeOf(const Message& m);

// Validity checks shared by encoder, decoder and callers.
bool validName(std::string_view name);
bool validPose(const Pose& p);

// Serial-number comparison of 32-bit sequences (RFC 1982): a is newer than b.
inline bool seqNewer(std::uint32_t a, std::uint32_t b) {
    return a != b && static_cast<std::int32_t>(a - b) > 0;
}

// Yaw in degrees normalised to [0, 360).
float normaliseYaw(float deg);

} // namespace kp::proto
