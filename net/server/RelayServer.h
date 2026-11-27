// The relay server: accepts players, keeps each one's latest pose and sends every player a
// Snapshot of the others at a fixed rate. Not authoritative (it checks format, not plausibility).
// Single-threaded: call poll() in a loop.
#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "../common/Enet.h"

namespace kp::net {

struct ServerConfig {
    std::uint16_t port = proto::kDefaultPort;   // 0 = any free port (tests)
    bool loopbackOnly = false;                  // bind 127.0.0.1 instead of all interfaces
    std::size_t maxPlayers = proto::kMaxPlayers;
    std::uint16_t rateHz = 10;
    std::chrono::milliseconds helloTimeout{5000};
    std::function<void(std::string_view)> log;  // optional
    std::chrono::milliseconds poseLogEvery{0};  // > 0: log each player's pose at most this often
};

struct ServerStats {
    std::uint64_t snapshotsSent = 0;
    std::uint64_t posesAccepted = 0;
    std::uint64_t posesStale = 0;
    std::uint64_t rejected = 0;
};

class RelayServer {
public:
    RelayServer() = default;
    ~RelayServer();
    RelayServer(const RelayServer&) = delete;
    RelayServer& operator=(const RelayServer&) = delete;

    bool start(const ServerConfig& cfg);
    void stop();
    // Services the network for up to timeoutMs and sends snapshots when due.
    void poll(int timeoutMs);

    std::uint16_t port() const { return port_; }
    std::size_t playerCount() const;
    const ServerStats& stats() const { return stats_; }

private:
    using Clock = std::chrono::steady_clock;
    struct Player {
        std::uint16_t id = 0;     // 0 until welcomed
        std::string name;
        Clock::time_point connectedAt;
        bool closing = false;     // Reject sent, waiting for the disconnect
        bool hasPose = false;
        std::uint32_t seq = 0;
        proto::Pose pose;
        Clock::time_point poseAt;   // when the latest pose arrived
        Clock::time_point poseLoggedAt;
        bool poseLogged = false;
    };

    void onConnect(ENetPeer* peer);
    void onReceive(ENetPeer* peer, ENetPacket* packet, enet_uint8 channel);
    void onDisconnect(ENetPeer* peer, enet_uint32 data);
    void reject(ENetPeer* peer, Player& p, proto::RejectReason why);
    void sendSnapshots();
    void checkHelloTimeouts();
    void broadcast(const proto::Message& m, ENetPeer* except);
    void log(const std::string& s) const;

    EnetLib lib_;
    ENetHost* host_ = nullptr;
    ServerConfig cfg_;
    std::uint16_t port_ = 0;
    std::uint16_t nextId_ = 1;
    std::uint32_t tick_ = 0;
    Clock::time_point nextTick_;
    std::unordered_map<ENetPeer*, Player> players_;
    ServerStats stats_;
};

} // namespace kp::net
