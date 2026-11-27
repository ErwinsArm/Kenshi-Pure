// kp-bot: a test player that connects to the relay server and walks a circle.
//   kp-bot [--host H] [--port N] [--name S] [--center x,y,z | --around-first] [--radius R] [--speed V] [--seconds N]
// --around-first: wait for the first other player's pose and circle around where it was then (in-game
// tests: the circle lies around the tester wherever they stand).
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "../client/NetClient.h"

namespace {
std::atomic<bool> g_stop{false};
void onSignal(int) { g_stop = true; }
constexpr double kPi = 3.14159265358979323846;

int usage() {
    std::printf("usage: kp-bot [--host H] [--port N] [--name S] [--center x,y,z | --around-first] [--radius R] [--speed V] [--seconds N]\n"
                "  defaults: --host 127.0.0.1 --port %u --name bot --center 0,0,0 --radius 50 --speed 20 (units/s)\n"
                "  --seconds 0 (default) runs until Ctrl+C\n", kp::proto::kDefaultPort);
    return 2;
}
}

int main(int argc, char** argv) {
    std::string host = "127.0.0.1", name = "bot";
    long port = kp::proto::kDefaultPort;
    double cx = 0, cy = 0, cz = 0, radius = 50, speed = 20, seconds = 0;
    bool aroundFirst = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--around-first") { aroundFirst = true; continue; }
        if (i + 1 >= argc) return usage();
        std::string v = argv[++i];
        if (a == "--host") host = v;
        else if (a == "--port") { port = std::strtol(v.c_str(), nullptr, 10); if (port < 1 || port > 65535) return usage(); }
        else if (a == "--name") name = v;
        else if (a == "--center") { if (sscanf_s(v.c_str(), "%lf,%lf,%lf", &cx, &cy, &cz) != 3) return usage(); }
        else if (a == "--radius") { radius = std::atof(v.c_str()); if (!(radius > 0)) return usage(); }
        else if (a == "--speed") { speed = std::atof(v.c_str()); if (!(speed >= 0)) return usage(); }
        else if (a == "--seconds") seconds = std::atof(v.c_str());
        else return usage();
    }
    if (!kp::proto::validName(name)) { std::printf("bad name (1-32 bytes, no control characters)\n"); return 2; }
    std::signal(SIGINT, onSignal);

    kp::net::NetClient c;
    if (!c.connect(host, static_cast<std::uint16_t>(port), name)) { std::printf("cannot connect to %s:%ld\n", host.c_str(), port); return 1; }
    std::printf("connecting to %s:%ld as %s\n", host.c_str(), port, name.c_str());

    using Clock = std::chrono::steady_clock;
    auto start = Clock::now(), nextPose = start, lastLog = start;
    std::vector<kp::net::ClientEvent> events;
    std::size_t lastSeen = 0;
    auto lastSnap = start;   // the server sends no empty snapshots: none for a while = nobody to see
    bool haveCenter = !aroundFirst;
    if (aroundFirst) std::printf("waiting for another player's pose to circle around\n");
    while (!g_stop) {
        events.clear();
        c.poll(5, events);
        for (const auto& e : events) {
            using K = kp::net::ClientEvent::Kind;
            switch (e.kind) {
            case K::Welcome: std::printf("welcome: player id %u\n", e.playerId); break;
            case K::Rejected: std::printf("rejected (reason %d)\n", static_cast<int>(e.reject)); return 1;
            case K::Joined: std::printf("player %u joined: %s\n", e.playerId, e.name.c_str()); break;
            case K::Left: std::printf("player %u left (reason %d)\n", e.playerId, static_cast<int>(e.leave)); break;
            case K::Snapshot:
                lastSeen = e.snapshot.entries.size();
                lastSnap = Clock::now();
                if (!haveCenter && !e.snapshot.entries.empty()) {
                    const auto& q = e.snapshot.entries.front();
                    cx = q.pose.pos[0]; cy = q.pose.pos[1]; cz = q.pose.pos[2];
                    haveCenter = true;
                    start = nextPose = Clock::now();
                    std::printf("circling around player %u at %.1f %.1f %.1f\n", q.playerId, cx, cy, cz);
                }
                break;
            case K::Disconnected: std::printf("disconnected\n"); return 1;
            }
        }
        auto now = Clock::now();
        if (now - lastSnap > std::chrono::milliseconds(500)) lastSeen = 0;
        double t = std::chrono::duration<double>(now - start).count();
        if (seconds > 0 && t >= seconds) break;
        if (c.welcomed() && haveCenter && now >= nextPose) {
            nextPose += std::chrono::milliseconds(100);
            double w = speed / radius;               // radians per second
            double a = w * t;
            kp::proto::Pose p;
            p.pos[0] = static_cast<float>(cx + radius * std::sin(a));
            p.pos[1] = static_cast<float>(cy);
            p.pos[2] = static_cast<float>(cz + radius * std::cos(a));
            double vx = speed * std::cos(a), vz = -speed * std::sin(a);
            p.vel[0] = static_cast<float>(vx);
            p.vel[2] = static_cast<float>(vz);
            p.yaw = kp::proto::normaliseYaw(static_cast<float>(std::atan2(vx, vz) * 180.0 / kPi));  // 0 = +z, 90 = +x
            p.flags = speed > 0 ? kp::proto::kFlagMoving : 0;
            c.sendPose(p);
            if (now - lastLog >= std::chrono::seconds(1)) {
                lastLog = now;
                std::printf("pose %.1f %.1f %.1f yaw %.0f; sees %zu player(s)\n", p.pos[0], p.pos[1], p.pos[2], p.yaw, lastSeen);
            }
        }
        std::fflush(stdout);
    }
    c.disconnect();
    std::printf("left\n");
    return 0;
}
