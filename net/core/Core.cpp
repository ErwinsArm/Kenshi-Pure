// KenshiPureCore.dll: the client's network side behind the C interface in client/KpCore.h.
// One network thread per core owns the NetClient (the only thread that touches ENet); the caller
// (the game's frame thread) talks to it through a latest-wins pose slot, an event queue and a
// latest-pose-per-player table, each guarded by one mutex held only for copies.
// The thread keeps servicing ENet while the caller is stalled (Kenshi blocks its main thread ~60 s
// while loading), so ENet's peer timeout (5-30 s) is never reached by a stall of the game.
// Built with MSVC 14.44 and a static CRT; nothing of the C++ runtime crosses the interface.
#include "KpCore.h"   // client/KpCore.h (shared with the plugin)

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "client/NetClient.h"

#ifndef KP_CORE_BUILD
#define KP_CORE_BUILD "dev"
#endif

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace {

constexpr std::size_t kMaxEvents = 1024;
constexpr auto kWelcomeDeadline = 10s;      // connect + Hello + Welcome
constexpr auto kBackoffFirst = 1s;
constexpr auto kBackoffMax = 30s;
constexpr auto kBackoffRejected = 60s;       // the server said no; asking often will not change it
constexpr auto kFailLogEvery = 60s;          // repeated connect failures: one summary line per minute
constexpr int kPollMs = 2;                   // network thread services ENet every <= 2 ms (as AK)

struct Remote {
    KpRemotePose rp{};
    std::uint16_t serverAgeMs = 0;
    Clock::time_point arrived;
    bool dirty = false;
};

} // namespace

struct KpCore {
    KpCoreConfig cfg{};
    std::thread thread;
    std::atomic<bool> stopping{false};
    std::atomic<bool> finished{false};

    std::mutex m;   // guards everything below
    KpPose local{};
    std::uint32_t localSeq = 0;
    std::deque<KpEvent> events;
    std::map<std::uint16_t, Remote> remotes;
    KpCoreStatus st{};

    void log(const char* fmt, ...) {
        if (!cfg.log) return;
        char line[512];
        va_list a;
        va_start(a, fmt);
        std::vsnprintf(line, sizeof(line), fmt, a);
        va_end(a);
        cfg.log(cfg.logCtx, line);
    }

    void push(int kind, unsigned a = 0, unsigned b = 0, const std::string& name = {}) {
        KpEvent e{};
        e.kind = kind;
        e.a = a;
        e.b = b;
        strncpy_s(e.name, name.c_str(), _TRUNCATE);
        std::lock_guard<std::mutex> lock(m);
        if (events.size() >= kMaxEvents) { events.pop_front(); ++st.eventsDropped; }
        events.push_back(e);
    }

    void setState(int s, unsigned id) {
        std::lock_guard<std::mutex> lock(m);
        st.state = s;
        st.playerId = id;
    }

    void run();
};

namespace {

const char* rejectText(unsigned r) {
    switch (r) {
    case 1: return "protocol version differs";
    case 2: return "bad name";
    case 3: return "server full";
    case 4: return "malformed or unexpected message";
    case 5: return "no Hello in time";
    default: return "unknown reason";
    }
}

const char* leaveText(unsigned r) {
    switch (r) {
    case 0: return "left";
    case 1: return "timed out";
    case 2: return "removed by the server";
    default: return "unknown reason";
    }
}

kp::proto::Pose toProto(const KpPose& p) {
    kp::proto::Pose q;
    for (int i = 0; i < 3; ++i) { q.pos[i] = p.pos[i]; q.vel[i] = p.vel[i]; }
    q.yaw = kp::proto::normaliseYaw(p.yaw);
    q.flags = static_cast<std::uint8_t>(p.flags & kp::proto::kFlagsKnown);
    return q;
}

KpPose fromProto(const kp::proto::Pose& p) {
    KpPose q{};
    for (int i = 0; i < 3; ++i) { q.pos[i] = p.pos[i]; q.vel[i] = p.vel[i]; }
    q.yaw = p.yaw;
    q.flags = p.flags;
    return q;
}

} // namespace

