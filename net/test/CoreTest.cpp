// KenshiPureCore.dll through its C interface (client/KpCore.h), driven by the plugin's own frame side
// (client/NetFrame.cpp) with a fake game, against the real relay server (RelayServer on its own
// thread) and an observer bot (NetClient on its own thread). Checks:
//   - bad configs are refused; KenshiPure.cfg parsing;
//   - the first samples after a load are dropped until the destination is set;
//   - poses flow both ways;
//   - a 60 s stall of the frame thread keeps the connection (same id, no leave), while an unserviced
//     client next to it is timed out by the server (negative control: ENet's timeout is real);
//   - the server going away gives "lost" and a reconnect with a new id;
//   - an address where nobody listens gives "connect failed" and retries.
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Check.h"
#include "KpCore.h"
#include "NetFrame.h"
#include "client/NetClient.h"
#include "server/RelayServer.h"

using namespace kp;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace {

constexpr float kUnsettledX = -1234.0f;   // marks samples that must never leave the client

unsigned long long nowMs() {
    return static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
}

// Log lines from the core's thread and the frame side, searchable.
struct Lines {
    std::mutex m;
    std::vector<std::string> v;
    void add(const char* s) {
        std::printf("  | %s\n", s);
        std::lock_guard<std::mutex> lock(m);
        v.emplace_back(s);
    }
    bool has(const char* part) {
        std::lock_guard<std::mutex> lock(m);
        for (const auto& s : v) if (s.find(part) != std::string::npos) return true;
        return false;
    }
};
Lines g_lines;
void coreLog(void*, const char* line) { g_lines.add(line); }

// The fake game: a character walking in +x, sampled at 10 Hz like client/Pose.cpp.
struct FakeGame : IGameAccess {
    unsigned epoch = 1;
    int unsettledLeft = 3;           // first samples of an epoch: destination 0/0/0, marker x
    unsigned long long lastSample = 0, tick = 0;
    float x = 100.0f;
    bool LocalPose(NetLocalSample* out) override {
        unsigned long long t = nowMs();
        if (t - lastSample >= 100) {
            lastSample = t;
            tick = t;
            x += 7.0f;
            if (unsettledLeft > 0) --unsettledLeft;
        }
        std::memset(out, 0, sizeof(*out));
        bool unsettled = unsettledLeft > 0;
        out->pos[0] = unsettled ? kUnsettledX : x; out->pos[1] = 20; out->pos[2] = 300;
        out->vel[0] = 70;
        out->yawDeg = 90;
        if (!unsettled) { out->dest[0] = x + 500; out->dest[1] = 20; out->dest[2] = 300; }
        out->moving = 1;
        out->tick = tick;
        out->epoch = epoch;
        return true;
    }
    void Log(const char* line) override { g_lines.add(line); }
};

struct ServerThread {
    net::RelayServer s;
    std::thread t;
    std::atomic<bool> stop{false};
    bool start(std::uint16_t port) {
        net::ServerConfig cfg;
        cfg.port = port;
        cfg.loopbackOnly = true;
        cfg.log = [](std::string_view l) { std::string s(l); s = "server: " + s; g_lines.add(s.c_str()); };
        if (!s.start(cfg)) return false;
        stop = false;
        t = std::thread([this] { while (!stop) s.poll(2); });
        return true;
    }
    void shutdown() {
        stop = true;
        if (t.joinable()) t.join();
        s.stop();
    }
};

// A bot on its own thread: walks, records what it sees of the others.
struct Observer {
    net::NetClient c;
    std::thread t;
    std::atomic<bool> stop{false};
    std::mutex m;
    std::uint16_t myId = 0;
    std::vector<net::ClientEvent> left;
    std::uint16_t watch = 0;          // player whose entries are recorded
    unsigned maxAge = 0, lastAge = 0, entries = 0;
    bool sawUnsettled = false;
    float lastX = 0;
    void run(std::uint16_t port) {
        c.connect("127.0.0.1", port, "observer");
        t = std::thread([this] {
            auto next = Clock::now();
            std::vector<net::ClientEvent> ev;
            float a = 0;
            while (!stop) {
                ev.clear();
                c.poll(2, ev);
                std::lock_guard<std::mutex> lock(m);
                for (auto& e : ev) {
                    if (e.kind == net::ClientEvent::Kind::Welcome) myId = e.playerId;
                    if (e.kind == net::ClientEvent::Kind::Left) left.push_back(e);
                    if (e.kind == net::ClientEvent::Kind::Snapshot)
                        for (auto& en : e.snapshot.entries) {
                            if (en.pose.pos[0] == kUnsettledX) sawUnsettled = true;
                            if (en.playerId != watch) continue;
                            ++entries;
                            lastAge = en.ageMs;
                            if (en.ageMs > maxAge) maxAge = en.ageMs;
                            lastX = en.pose.pos[0];
                        }
                }
                if (c.welcomed() && Clock::now() >= next) {
                    next += 100ms;
                    a += 0.1f;
                    proto::Pose p;
                    p.pos[0] = 50 * std::sin(a); p.pos[1] = 5; p.pos[2] = 50 * std::cos(a);
                    p.yaw = 45;
                    p.flags = proto::kFlagMoving;
                    c.sendPose(p);
                }
            }
            c.disconnect(200);
        });
    }
    void shutdown() { stop = true; if (t.joinable()) t.join(); }
};

// Runs the frame side like the game's frame hook (~60 frames/s) for ms, or until done().
template <class F>
bool frames(NetFrame& nf, int ms, F done) {
    auto until = Clock::now() + std::chrono::milliseconds(ms);
    while (Clock::now() < until) {
        nf.OnFrame(nowMs());
        if (done()) return true;
        std::this_thread::sleep_for(16ms);
    }
    return done();
}
bool frames(NetFrame& nf, int ms) { return frames(nf, ms, [] { return false; }); }

KpCoreConfig config(std::uint16_t port, const char* name = "tester") {
    KpCoreConfig cfg{};
    cfg.size = sizeof(cfg);
    cfg.version = KP_CORE_VERSION;
    strcpy_s(cfg.host, "127.0.0.1");
    cfg.port = port;
    strcpy_s(cfg.name, name);
    cfg.log = coreLog;
    return cfg;
}

const KpCoreApi* loadCore() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) slash[1] = 0;
    wcscat_s(path, L"KenshiPureCore.dll");
    HMODULE h = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) { std::printf("cannot load %ls (%lu)\n", path, GetLastError()); return nullptr; }
    auto get = reinterpret_cast<KpCoreGetApiFn>(GetProcAddress(h, "kpCoreGetApi"));
    if (!get) return nullptr;
    if (get(KP_CORE_VERSION + 1) != nullptr) { std::printf("core accepts an unknown version\n"); return nullptr; }
    return get(KP_CORE_VERSION);
}

