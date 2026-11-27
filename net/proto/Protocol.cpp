// KenshiPure protocol v1 codec (spec: docs/protocol-ours.md).
#include "Protocol.h"

#include <bit>
#include <cmath>
#include <cstring>

namespace kp::proto {
namespace {

class Writer {
public:
    void u8(std::uint8_t v) { out_.push_back(v); }
    void u16(std::uint16_t v) { u8(static_cast<std::uint8_t>(v)); u8(static_cast<std::uint8_t>(v >> 8)); }
    void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) u8(static_cast<std::uint8_t>(v >> (8 * i))); }
    void f32(float v) { u32(std::bit_cast<std::uint32_t>(v)); }
    void str16(std::string_view s) {
        u16(static_cast<std::uint16_t>(s.size()));
        out_.insert(out_.end(), s.begin(), s.end());
    }
    void pose(const Pose& p) {
        for (float v : p.pos) f32(v);
        for (float v : p.vel) f32(v);
        f32(p.yaw);
        u8(p.flags);
    }
    Bytes take() { return std::move(out_); }

private:
    Bytes out_;
};

// Reads little-endian values; any read past the end sets failed() and returns 0.
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> d) : d_(d) {}
    bool failed() const { return failed_; }
    bool atEnd() const { return pos_ == d_.size(); }
    std::uint8_t u8() {
        if (pos_ >= d_.size()) { failed_ = true; return 0; }
        return d_[pos_++];
    }
    std::uint16_t u16() { std::uint16_t lo = u8(); return static_cast<std::uint16_t>(lo | (u8() << 8)); }
    std::uint32_t u32() {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(u8()) << (8 * i);
        return v;
    }
    float f32() { return std::bit_cast<float>(u32()); }
    std::string str16(std::size_t maxBytes) {
        std::size_t n = u16();
        if (failed_ || n > maxBytes || d_.size() - pos_ < n) { failed_ = true; return {}; }
        std::string s(reinterpret_cast<const char*>(d_.data() + pos_), n);
        pos_ += n;
        return s;
    }
    Pose pose() {
        Pose p;
        for (float& v : p.pos) v = f32();
        for (float& v : p.vel) v = f32();
        p.yaw = f32();
        p.flags = u8();
        return p;
    }

private:
    std::span<const std::uint8_t> d_;
    std::size_t pos_ = 0;
    bool failed_ = false;
};

bool inRange(float v, float limit) { return std::isfinite(v) && std::fabs(v) <= limit; }

struct Encoder {
    Writer& w;
    bool operator()(const Hello& m) const {
        if (!validName(m.name)) return false;
        w.u8(static_cast<std::uint8_t>(Type::Hello)); w.u16(m.version); w.str16(m.name);
        return true;
    }
    bool operator()(const Welcome& m) const {
        if (m.playerId == 0 || m.rateHz < 1 || m.rateHz > 60) return false;
        w.u8(static_cast<std::uint8_t>(Type::Welcome)); w.u16(m.version); w.u16(m.playerId); w.u16(m.rateHz);
        return true;
    }
    bool operator()(const Reject& m) const {
        auto r = static_cast<std::uint8_t>(m.reason);
        if (r < 1 || r > 5) return false;
        w.u8(static_cast<std::uint8_t>(Type::Reject)); w.u8(r); w.u16(m.version);
        return true;
    }
    bool operator()(const PoseMsg& m) const {
        if (!validPose(m.pose)) return false;
        w.u8(static_cast<std::uint8_t>(Type::Pose)); w.u32(m.seq); w.pose(m.pose);
        return true;
    }
    bool operator()(const Snapshot& m) const {
        if (m.entries.size() > kMaxSnapshotEntries) return false;
        for (const auto& e : m.entries)
            if (e.playerId == 0 || !validPose(e.pose)) return false;
        w.u8(static_cast<std::uint8_t>(Type::Snapshot)); w.u32(m.tick);
        w.u8(static_cast<std::uint8_t>(m.entries.size()));
        for (const auto& e : m.entries) { w.u16(e.playerId); w.u16(e.ageMs); w.pose(e.pose); }
        return true;
    }
    bool operator()(const PlayerJoined& m) const {
        if (m.playerId == 0 || !validName(m.name)) return false;
        w.u8(static_cast<std::uint8_t>(Type::PlayerJoined)); w.u16(m.playerId); w.str16(m.name);
        return true;
    }
    bool operator()(const PlayerLeft& m) const {
        auto r = static_cast<std::uint8_t>(m.reason);
        if (m.playerId == 0 || r > 2) return false;
        w.u8(static_cast<std::uint8_t>(Type::PlayerLeft)); w.u16(m.playerId); w.u8(r);
        return true;
    }
};

} // namespace

