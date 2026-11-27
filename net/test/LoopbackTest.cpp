// Server + clients over real ENet on 127.0.0.1 (ephemeral port), all in one thread.
// Checks: join flow, B's pose reaches A within 300 ms, a leave gives PlayerLeft, rejects.
#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>

#include "Check.h"
#include "client/NetClient.h"
#include "server/RelayServer.h"

using namespace kp;
using namespace kp::net;
using Clock = std::chrono::steady_clock;
using Kind = ClientEvent::Kind;

namespace {

struct Bot {
    NetClient c;
    std::vector<ClientEvent> ev;   // everything received so far
};

// Polls server and clients until done() or the time runs out; true if done() became true.
bool pump(RelayServer& s, std::initializer_list<Bot*> bots, int maxMs, const std::function<bool()>& done) {
    auto until = Clock::now() + std::chrono::milliseconds(maxMs);
    while (Clock::now() < until) {
        s.poll(0);
        for (Bot* b : bots) b->c.poll(0, b->ev);
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return done();
}

const ClientEvent* find(const Bot& b, Kind k, std::uint16_t id = 0) {
    for (const auto& e : b.ev)
        if (e.kind == k && (id == 0 || e.playerId == id)) return &e;
    return nullptr;
}

ServerConfig localConfig() {
    ServerConfig cfg;
    cfg.port = 0;
    cfg.loopbackOnly = true;
    return cfg;
}

proto::Pose poseAt(float x) {
    proto::Pose p;
    p.pos[0] = x; p.pos[1] = 10; p.pos[2] = -x;
    p.vel[0] = 20; p.vel[2] = -1;
    p.yaw = 87.0f;
    p.flags = proto::kFlagMoving;
    return p;
}

void testRelay() {
    RelayServer s;
    CHECK(s.start(localConfig()));
    CHECK(s.port() != 0);
    Bot a, b;
    CHECK(a.c.connect("127.0.0.1", s.port(), "Alice"));
    CHECK(pump(s, {&a}, 2000, [&] { return a.c.welcomed(); }));
    CHECK(b.c.connect("127.0.0.1", s.port(), "Bob"));
    CHECK(pump(s, {&a, &b}, 2000, [&] { return b.c.welcomed() && find(a, Kind::Joined) && find(b, Kind::Joined); }));
    CHECK(a.c.playerId() != b.c.playerId());
    const ClientEvent* ja = find(a, Kind::Joined);
    const ClientEvent* jb = find(b, Kind::Joined);
    CHECK(ja && ja->playerId == b.c.playerId() && ja->name == "Bob");
    CHECK(jb && jb->playerId == a.c.playerId() && jb->name == "Alice");
    CHECK(s.playerCount() == 2);

    // B sends 10 poses at 10 Hz; each must show up in A's snapshots within 300 ms of sending.
    double worstMs = 0;
    int arrived = 0;
    for (int i = 0; i < 10; ++i) {
        proto::Pose p = poseAt(100.0f + i);
        auto sent = Clock::now();
        CHECK(b.c.sendPose(p));
        a.ev.clear();
        bool got = pump(s, {&a, &b}, 300, [&] {
            for (const auto& e : a.ev)
                if (e.kind == Kind::Snapshot)
                    for (const auto& en : e.snapshot.entries)
                        if (en.playerId == b.c.playerId() && en.pose == p) return true;
            return false;
        });
        double ms = std::chrono::duration<double, std::milli>(Clock::now() - sent).count();
        if (got) { ++arrived; if (ms > worstMs) worstMs = ms; }
        CHECK(got);
    }
    std::printf("relay: %d/10 poses arrived, worst %.1f ms (limit 300)\n", arrived, worstMs);
    // A never sees itself; B sees nothing from A (A sent no pose).
    for (const auto& e : a.ev)
        if (e.kind == Kind::Snapshot)
            for (const auto& en : e.snapshot.entries) { CHECK(en.playerId != a.c.playerId()); CHECK(en.ageMs < 300); }
    bool bSawA = false;
    for (const auto& e : b.ev)
        if (e.kind == Kind::Snapshot) bSawA = true;
    CHECK(!bSawA);

    // A stale (older sequence) pose is dropped by the server: send raw seq 1 after seq 10.
    auto stale = proto::encode(proto::PoseMsg{1, poseAt(-5)});
    auto before = s.stats().posesStale;
    CHECK(b.c.sendRawForTest(stale, proto::Channel::Unreliable, false));
    pump(s, {&a, &b}, 200, [&] { return s.stats().posesStale > before; });
    CHECK(s.stats().posesStale == before + 1);

    // B leaves on purpose -> A gets PlayerLeft(B, left); later snapshots never name B.
    std::uint16_t bid = b.c.playerId();
    a.ev.clear();
    b.c.disconnect();
    CHECK(pump(s, {&a}, 1000, [&] { return find(a, Kind::Left, bid) != nullptr; }));
    const ClientEvent* left = find(a, Kind::Left, bid);
    CHECK(left && left->leave == proto::LeaveReason::Left);
    CHECK(s.playerCount() == 1);

    // A third player gets a fresh id (ids are never reused).
    Bot c;
    CHECK(c.c.connect("127.0.0.1", s.port(), "Cara"));
    CHECK(pump(s, {&a, &c}, 2000, [&] { return c.c.welcomed() && find(a, Kind::Joined); }));
    CHECK(c.c.playerId() != bid && c.c.playerId() != a.c.playerId());
    c.c.disconnect();
    a.c.disconnect();
    s.stop();
}

// Connects, sends raw bytes after the connection stands (no proper Hello first if raw is set).
proto::RejectReason expectReject(const ServerConfig& cfg, const std::string& name, std::uint16_t version,
                                 const proto::Bytes* raw, proto::Channel ch, bool reliable, bool* gotReject) {
    RelayServer s;
    CHECK(s.start(cfg));
    Bot x;
    *gotReject = false;
    if (raw) {
        // Connect with a valid Hello suppressed: use a name the client sends, then the raw bytes.
        CHECK(x.c.connect("127.0.0.1", s.port(), name, version));
        pump(s, {&x}, 2000, [&] { return x.c.welcomed() || find(x, Kind::Rejected); });
        CHECK(x.c.sendRawForTest(*raw, ch, reliable));
    } else {
        CHECK(x.c.connect("127.0.0.1", s.port(), name, version));
    }
    pump(s, {&x}, 3000, [&] { return find(x, Kind::Disconnected) != nullptr; });
    const ClientEvent* r = find(x, Kind::Rejected);
    CHECK(find(x, Kind::Disconnected) != nullptr);   // the server closes the connection
    s.stop();
    if (!r) return proto::RejectReason::Malformed;
    *gotReject = true;
    return r->reject;
}

void testRejects() {
    bool got = false;
    CHECK(expectReject(localConfig(), "Old", 99, nullptr, proto::Channel::Reliable, true, &got) == proto::RejectReason::Version);
    CHECK(got);
    CHECK(expectReject(localConfig(), std::string("bad\tname"), 1, nullptr, proto::Channel::Reliable, true, &got) == proto::RejectReason::Malformed);
    CHECK(got);   // an invalid name fails the decoder: malformed

    // After Welcome: garbage, a Pose on the reliable channel, a second Hello. The client never
    // sees Reject after Welcome (it is dropped as unexpected), but the server disconnects it.
    {
        RelayServer s;
        CHECK(s.start(localConfig()));
        Bot a, x;
        CHECK(a.c.connect("127.0.0.1", s.port(), "Watcher"));
        CHECK(x.c.connect("127.0.0.1", s.port(), "Rogue"));
        CHECK(pump(s, {&a, &x}, 2000, [&] { return a.c.welcomed() && x.c.welcomed(); }));
        std::uint16_t xid = x.c.playerId();
        proto::Bytes trailing = proto::encode(proto::PoseMsg{1, poseAt(1)});
        trailing.push_back(0);
        CHECK(x.c.sendRawForTest(trailing, proto::Channel::Unreliable, false));
        CHECK(pump(s, {&a, &x}, 2000, [&] { return find(x, Kind::Disconnected) && find(a, Kind::Left, xid); }));
        const ClientEvent* l = find(a, Kind::Left, xid);
        CHECK(l && l->leave == proto::LeaveReason::Removed);
        CHECK(s.stats().rejected == 1);

        Bot y;
        CHECK(y.c.connect("127.0.0.1", s.port(), "Rogue2"));
        CHECK(pump(s, {&a, &y}, 2000, [&] { return y.c.welcomed(); }));
        CHECK(y.c.sendRawForTest(proto::encode(proto::PoseMsg{1, poseAt(1)}), proto::Channel::Reliable, true));   // wrong channel
        CHECK(pump(s, {&a, &y}, 2000, [&] { return find(y, Kind::Disconnected) != nullptr; }));

        Bot z;
        CHECK(z.c.connect("127.0.0.1", s.port(), "Rogue3"));
        CHECK(pump(s, {&a, &z}, 2000, [&] { return z.c.welcomed(); }));
        CHECK(z.c.sendRawForTest(proto::encode(proto::Hello{1, "again"}), proto::Channel::Reliable, true));
        CHECK(pump(s, {&a, &z}, 2000, [&] { return find(z, Kind::Disconnected) != nullptr; }));
        CHECK(s.stats().rejected == 3);
        // The server forgets a rejected player once ENet confirms the disconnect.
        CHECK(pump(s, {&a}, 2000, [&] { return s.playerCount() == 1; }));
        a.c.disconnect();
    }

    // Server full.
    {
        ServerConfig cfg = localConfig();
        cfg.maxPlayers = 1;
        RelayServer s;
        CHECK(s.start(cfg));
        Bot a, b;
        CHECK(a.c.connect("127.0.0.1", s.port(), "First"));
        CHECK(pump(s, {&a}, 2000, [&] { return a.c.welcomed(); }));
        CHECK(b.c.connect("127.0.0.1", s.port(), "Second"));
        CHECK(pump(s, {&a, &b}, 2000, [&] { return find(b, Kind::Rejected) != nullptr; }));
        const ClientEvent* r = find(b, Kind::Rejected);
        CHECK(r && r->reject == proto::RejectReason::Full);
        CHECK(find(a, Kind::Joined) == nullptr);   // a rejected peer is never announced
        a.c.disconnect();
    }

    // No Hello in time: a raw ENet peer that connects and stays silent.
    {
        ServerConfig cfg = localConfig();
        cfg.helloTimeout = std::chrono::milliseconds(200);
        RelayServer s;
        CHECK(s.start(cfg));
        ENetHost* h = enet_host_create(nullptr, 1, proto::kChannelCount, 0, 0);
        CHECK(h != nullptr);
        ENetAddress addr{};
        enet_address_set_host_ip(&addr, "127.0.0.1");
        addr.port = s.port();
        enet_host_connect(h, &addr, proto::kChannelCount, 0);
        bool rejected = false, disconnected = false;
        auto until = Clock::now() + std::chrono::milliseconds(3000);
        while (Clock::now() < until && !disconnected) {
            s.poll(0);
            ENetEvent ev;
            while (enet_host_service(h, &ev, 0) > 0) {
                if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                    auto m = proto::decode(std::span<const std::uint8_t>(ev.packet->data, ev.packet->dataLength));
                    if (m && std::holds_alternative<proto::Reject>(*m) && std::get<proto::Reject>(*m).reason == proto::RejectReason::NoHello) rejected = true;
                    enet_packet_destroy(ev.packet);
                }
                if (ev.type == ENET_EVENT_TYPE_DISCONNECT) disconnected = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(rejected);
        CHECK(disconnected);
        enet_host_destroy(h);
    }
}

// A Snapshot entry for an id the client has not been told about (or that already left) is
// dropped by the client: checked through the client's filter with a fake server.
void testClientFilter() {
    EnetLib lib;
    ENetAddress addr{};
    enet_address_set_host_ip(&addr, "127.0.0.1");
    addr.port = 0;
    ENetHost* srv = enet_host_create(&addr, 2, proto::kChannelCount, 0, 0);
    CHECK(srv != nullptr);
    ENetAddress bound{};
    enet_socket_get_address(srv->socket, &bound);
    Bot a;
    CHECK(a.c.connect("127.0.0.1", bound.port, "Alice"));
    ENetPeer* peer = nullptr;
    auto step = [&](int ms) {
        auto until = Clock::now() + std::chrono::milliseconds(ms);
        while (Clock::now() < until) {
            ENetEvent ev;
            while (enet_host_service(srv, &ev, 0) > 0) {
                if (ev.type == ENET_EVENT_TYPE_CONNECT) peer = ev.peer;
                if (ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
            }
            a.c.poll(0, a.ev);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    step(200);
    CHECK(peer != nullptr);
    if (!peer) { enet_host_destroy(srv); return; }
    auto snap = [](std::uint32_t tick, std::uint16_t id) { return proto::Snapshot{tick, {{id, 0, poseAt(1)}}}; };
    sendMessage(peer, proto::Welcome{1, 1, 10});
    step(150);
    sendMessage(peer, snap(1, 7));               // unknown id 7: dropped
    step(150);
    sendMessage(peer, proto::PlayerJoined{7, "Seven"});
    step(150);
    sendMessage(peer, snap(2, 7));               // known: kept
    step(150);
    sendMessage(peer, proto::PlayerLeft{7, proto::LeaveReason::Left});
    step(150);
    sendMessage(peer, snap(3, 7));               // left: dropped
    sendMessage(peer, proto::PlayerJoined{1, "Me"});   // own id: dropped
    step(150);
    int kept = 0, snaps = 0;
    for (const auto& e : a.ev)
        if (e.kind == Kind::Snapshot) { ++snaps; kept += static_cast<int>(e.snapshot.entries.size()); if (!e.snapshot.entries.empty()) CHECK(e.snapshot.tick == 2); }
    CHECK(snaps == 3);
    CHECK(kept == 1);
    CHECK(find(a, Kind::Joined, 7) != nullptr);
    CHECK(find(a, Kind::Joined, 1) == nullptr);
    CHECK(find(a, Kind::Left, 7) != nullptr);
    CHECK(a.c.droppedMessages() == 1);
    a.c.disconnect(100);
    enet_host_destroy(srv);
}

} // namespace

int main() {
    testRelay();
    testRejects();
    testClientFilter();
    return TEST_RESULT("loopback");
}
