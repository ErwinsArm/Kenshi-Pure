// Codec tests: exact byte layouts (docs/protocol-ours.md), round trips, and rejection of
// truncated, oversized, trailing-byte, out-of-range and non-finite input.
#include <cmath>
#include <cstring>
#include <limits>

#include "Check.h"
#include "proto/Protocol.h"

using namespace kp::proto;

namespace {

Pose samplePose(float k = 1) {
    Pose p;
    p.pos[0] = -51234.5f * k; p.pos[1] = 1520.25f; p.pos[2] = 8800.125f * k;
    p.vel[0] = 72.1f; p.vel[1] = -0.5f; p.vel[2] = -3.25f;
    p.yaw = 91.7f;
    p.flags = kFlagMoving;
    return p;
}

std::vector<Message> samples() {
    Snapshot full;
    full.tick = 0xfffffffe;
    for (std::uint16_t i = 1; i <= kMaxSnapshotEntries; ++i) full.entries.push_back({i, static_cast<std::uint16_t>(i * 100), samplePose(i)});
    return {
        Hello{kProtocolVersion, "Beep"},
        Hello{7, std::string(kMaxNameBytes, 'x')},
        Welcome{kProtocolVersion, 65535, 10},
        Reject{RejectReason::Full, kProtocolVersion},
        Reject{RejectReason::NoHello, 3},
        PoseMsg{0xffffffff, samplePose()},
        PoseMsg{1, Pose{}},
        Snapshot{7, {}},
        Snapshot{8, {{3, 0, samplePose()}, {9, 65535, samplePose(2)}}},
        full,
        PlayerJoined{12, "Ruka the Second"},
        PlayerLeft{12, LeaveReason::TimedOut},
    };
}

bool same(const Message& a, const Message& b) { return a == b; }

std::size_t expectedSize(const Message& m) {
    switch (typeOf(m)) {
    case Type::Hello: return 1 + 2 + 2 + std::get<Hello>(m).name.size();
    case Type::Welcome: return 1 + 2 + 2 + 2;
    case Type::Reject: return 1 + 1 + 2;
    case Type::Pose: return 1 + 4 + kPoseBytes;
    case Type::Snapshot: return 1 + 4 + 1 + std::get<Snapshot>(m).entries.size() * kSnapshotEntryBytes;
    case Type::PlayerJoined: return 1 + 2 + 2 + std::get<PlayerJoined>(m).name.size();
    case Type::PlayerLeft: return 1 + 2 + 1;
    }
    return 0;
}

void putF32(Bytes& b, std::size_t at, float v) {
    std::uint32_t u;
    std::memcpy(&u, &v, 4);
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<std::uint8_t>(u >> (8 * i));
}

void testRoundTrips() {
    for (const auto& m : samples()) {
        Bytes b = encode(m);
        CHECK(!b.empty());
        CHECK(b.size() == expectedSize(m));
        CHECK(b.size() <= kMaxMessage);
        CHECK(b[0] == static_cast<std::uint8_t>(typeOf(m)));
        auto d = decode(b);
        CHECK(d.has_value());
        if (d) CHECK(same(*d, m));
        // Every proper prefix is truncated and must fail.
        for (std::size_t n = 0; n < b.size(); ++n) CHECK(!decode(std::span(b.data(), n)).has_value());
        // One trailing byte must fail.
        Bytes t = b; t.push_back(0);
        CHECK(!decode(t).has_value());
    }
}

void testLayouts() {
    // Hello "ab": 01 | 01 00 | 02 00 | 'a' 'b'
    Bytes h = encode(Hello{1, "ab"});
    CHECK((h == Bytes{0x01, 0x01, 0x00, 0x02, 0x00, 'a', 'b'}));
    CHECK((encode(Welcome{1, 0x1234, 10}) == Bytes{0x02, 0x01, 0x00, 0x34, 0x12, 0x0a, 0x00}));
    CHECK((encode(Reject{RejectReason::Version, 1}) == Bytes{0x03, 0x01, 0x01, 0x00}));
    CHECK((encode(PlayerLeft{0x0102, LeaveReason::Removed}) == Bytes{0x07, 0x02, 0x01, 0x02}));
    CHECK((encode(PlayerJoined{5, "Z"}) == Bytes{0x06, 0x05, 0x00, 0x01, 0x00, 'Z'}));
    // Pose: seq LE at 1, then pos x at 5 (1.0f = 00 00 80 3f), yaw at 29, flags at 33.
    Pose p; p.pos[0] = 1.0f; p.yaw = -2.0f; p.flags = kFlagMoving | kFlagPaused;
    Bytes b = encode(PoseMsg{0x01020304, p});
    CHECK(b.size() == 34);
    CHECK((Bytes(b.begin(), b.begin() + 9) == Bytes{0x04, 0x04, 0x03, 0x02, 0x01, 0x00, 0x00, 0x80, 0x3f}));
    CHECK((Bytes(b.begin() + 29, b.begin() + 33) == Bytes{0x00, 0x00, 0x00, 0xc0}));
    CHECK(b[33] == 0x03);
    // Snapshot: tick LE, count, then {id LE, age ms LE, pose}.
    Bytes s = encode(Snapshot{0x0a0b0c0d, {{0x0201, 0x0403, Pose{}}}});
    CHECK(s.size() == 6 + 33);
    CHECK((Bytes(s.begin(), s.begin() + 10) == Bytes{0x05, 0x0d, 0x0c, 0x0b, 0x0a, 0x01, 0x01, 0x02, 0x03, 0x04}));
    // Full snapshot stays under the MTU.
    static_assert(6 + kMaxSnapshotEntries * kSnapshotEntryBytes <= kMaxMessage);
}

void testRejections() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    Bytes good = encode(PoseMsg{5, samplePose()});
    // Non-finite or out-of-range float in each of the 7 float fields.
    for (std::size_t f = 0; f < 7; ++f) {
        for (float bad : {nan, inf, -inf, 2e7f}) {
            Bytes b = good;
            if (bad == 2e7f && f >= 3) continue;   // checked below for velocity/yaw limits
            putF32(b, 5 + 4 * f, bad);
            CHECK(!decode(b).has_value());
        }
    }
    { Bytes b = good; putF32(b, 5 + 12, 100001.0f); CHECK(!decode(b).has_value()); }   // velocity
    { Bytes b = good; putF32(b, 5 + 24, 360.5f); CHECK(!decode(b).has_value()); }      // yaw
    { Bytes b = good; putF32(b, 5 + 24, -360.0f); CHECK(decode(b).has_value()); }      // yaw edge ok
    { Bytes b = good; b[33] = 0x04; CHECK(!decode(b).has_value()); }                   // unknown flag
    // NaN inside a Snapshot entry.
    Bytes s = encode(Snapshot{1, {{2, 0, samplePose()}, {3, 0, samplePose()}}});
    { Bytes b = s; putF32(b, 6 + 33 + 4 + 4, nan); CHECK(!decode(b).has_value()); }
    // Snapshot count says more entries than present / more than allowed / player id 0.
    { Bytes b = s; b[5] = 3; CHECK(!decode(b).has_value()); }
    { Bytes b = s; b[5] = 1; CHECK(!decode(b).has_value()); }   // fewer: trailing bytes
    { Bytes b{0x05, 0, 0, 0, 0, static_cast<std::uint8_t>(kMaxSnapshotEntries + 1)}; CHECK(!decode(b).has_value()); }
    { Bytes b = s; b[6] = 0; b[7] = 0; CHECK(!decode(b).has_value()); }
    // Oversized: a message over 1200 bytes, even if the head is valid.
    { Bytes b = encode(Hello{1, "ok"}); Bytes big = b; big.resize(kMaxMessage + 1, 0); CHECK(!decode(big).has_value()); }
    { Snapshot over; for (std::uint16_t i = 1; i <= kMaxSnapshotEntries + 1; ++i) over.entries.push_back({i, 0, Pose{}}); CHECK(encode(over).empty()); }
    // Names: empty, too long, control chars, length field longer than the data.
    CHECK(encode(Hello{1, ""}).empty());
    CHECK(encode(Hello{1, std::string(kMaxNameBytes + 1, 'a')}).empty());
    CHECK(encode(Hello{1, std::string("a\nb")}).empty());
    CHECK(encode(Hello{1, std::string("a\x7f")}).empty());
    CHECK(!decode(Bytes{0x01, 0x01, 0x00, 0x00, 0x00}).has_value());
    CHECK(!decode(Bytes{0x01, 0x01, 0x00, 0x03, 0x00, 'a', 'b'}).has_value());
    CHECK(!decode(Bytes{0x01, 0x01, 0x00, 0x01, 0x00, 0x09}).has_value());
    { Bytes b{0x01, 0x01, 0x00, 33, 0x00}; b.resize(5 + 33, 'a'); CHECK(!decode(b).has_value()); }
    // Enum and id ranges.
    CHECK(!decode(Bytes{0x03, 0x00, 0x01, 0x00}).has_value());
    CHECK(!decode(Bytes{0x03, 0x06, 0x01, 0x00}).has_value());
    CHECK(!decode(Bytes{0x07, 0x01, 0x00, 0x03}).has_value());
    CHECK(!decode(Bytes{0x07, 0x00, 0x00, 0x00}).has_value());
    CHECK(!decode(Bytes{0x02, 0x01, 0x00, 0x00, 0x00, 0x0a, 0x00}).has_value());   // id 0
    CHECK(!decode(Bytes{0x02, 0x01, 0x00, 0x01, 0x00, 0x3d, 0x00}).has_value());   // 61 Hz
    CHECK(!decode(Bytes{0x06, 0x00, 0x00, 0x01, 0x00, 'a'}).has_value());
    // Unknown types and empty input.
    CHECK(!decode(Bytes{}).has_value());
    for (int t : {0x00, 0x08, 0x0b, 0x7f, 0xff}) CHECK(!decode(Bytes{static_cast<std::uint8_t>(t), 0, 0, 0}).has_value());
    // Encoders refuse what decoders refuse.
    Pose bad = samplePose(); bad.vel[1] = nan;
    CHECK(encode(PoseMsg{1, bad}).empty());
    CHECK(encode(Welcome{1, 0, 10}).empty());
    CHECK(encode(PlayerLeft{0, LeaveReason::Left}).empty());
}

void testHelpers() {
    CHECK(seqNewer(2, 1));
    CHECK(!seqNewer(1, 2));
    CHECK(!seqNewer(5, 5));
    CHECK(seqNewer(0, 0xffffffff));   // wrap
    CHECK(!seqNewer(0xffffffff, 0));
    CHECK(normaliseYaw(-90.0f) == 270.0f);
    CHECK(normaliseYaw(720.0f) == 0.0f);
    CHECK(normaliseYaw(359.5f) == 359.5f);
    float tiny = normaliseYaw(-1e-6f);
    CHECK(tiny >= 0.0f && tiny < 360.0f);
    CHECK(channelOf(Type::Pose) == Channel::Unreliable);
    CHECK(channelOf(Type::Snapshot) == Channel::Unreliable);
    for (Type t : {Type::Hello, Type::Welcome, Type::Reject, Type::PlayerJoined, Type::PlayerLeft})
        CHECK(channelOf(t) == Channel::Reliable);
}

} // namespace

int main() {
    testRoundTrips();
    testLayouts();
    testRejections();
    testHelpers();
    return TEST_RESULT("proto");
}