void testConfig(const KpCoreApi* api) {
    KpCoreConfig c;
    CHECK(!NetParseConfig("probe_write=1\n", &c));
    CHECK(!NetParseConfig("host=\nport=1\n", &c));
    CHECK(NetParseConfig("# comment\r\nprobe_write=1\r\nhost = 10.0.0.5 \r\nport=1234\r\nname= Gecko \r\n", &c));
    CHECK(std::strcmp(c.host, "10.0.0.5") == 0);
    CHECK(c.port == 1234);
    CHECK(std::strcmp(c.name, "Gecko") == 0);
    CHECK(NetParseConfig("host=a", &c) && c.port == 27850 && std::strcmp(c.name, "player") == 0);
    CHECK(NetParseConfig("host=a\nport=99999\n", &c) && c.port == 0);
    CHECK(NetParseConfig("host=a\nname=0123456789012345678901234567890123\n", &c) && c.name[0] == 0);

    KpCoreConfig bad = config(1);
    bad.host[0] = 0;
    CHECK(api->start(&bad) == nullptr);
    bad = config(0);
    CHECK(api->start(&bad) == nullptr);
    bad = config(1, "");
    CHECK(api->start(&bad) == nullptr);
    bad = config(1);
    bad.name[5] = '\x01';
    CHECK(api->start(&bad) == nullptr);
    bad = config(1);
    bad.version = 99;
    CHECK(api->start(&bad) == nullptr);
    KpPose nan{};
    nan.yaw = std::numeric_limits<float>::quiet_NaN();
    CHECK(api->setLocalPose(nullptr, &nan) == 0);
}

KpCoreStatus status(const KpCoreApi* api, NetFrame& nf, KpCore* core) {
    (void)nf;
    KpCoreStatus st{};
    api->status(core, &st);
    return st;
}

} // namespace

// NetFrame does not hand out its core; the test keeps its own copy of the pointer by wrapping start.
namespace {
const KpCoreApi* g_real = nullptr;
KpCore* g_lastCore = nullptr;
KpCore* wrapStart(const KpCoreConfig* cfg) { g_lastCore = g_real->start(cfg); return g_lastCore; }
}