void KpCore::run() {
    using Kind = kp::net::ClientEvent::Kind;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);   // loading saturates the CPU
    kp::net::NetClient net;
    enum class S { Waiting, Connecting, Welcomed } s = S::Waiting;
    auto now = Clock::now();
    auto nextAttempt = now;
    auto deadline = now;
    auto backoff = kBackoffFirst;
    auto lastFailLog = now - kFailLogEvery;
    unsigned failsSinceLog = 0, failsInRow = 0;
    std::uint32_t sentSeq = 0;
    std::vector<kp::net::ClientEvent> ev;

    // A failed attempt or a lost connection: back off, log the first failure of a row and then
    // one summary a minute.
    auto failed = [&](const char* why, int kind, std::chrono::seconds wait) {
        net.disconnect(0);
        {
            std::lock_guard<std::mutex> lock(m);
            remotes.clear();
        }
        setState(KP_CORE_WAITING, 0);
        push(kind);
        ++failsInRow;
        ++failsSinceLog;
        if (failsInRow == 1 || now - lastFailLog >= kFailLogEvery) {
            log("net: %s (%u failure%s since the last line); next try in %lld s", why, failsSinceLog,
                failsSinceLog == 1 ? "" : "s", static_cast<long long>(wait.count()));
            lastFailLog = now;
            failsSinceLog = 0;
        }
        nextAttempt = now + wait;
        s = S::Waiting;
    };

    while (!stopping.load()) {
        now = Clock::now();
        if (s == S::Waiting) {
            if (now < nextAttempt) { std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs)); continue; }
            unsigned attempt;
            {
                std::lock_guard<std::mutex> lock(m);
                attempt = ++st.attempts;
                st.state = KP_CORE_CONNECTING;
            }
            push(KP_EV_CONNECTING, attempt);
            if (failsInRow == 0) log("net: connecting to %s:%u as %s (attempt %u)", cfg.host, cfg.port, cfg.name, attempt);
            if (!net.connect(cfg.host, cfg.port, cfg.name)) {
                failed("cannot resolve the server address or start ENet", KP_EV_CONNECT_FAILED,
                       std::chrono::duration_cast<std::chrono::seconds>(backoff));
                backoff = std::min<std::chrono::seconds>(std::chrono::duration_cast<std::chrono::seconds>(backoff) * 2, kBackoffMax);
                continue;
            }
            s = S::Connecting;
            deadline = now + kWelcomeDeadline;
        }

        ev.clear();
        net.poll(kPollMs, ev);
        now = Clock::now();
        for (auto& e : ev) {
            switch (e.kind) {
            case Kind::Welcome:
                s = S::Welcomed;
                setState(KP_CORE_WELCOMED, e.playerId);
                push(KP_EV_WELCOME, e.playerId);
                log("net: welcomed by %s:%u as player %u%s", cfg.host, cfg.port, e.playerId,
                    failsInRow ? " (after failed attempts)" : "");
                failsInRow = 0;
                failsSinceLog = 0;
                backoff = kBackoffFirst;
                sentSeq = 0;   // send the current pose again on the new connection
                break;
            case Kind::Rejected: {
                unsigned r = static_cast<unsigned>(e.reject);
                push(KP_EV_REJECTED, r);
                char why[96];
                std::snprintf(why, sizeof(why), "server refused us: %s", rejectText(r));
                failsInRow = 0;   // always log a refusal
                failed(why, KP_EV_CONNECT_FAILED, kBackoffRejected);
                break;
            }
            case Kind::Joined:
                push(KP_EV_JOINED, e.playerId, 0, e.name);
                break;
            case Kind::Left: {
                std::lock_guard<std::mutex> lock(m);
                remotes.erase(e.playerId);
            }
                push(KP_EV_LEFT, e.playerId, static_cast<unsigned>(e.leave));
                break;
            case Kind::Snapshot: {
                std::lock_guard<std::mutex> lock(m);
                for (const auto& en : e.snapshot.entries) {
                    Remote& r = remotes[en.playerId];
                    r.rp.playerId = en.playerId;
                    r.rp.tick = e.snapshot.tick;
                    r.rp.pose = fromProto(en.pose);
                    r.serverAgeMs = en.ageMs;
                    r.arrived = now;
                    r.dirty = true;
                }
                break;
            }
            case Kind::Disconnected:
                if (s == S::Welcomed) {
                    failsInRow = 0;   // always log a loss
                    failed("connection lost (ENet timeout or the server closed)", KP_EV_LOST, std::chrono::seconds(1));
                } else {
                    failed("no answer from the server", KP_EV_CONNECT_FAILED,
                           std::chrono::duration_cast<std::chrono::seconds>(backoff));
                    backoff = std::min<std::chrono::seconds>(std::chrono::duration_cast<std::chrono::seconds>(backoff) * 2, kBackoffMax);
                }
                break;
            }
            if (s == S::Waiting) break;   // the rest of this batch belongs to a closed connection
        }
        if (s == S::Connecting && now >= deadline) {
            failed("no Welcome within 10 s", KP_EV_CONNECT_FAILED, std::chrono::duration_cast<std::chrono::seconds>(backoff));
            backoff = std::min<std::chrono::seconds>(std::chrono::duration_cast<std::chrono::seconds>(backoff) * 2, kBackoffMax);
        }
        if (s == S::Welcomed) {
            KpPose p;
            std::uint32_t seq;
            {
                std::lock_guard<std::mutex> lock(m);
                p = local;
                seq = localSeq;
            }
            if (seq != sentSeq && seq != 0) {
                sentSeq = seq;
                if (net.sendPose(toProto(p))) {
                    std::lock_guard<std::mutex> lock(m);
                    ++st.posesSent;
                }
            }
            std::lock_guard<std::mutex> lock(m);
            st.messagesDropped = static_cast<unsigned>(net.droppedMessages());
        }
    }
    if (s == S::Welcomed) log("net: leaving the server");
    net.disconnect(300);
    setState(KP_CORE_STOPPED, 0);
}

