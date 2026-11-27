#include "Enet.h"

#include <mutex>

namespace kp::net {
namespace {
std::mutex g_mutex;
int g_users = 0;
bool g_ok = false;
}

EnetLib::EnetLib() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_users++ == 0) g_ok = (enet_initialize() == 0);
    ok_ = g_ok;
}

EnetLib::~EnetLib() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (--g_users == 0 && g_ok) { enet_deinitialize(); g_ok = false; }
}

bool sendRaw(ENetPeer* peer, const proto::Bytes& bytes, proto::Channel ch, bool reliable) {
    if (!peer || bytes.empty()) return false;
    ENetPacket* p = enet_packet_create(bytes.data(), bytes.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (!p) return false;
    if (enet_peer_send(peer, static_cast<enet_uint8>(ch), p) != 0) { enet_packet_destroy(p); return false; }
    return true;
}

bool sendMessage(ENetPeer* peer, const proto::Message& m) {
    proto::Bytes b = proto::encode(m);
    if (b.empty()) return false;
    proto::Channel ch = proto::channelOf(proto::typeOf(m));
    return sendRaw(peer, b, ch, ch == proto::Channel::Reliable);
}

std::optional<proto::Message> decodePacket(const ENetPacket* packet, enet_uint8 channel) {
    if (!packet) return std::nullopt;
    auto m = proto::decode(std::span<const std::uint8_t>(packet->data, packet->dataLength));
    if (!m) return std::nullopt;
    proto::Channel want = proto::channelOf(proto::typeOf(*m));
    bool reliable = (packet->flags & ENET_PACKET_FLAG_RELIABLE) != 0;
    if (channel != static_cast<enet_uint8>(want) || reliable != (want == proto::Channel::Reliable)) return std::nullopt;
    return m;
}

} // namespace kp::net
