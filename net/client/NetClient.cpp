#include "NetClient.h"

#include <algorithm>

namespace kp::net {

using namespace proto;

NetClient::~NetClient() { close(); }

void NetClient::close() {
    if (host_) enet_host_destroy(host_);
    host_ = nullptr;
    peer_ = nullptr;
    connected_ = false;
    playerId_ = 0;
    known_.clear();
}

bool NetClient::connect(const std::string& host, std::uint16_t port, const std::string& name, std::uint16_t version) {
    close();
    if (!lib_.ok()) return false;
    name_ = name;
    version_ = version;
    host_ = enet_host_create(nullptr, 1, kChannelCount, 0, 0);
    if (!host_) return false;
    ENetAddress addr{};
    if (enet_address_set_host(&addr, host.c_str()) != 0) { close(); return false; }
    addr.port = port;
    peer_ = enet_host_connect(host_, &addr, kChannelCount, 0);
    if (!peer_) { close(); return false; }
    return true;
}

void NetClient::disconnect(int waitMs) {
    if (!host_) return;
    if (peer_ && connected_) {
        enet_peer_disconnect(peer_, kDisconnectLeft);
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitMs);
        ENetEvent ev;
        while (std::chrono::steady_clock::now() < until) {
            int r = enet_host_service(host_, &ev, 10);
            if (r > 0 && ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
            if (r > 0 && ev.type == ENET_EVENT_TYPE_DISCONNECT) { peer_ = nullptr; break; }
        }
        if (peer_) enet_peer_reset(peer_);
    }
    close();
}

bool NetClient::sendPose(const Pose& pose) {
    if (!peer_ || !welcomed()) return false;
    PoseMsg m{++seq_, pose};
    return sendMessage(peer_, m);
}

bool NetClient::sendRawForTest(const Bytes& b, Channel ch, bool reliable) {
    return peer_ && connected_ && sendRaw(peer_, b, ch, reliable);
}

void NetClient::poll(int timeoutMs, std::vector<ClientEvent>& out) {
    if (!host_) return;
    ENetEvent ev;
    int r = enet_host_service(host_, &ev, static_cast<enet_uint32>(std::max(timeoutMs, 0)));
    while (r > 0) {
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT:
            connected_ = true;
            if (!validName(name_) || !sendMessage(peer_, Hello{version_, name_})) {
                // An invalid name cannot be encoded: send the raw form so the server rejects it.
                Bytes b{static_cast<std::uint8_t>(Type::Hello), static_cast<std::uint8_t>(version_), static_cast<std::uint8_t>(version_ >> 8),
                        static_cast<std::uint8_t>(name_.size()), static_cast<std::uint8_t>(name_.size() >> 8)};
                b.insert(b.end(), name_.begin(), name_.end());
                sendRaw(peer_, b, Channel::Reliable, true);
            }
            break;
        case ENET_EVENT_TYPE_RECEIVE: {
            auto m = decodePacket(ev.packet, ev.channelID);
            enet_packet_destroy(ev.packet);
            if (m) handle(*m, out); else ++dropped_;
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
            ClientEvent e{ClientEvent::Kind::Disconnected};
            out.push_back(e);
            peer_ = nullptr;
            connected_ = false;
            playerId_ = 0;
            known_.clear();
            break;
        }
        default: break;
        }
        if (!host_) break;
        r = enet_host_check_events(host_, &ev);
    }
    if (host_) enet_host_flush(host_);
}

void NetClient::handle(const Message& m, std::vector<ClientEvent>& out) {
    if (!welcomed()) {
        if (auto* w = std::get_if<Welcome>(&m)) {
            playerId_ = w->playerId;
            ClientEvent e{ClientEvent::Kind::Welcome};
            e.playerId = w->playerId;
            out.push_back(e);
        } else if (auto* rj = std::get_if<Reject>(&m)) {
            ClientEvent e{ClientEvent::Kind::Rejected};
            e.reject = rj->reason;
            out.push_back(e);
        } else {
            ++dropped_;
        }
        return;
    }
    if (auto* j = std::get_if<PlayerJoined>(&m)) {
        if (j->playerId == playerId_ || !known_.insert(j->playerId).second) { ++dropped_; return; }
        ClientEvent e{ClientEvent::Kind::Joined};
        e.playerId = j->playerId;
        e.name = j->name;
        out.push_back(e);
    } else if (auto* l = std::get_if<PlayerLeft>(&m)) {
        if (known_.erase(l->playerId) == 0) { ++dropped_; return; }
        ClientEvent e{ClientEvent::Kind::Left};
        e.playerId = l->playerId;
        e.leave = l->reason;
        out.push_back(e);
    } else if (auto* s = std::get_if<Snapshot>(&m)) {
        // Snapshots (unreliable channel) are not ordered against PlayerJoined/Left (reliable
        // channel): keep only entries of players currently announced. Ids are never reused, so a
        // player that left never comes back under the same id.
        ClientEvent e{ClientEvent::Kind::Snapshot};
        e.snapshot.tick = s->tick;
        for (const auto& en : s->entries)
            if (known_.count(en.playerId)) e.snapshot.entries.push_back(en);
        out.push_back(std::move(e));
    } else {
        ++dropped_;   // Welcome/Reject after Welcome, or a client-to-server type
    }
}

} // namespace kp::net
