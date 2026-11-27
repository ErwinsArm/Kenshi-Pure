#include "RelayServer.h"

#include <cstdio>

#include <algorithm>
#include <vector>

namespace kp::net {

using namespace proto;

RelayServer::~RelayServer() { stop(); }

void RelayServer::log(const std::string& s) const {
    if (cfg_.log) cfg_.log(s);
}

bool RelayServer::start(const ServerConfig& cfg) {
    stop();
    cfg_ = cfg;
    if (!lib_.ok()) { log("ENet failed to initialise"); return false; }
    if (cfg_.rateHz < 1 || cfg_.rateHz > 60) cfg_.rateHz = 10;
    cfg_.maxPlayers = std::clamp<std::size_t>(cfg_.maxPlayers, 1, kMaxPlayers);
    ENetAddress addr{};
    addr.host = ENET_HOST_ANY;
    if (cfg_.loopbackOnly) enet_address_set_host_ip(&addr, "127.0.0.1");
    addr.port = cfg_.port;
    // Spare peer slots so a full server can still deliver "Reject: server full" (ENet itself
    // silently refuses connects beyond its peer count).
    host_ = enet_host_create(&addr, cfg_.maxPlayers + 16, kChannelCount, 0, 0);
    if (!host_) { log("cannot open UDP port " + std::to_string(cfg_.port)); return false; }
    ENetAddress bound{};
    port_ = (enet_socket_get_address(host_->socket, &bound) == 0) ? bound.port : cfg_.port;
    nextTick_ = Clock::now() + std::chrono::milliseconds(1000 / cfg_.rateHz);
    log("listening on UDP port " + std::to_string(port_) + (cfg_.loopbackOnly ? " (this PC only)" : ""));
    return true;
}

void RelayServer::stop() {
    if (!host_) return;
    for (auto& [peer, p] : players_) enet_peer_disconnect_now(peer, 0);
    players_.clear();
    enet_host_destroy(host_);
    host_ = nullptr;
}

std::size_t RelayServer::playerCount() const {
    return static_cast<std::size_t>(std::count_if(players_.begin(), players_.end(),
        [](const auto& kv) { return kv.second.id != 0; }));
}

void RelayServer::poll(int timeoutMs) {
    if (!host_) return;
    auto period = std::chrono::milliseconds(1000 / cfg_.rateHz);
    auto until = Clock::now() + std::chrono::milliseconds(std::max(timeoutMs, 0));
    for (;;) {
        auto now = Clock::now();
        if (now >= nextTick_) {
            sendSnapshots();
            nextTick_ += period;
            if (nextTick_ <= now) nextTick_ = now + period;   // fell behind: do not burst
        }
        checkHelloTimeouts();
        auto wait = std::min(until, nextTick_) - now;
        auto waitMs = std::max<long long>(0, std::chrono::duration_cast<std::chrono::milliseconds>(wait).count());
        ENetEvent ev;
        int r = enet_host_service(host_, &ev, static_cast<enet_uint32>(waitMs));
        while (r > 0) {
            switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT: onConnect(ev.peer); break;
            case ENET_EVENT_TYPE_RECEIVE: onReceive(ev.peer, ev.packet, ev.channelID); enet_packet_destroy(ev.packet); break;
            case ENET_EVENT_TYPE_DISCONNECT: onDisconnect(ev.peer, ev.data); break;
            default: break;
            }
            r = enet_host_check_events(host_, &ev);
        }
        if (Clock::now() >= until) break;
    }
    enet_host_flush(host_);
}

void RelayServer::onConnect(ENetPeer* peer) {
    Player p;
    p.connectedAt = Clock::now();
    players_[peer] = p;
}

void RelayServer::reject(ENetPeer* peer, Player& p, RejectReason why) {
    if (p.closing) return;
    sendMessage(peer, Reject{why, kProtocolVersion});
    enet_peer_disconnect_later(peer, 0);   // after the Reject is delivered
    p.closing = true;
    ++stats_.rejected;
    log("rejected a peer (reason " + std::to_string(static_cast<int>(why)) + ")");
}

