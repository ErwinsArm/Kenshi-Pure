// Small ENet helpers shared by server and client.
#pragma once
#include <enet/enet.h>

#include "../proto/Protocol.h"

namespace kp::net {

// enet_initialize / enet_deinitialize, counted (several hosts in one process, e.g. tests).
class EnetLib {
public:
    EnetLib();
    ~EnetLib();
    EnetLib(const EnetLib&) = delete;
    EnetLib& operator=(const EnetLib&) = delete;
    bool ok() const { return ok_; }

private:
    bool ok_ = false;
};

// Encodes and queues one message on its channel; false if it could not be encoded or queued.
bool sendMessage(ENetPeer* peer, const proto::Message& m);

// Queues raw bytes (tests only: lets a test send malformed data).
bool sendRaw(ENetPeer* peer, const proto::Bytes& bytes, proto::Channel ch, bool reliable);

// Decodes a received packet and checks that it came on its type's channel with the right
// reliability; std::nullopt otherwise.
std::optional<proto::Message> decodePacket(const ENetPacket* packet, enet_uint8 channel);

} // namespace kp::net