int main() {
    const KpCoreApi* real = loadCore();
    CHECK(real != nullptr);
    if (!real) return TEST_RESULT("kp-core-test");
    std::printf("%s\n", real->build);
    testConfig(real);
    g_real = real;
    KpCoreApi api = *real;
    api.start = wrapStart;

    ServerThread server;
    CHECK(server.start(0));
    std::uint16_t port = server.s.port();

    // A second core aimed at a port where nobody listens: runs through the whole test.
    KpCoreConfig deadCfg = config(static_cast<std::uint16_t>(port + 1), "nobody");
    KpCore* dead = real->start(&deadCfg);
    CHECK(dead != nullptr);

    FakeGame game;
    NetFrame nf;
    KpCoreConfig cfg = config(port);
    CHECK(nf.Start(&api, cfg, &game));
    KpCore* core = g_lastCore;

    Observer obs;
    obs.run(port);

    // 1. Welcome, unsettled samples dropped, poses both ways.
    CHECK(frames(nf, 5000, [&] { return status(real, nf, core).state == KP_CORE_WELCOMED; }));
    std::uint16_t myId = static_cast<std::uint16_t>(status(real, nf, core).playerId);
    CHECK(myId != 0);
    { std::lock_guard<std::mutex> lock(obs.m); obs.watch = myId; }
    CHECK(frames(nf, 3000, [&] {
        KpRemotePose rp;
        std::lock_guard<std::mutex> lock(obs.m);
        return obs.entries > 5 && obs.myId != 0 && nf.Remote(obs.myId, &rp);
    }));
    CHECK(nf.Stats().posesUnsettled >= 2);
    CHECK(nf.Stats().posesSent > 5);
    CHECK(g_lines.has("net: remote observer"));
    CHECK(g_lines.has("player") && g_lines.has("joined: observer"));
    {
        std::lock_guard<std::mutex> lock(obs.m);
        CHECK(!obs.sawUnsettled);
        CHECK(obs.lastX > 100.0f);
        CHECK(obs.lastAge < 1000);
    }

    // 2. A 60 s stall of the frame thread (Kenshi loading). Next to it an unserviced client.
    net::NetClient ghost;
    CHECK(ghost.connect("127.0.0.1", port, "ghost"));
    {
        std::vector<net::ClientEvent> ev;
        auto until = Clock::now() + 3s;
        while (!ghost.welcomed() && Clock::now() < until) ghost.poll(5, ev);
        CHECK(ghost.welcomed());
    }
    std::uint16_t ghostId = ghost.playerId();
    unsigned sentBefore = status(real, nf, core).posesSent;
    std::printf("stall: frame thread sleeps 60 s (the ghost client is not serviced at all)\n");
    std::this_thread::sleep_for(60s);
    KpCoreStatus st = status(real, nf, core);
    CHECK(st.state == KP_CORE_WELCOMED);
    CHECK(st.playerId == myId);
    CHECK(st.attempts == 1);
    CHECK(st.posesSent == sentBefore);   // nothing new to send during the stall
    {
        std::lock_guard<std::mutex> lock(obs.m);
        std::printf("stall: observer saw our pose age up to %u ms\n", obs.maxAge);
        CHECK(obs.maxAge >= 55000);
        bool weLeft = false, ghostLeft = false;
        for (auto& e : obs.left) {
            if (e.playerId == myId) weLeft = true;
            if (e.playerId == ghostId && e.leave == proto::LeaveReason::TimedOut) ghostLeft = true;
        }
        CHECK(!weLeft);
        CHECK(ghostLeft);   // ENet's timeout does fire for a client that stops servicing
    }
    CHECK(frames(nf, 3000, [&] { std::lock_guard<std::mutex> lock(obs.m); return obs.lastAge < 500; }));
    CHECK(!g_lines.has("connection lost"));

    // Meanwhile the core aimed at nobody failed and kept trying.
    {
        KpEvent ev[64];
        int n = real->takeEvents(dead, ev, 64);
        int fails = 0, attempts = 0;
        for (int i = 0; i < n; ++i) {
            if (ev[i].kind == KP_EV_CONNECT_FAILED) ++fails;
            if (ev[i].kind == KP_EV_CONNECTING) ++attempts;
        }
        std::printf("dead server: %d attempts, %d failures in ~70 s\n", attempts, fails);
        CHECK(fails >= 2);
        CHECK(attempts >= 3);
        KpCoreStatus ds{};
        real->status(dead, &ds);
        CHECK(ds.state != KP_CORE_WELCOMED);
        CHECK(g_lines.has("no Welcome within 10 s") || g_lines.has("no answer from the server"));
        real->stop(dead);
    }

    // 3. The server goes away and comes back on the same port: lost, then a new id.
    obs.shutdown();
    server.shutdown();
    // The core sets its state before it logs the loss (network thread): wait for both, not only the state.
    CHECK(frames(nf, 3000, [&] { return status(real, nf, core).state != KP_CORE_WELCOMED && g_lines.has("connection lost"); }));
    ServerThread server2;
    CHECK(server2.start(port));
    CHECK(frames(nf, 15000, [&] { return status(real, nf, core).state == KP_CORE_WELCOMED; }));
    st = status(real, nf, core);
    CHECK(st.attempts >= 2);
    CHECK(st.playerId != 0);

    // 4. Stop leaves on purpose: the server logs "left".
    auto t0 = Clock::now();
    nf.Stop();
    auto stopMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
    std::printf("stop took %lld ms\n", static_cast<long long>(stopMs));
    CHECK(stopMs < 1500);
    // The server logs the leave on its own thread: wait up to 3 s for our own player's line (the
    // observer's earlier leave also says "left (left)").
    char leftLine[64];
    std::snprintf(leftLine, sizeof(leftLine), "server: player %u left (left)", static_cast<unsigned>(st.playerId));
    for (int i = 0; i < 300 && !g_lines.has(leftLine); ++i) std::this_thread::sleep_for(10ms);
    CHECK(g_lines.has(leftLine));
    server2.shutdown();
    return TEST_RESULT("kp-core-test");
}
