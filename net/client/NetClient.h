// Client side of the KenshiPure protocol: connect, Hello, send poses, receive events.
// Single-threaded: call poll() in a loop (the game client will run it on its own network thread).
#pragma once
#include <chrono>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "../common/Enet.h"

namespace kp::net {

struct ClientEvent {
    enum class Kind { Welcome, Rejected, Joined, Left, Snapshot, Disconnected };
    Kind kind;
    std::uint16_t playerId = 0;                 // Welcome: own id; Joined/Left: that player
    std::string name;                           // Joined
    proto::RejectReason reject = proto::RejectReason::Malformed;
    proto::LeaveReason leave = proto::LeaveReason::Left;
    proto::Snapshot snapshot;                   // only entries of players announced by PlayerJoined
};

class NetClient {
public:
    NetClient() = default;
    ~NetClient();
    NetClient(const NetClient&) = delete;
    NetClient& operator=(const NetClient&) = delete;

    // Starts connecting; Hello goes out as soon as the connection stands. False if the address
    // cannot be resolved or ENet fails.
    bool connect(const std::string& host, std::uint16_t port, const std::string& name,
                 std::uint16_t version = proto::kProtocolVersion);
    // Leaves on purpose (server reports PlayerLeft "left"); waits up to waitMs for the ack.
    void disconnect(int waitMs = 500);

    // Services the network for up to timeoutMs; appends what happened to out.
    void poll(int timeoutMs, std::vector<ClientEvent>& out);

    // Queues a pose (only once welcomed); fills in the sequence number.
    bool sendPose(const proto::Pose& pose);
    // Tests only: send any bytes.
    bool sendRawForTest(const proto::Bytes& b, proto::Channel ch, bool reliable);

    bool connected() const { return connected_; }
    bool welcomed() const { return playerId_ != 0; }
    std::uint16_t playerId() const { return playerId_; }
    std::uint64_t droppedMessages() const { return dropped_; }

private:
    void handle(const proto::Message& m, std::vector<ClientEvent>& out);
    void close();

    EnetLib lib_;
    ENetHost* host_ = nullptr;
    ENetPeer* peer_ = nullptr;
    std::string name_;
    std::uint16_t version_ = proto::kProtocolVersion;
    bool connected_ = false;
    std::uint16_t playerId_ = 0;
    std::uint32_t seq_ = 0;
    std::uint64_t dropped_ = 0;
    std::set<std::uint16_t> known_;   // announced by PlayerJoined and not left
};

} // namespace kp::net