bool validName(std::string_view name) {
    if (name.empty() || name.size() > kMaxNameBytes) return false;
    for (unsigned char c : name)
        if (c < 0x20 || c == 0x7f) return false;
    return true;
}

bool validPose(const Pose& p) {
    for (float v : p.pos) if (!inRange(v, kMaxPosition)) return false;
    for (float v : p.vel) if (!inRange(v, kMaxVelocity)) return false;
    if (!inRange(p.yaw, kMaxYaw)) return false;
    return (p.flags & ~kFlagsKnown) == 0;
}

float normaliseYaw(float deg) {
    if (!std::isfinite(deg)) return 0.0f;
    float r = std::fmod(deg, 360.0f);
    if (r < 0) r += 360.0f;
    if (r >= 360.0f) r = 0.0f;   // fmod of a tiny negative value can round up to 360
    return r;
}

Bytes encode(const Message& m) {
    Writer w;
    if (!std::visit(Encoder{w}, m)) return {};
    Bytes out = w.take();
    if (out.size() > kMaxMessage) return {};
    return out;
}

std::optional<Message> decode(std::span<const std::uint8_t> payload) {
    if (payload.empty() || payload.size() > kMaxMessage) return std::nullopt;
    Reader r(payload.subspan(1));
    Message m;
    switch (static_cast<Type>(payload[0])) {
    case Type::Hello: {
        Hello h; h.version = r.u16(); h.name = r.str16(kMaxNameBytes);
        if (r.failed() || !validName(h.name)) return std::nullopt;
        m = std::move(h);
        break;
    }
    case Type::Welcome: {
        Welcome x; x.version = r.u16(); x.playerId = r.u16(); x.rateHz = r.u16();
        if (r.failed() || x.playerId == 0 || x.rateHz < 1 || x.rateHz > 60) return std::nullopt;
        m = x;
        break;
    }
    case Type::Reject: {
        Reject x; std::uint8_t reason = r.u8(); x.version = r.u16();
        if (r.failed() || reason < 1 || reason > 5) return std::nullopt;
        x.reason = static_cast<RejectReason>(reason);
        m = x;
        break;
    }
    case Type::Pose: {
        PoseMsg x; x.seq = r.u32(); x.pose = r.pose();
        if (r.failed() || !validPose(x.pose)) return std::nullopt;
        m = x;
        break;
    }
    case Type::Snapshot: {
        Snapshot x; x.tick = r.u32();
        std::size_t n = r.u8();
        if (r.failed() || n > kMaxSnapshotEntries) return std::nullopt;
        x.entries.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            SnapshotEntry e; e.playerId = r.u16(); e.ageMs = r.u16(); e.pose = r.pose();
            if (r.failed() || e.playerId == 0 || !validPose(e.pose)) return std::nullopt;
            x.entries.push_back(e);
        }
        m = std::move(x);
        break;
    }
    case Type::PlayerJoined: {
        PlayerJoined x; x.playerId = r.u16(); x.name = r.str16(kMaxNameBytes);
        if (r.failed() || x.playerId == 0 || !validName(x.name)) return std::nullopt;
        m = std::move(x);
        break;
    }
    case Type::PlayerLeft: {
        PlayerLeft x; x.playerId = r.u16(); std::uint8_t reason = r.u8();
        if (r.failed() || x.playerId == 0 || reason > 2) return std::nullopt;
        x.reason = static_cast<LeaveReason>(reason);
        m = x;
        break;
    }
    default:
        return std::nullopt;
    }
    if (!r.atEnd()) return std::nullopt;   // trailing bytes
    return m;
}

Type typeOf(const Message& m) {
    static constexpr Type kTypes[] = { Type::Hello, Type::Welcome, Type::Reject, Type::Pose,
                                       Type::Snapshot, Type::PlayerJoined, Type::PlayerLeft };
    return kTypes[m.index()];
}

Channel channelOf(Type t) {
    return (t == Type::Pose || t == Type::Snapshot) ? Channel::Unreliable : Channel::Reliable;
}

} // namespace kp::proto