namespace {

bool terminated(const char* s, std::size_t n) { return std::memchr(s, 0, n) != nullptr; }

KpCore* coreStart(const KpCoreConfig* cfg) {
    try {
        if (!cfg || cfg->size < sizeof(KpCoreConfig) || cfg->version != KP_CORE_VERSION) return nullptr;
        auto bad = [&](const char* why) -> KpCore* {
            if (cfg->log) {
                char line[160];
                std::snprintf(line, sizeof(line), "net: off (%s)", why);
                cfg->log(cfg->logCtx, line);
            }
            return nullptr;
        };
        if (!terminated(cfg->host, sizeof(cfg->host)) || cfg->host[0] == 0) return bad("no server host");
        if (cfg->port == 0) return bad("port 0");
        if (!terminated(cfg->name, sizeof(cfg->name)) || !kp::proto::validName(cfg->name))
            return bad("bad player name: 1-32 bytes, no control characters");
        auto* c = new KpCore();
        c->cfg = *cfg;
        c->st.state = KP_CORE_WAITING;
        c->thread = std::thread([c] {
            try {
                c->run();
            } catch (...) {
                // Never let an exception end the game; networking just stops.
                c->log("net: network thread stopped by an internal error");
                std::lock_guard<std::mutex> lock(c->m);
                c->st.state = KP_CORE_STOPPED;
            }
            c->finished = true;
        });
        return c;
    } catch (...) {
        return nullptr;
    }
}

void coreStop(KpCore* c) {
    if (!c) return;
    try {
        c->stopping = true;
        // The thread leaves within ~300 ms; a blocking name lookup could hold it longer. Never hang
        // the game's exit for it: after 2 s the thread is left to the process exit (and c leaked).
        auto until = Clock::now() + 2s;
        while (!c->finished.load() && Clock::now() < until) std::this_thread::sleep_for(5ms);
        if (c->finished.load()) {
            c->thread.join();
            delete c;
        } else {
            c->log("net: network thread did not stop within 2 s; leaving it to the process exit");
            c->thread.detach();
        }
    } catch (...) {
    }
}

int coreSetLocalPose(KpCore* c, const KpPose* p) {
    if (!c || !p) return 0;
    kp::proto::Pose q = toProto(*p);
    if (!kp::proto::validPose(q)) return 0;
    std::lock_guard<std::mutex> lock(c->m);
    c->local = *p;
    if (++c->localSeq == 0) c->localSeq = 1;   // 0 means "none yet"
    return 1;
}

int coreTakeEvents(KpCore* c, KpEvent* out, int max) {
    if (!c || !out || max <= 0) return 0;
    std::lock_guard<std::mutex> lock(c->m);
    int n = 0;
    while (n < max && !c->events.empty()) {
        out[n++] = c->events.front();
        c->events.pop_front();
    }
    return n;
}

int coreTakeRemotePoses(KpCore* c, KpRemotePose* out, int max) {
    if (!c || !out || max <= 0) return 0;
    auto now = Clock::now();
    std::lock_guard<std::mutex> lock(c->m);
    int n = 0;
    for (auto& [id, r] : c->remotes) {
        if (n >= max) break;
        if (!r.dirty) continue;
        r.dirty = false;
        out[n] = r.rp;
        auto since = std::chrono::duration_cast<std::chrono::milliseconds>(now - r.arrived).count();
        out[n].ageMs = static_cast<unsigned>(std::min<long long>(r.serverAgeMs + since, 0xffffffffLL));
        ++n;
    }
    return n;
}

void coreStatus(KpCore* c, KpCoreStatus* out) {
    if (!out) return;
    if (!c) { *out = KpCoreStatus{}; out->state = KP_CORE_STOPPED; return; }
    std::lock_guard<std::mutex> lock(c->m);
    *out = c->st;
}

const KpCoreApi g_api = {
    sizeof(KpCoreApi), KP_CORE_VERSION, "KenshiPureCore " KP_CORE_BUILD " (built " __DATE__ " " __TIME__ ")",
    coreStart, coreStop, coreSetLocalPose, coreTakeEvents, coreTakeRemotePoses, coreStatus,
};

} // namespace

extern "C" __declspec(dllexport) const KpCoreApi* kpCoreGetApi(unsigned int version) {
    return version == KP_CORE_VERSION ? &g_api : nullptr;
}