void RelayServer::onReceive(ENetPeer* peer, ENetPacket* packet, enet_uint8 channel) {
    auto it = players_.find(peer);
    if (it == players_.end()) return;
    Player& p = it->second;
    if (p.closing) return;
    auto m = decodePacket(packet, channel);
    if (!m) { reject(peer, p, RejectReason::Malformed); return; }

    if (auto* h = std::get_if<Hello>(&*m)) {
        if (p.id != 0) { reject(peer, p, RejectReason::Malformed); return; }   // second Hello
        if (h->version != kProtocolVersion) { reject(peer, p, RejectReason::Version); return; }
        if (playerCount() >= cfg_.maxPlayers || nextId_ == 0) { reject(peer, p, RejectReason::Full); return; }
        p.id = nextId_++;   // wraps to 0 after 65535: then the server only says "full"
        p.name = h->name;
        sendMessage(peer, Welcome{kProtocolVersion, p.id, cfg_.rateHz});
        for (const auto& [other, q] : players_)
            if (other != peer && q.id != 0 && !q.closing) sendMessage(peer, PlayerJoined{q.id, q.name});
        broadcast(PlayerJoined{p.id, p.name}, peer);
        log("player " + std::to_string(p.id) + " joined: " + p.name);
        return;
    }
    if (p.id == 0) { reject(peer, p, RejectReason::NoHello); return; }   // anything before Hello
    if (auto* pm = std::get_if<PoseMsg>(&*m)) {
        if (p.hasPose && !seqNewer(pm->seq, p.seq)) { ++stats_.posesStale; return; }
        p.seq = pm->seq;
        p.pose = pm->pose;
        p.hasPose = true;
        p.poseAt = Clock::now();
        ++stats_.posesAccepted;
        if (cfg_.poseLogEvery.count() > 0 && (!p.poseLogged || p.poseAt - p.poseLoggedAt >= cfg_.poseLogEvery)) {
            p.poseLogged = true;
            p.poseLoggedAt = p.poseAt;
            char line[200];
            std::snprintf(line, sizeof(line), "pose: player %u (%s) pos %.1f %.1f %.1f yaw %.1f moving %d%s",
                          p.id, p.name.c_str(), p.pose.pos[0], p.pose.pos[1], p.pose.pos[2], p.pose.yaw,
                          (p.pose.flags & kFlagMoving) ? 1 : 0, (p.pose.flags & kFlagPaused) ? " paused" : "");
            log(line);
        }
        return;
    }
    reject(peer, p, RejectReason::Malformed);   // a server-to-client type
}

void RelayServer::onDisconnect(ENetPeer* peer, enet_uint32 data) {
    auto it = players_.find(peer);
    if (it == players_.end()) return;
    Player p = it->second;
    players_.erase(it);
    if (p.id == 0) return;
    LeaveReason why = (data == kDisconnectLeft) ? LeaveReason::Left : LeaveReason::TimedOut;
    if (p.closing) why = LeaveReason::Removed;
    broadcast(PlayerLeft{p.id, why}, nullptr);
    log("player " + std::to_string(p.id) + " left (" + (why == LeaveReason::Left ? "left" : why == LeaveReason::TimedOut ? "timed out" : "removed") + ")");
}

void RelayServer::checkHelloTimeouts() {
    auto now = Clock::now();
    for (auto& [peer, p] : players_)
        if (p.id == 0 && !p.closing && now - p.connectedAt > cfg_.helloTimeout) reject(peer, p, RejectReason::NoHello);
}

void RelayServer::broadcast(const Message& m, ENetPeer* except) {
    for (auto& [peer, p] : players_)
        if (peer != except && p.id != 0 && !p.closing) sendMessage(peer, m);
}

void RelayServer::sendSnapshots() {
    ++tick_;
    std::vector<SnapshotEntry> all;
    auto now = Clock::now();
    for (const auto& [peer, p] : players_) {
        if (p.id == 0 || p.closing || !p.hasPose) continue;
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - p.poseAt).count();
        all.push_back({p.id, static_cast<std::uint16_t>(std::clamp<long long>(age, 0, 65535)), p.pose});
    }
    if (all.empty()) return;
    std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.playerId < b.playerId; });
    for (auto& [peer, p] : players_) {
        if (p.id == 0 || p.closing) continue;
        Snapshot s;
        s.tick = tick_;
        for (const auto& e : all)
            if (e.playerId != p.id) s.entries.push_back(e);
        if (s.entries.empty()) continue;
        if (sendMessage(peer, s)) ++stats_.snapshotsSent;
    }
}

} // namespace kp::net
