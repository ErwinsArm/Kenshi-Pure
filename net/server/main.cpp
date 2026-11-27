// kp-server: the KenshiPure relay server (console).
//   kp-server [--port N] [--max-players N] [--local]
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "RelayServer.h"

namespace {
std::atomic<bool> g_stop{false};
void onSignal(int) { g_stop = true; }

void logLine(std::string_view s) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char ts[16];
    std::strftime(ts, sizeof ts, "%H:%M:%S", &tm);
    std::printf("%s %.*s\n", ts, static_cast<int>(s.size()), s.data());
    std::fflush(stdout);
}

int usage() {
    std::printf("usage: kp-server [--port N] [--max-players N] [--local]\n"
                "  --port N         UDP port (default %u)\n"
                "  --max-players N  1-%zu (default %zu)\n"
                "  --local          accept only connections from this PC\n",
                kp::proto::kDefaultPort, kp::proto::kMaxPlayers, kp::proto::kMaxPlayers);
    return 2;
}
}

int main(int argc, char** argv) {
    kp::net::ServerConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> long { return (i + 1 < argc) ? std::strtol(argv[++i], nullptr, 10) : -1; };
        if (a == "--port") { long v = next(); if (v < 1 || v > 65535) return usage(); cfg.port = static_cast<std::uint16_t>(v); }
        else if (a == "--max-players") { long v = next(); if (v < 1 || v > static_cast<long>(kp::proto::kMaxPlayers)) return usage(); cfg.maxPlayers = static_cast<std::size_t>(v); }
        else if (a == "--local") cfg.loopbackOnly = true;
        else return usage();
    }
    cfg.log = logLine;
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    kp::net::RelayServer server;
    if (!server.start(cfg)) return 1;
    logLine("KenshiPure relay server, protocol " + std::to_string(kp::proto::kProtocolVersion) + "; Ctrl+C stops");
    auto lastStats = std::chrono::steady_clock::now();
    while (!g_stop) {
        server.poll(50);
        if (std::chrono::steady_clock::now() - lastStats > std::chrono::seconds(60)) {
            lastStats = std::chrono::steady_clock::now();
            const auto& s = server.stats();
            logLine("players " + std::to_string(server.playerCount()) + ", poses " + std::to_string(s.posesAccepted) +
                    ", snapshots " + std::to_string(s.snapshotsSent) + ", rejected " + std::to_string(s.rejected));
        }
    }
    server.stop();
    logLine("stopped");
    return 0;
}
