// kp-drive-test: the M4 stand-in logic without the game and without a network. client/Drive.cpp
// (track, planner, cfg) directly, and client/NetFrame.cpp (the plugin's own frame side) against a
// fake core table and a fake game: pick, drive, cycle phases, release on menu/load, stale remote,
// lost body, faulting calls, player left, stop.
#include "Check.h"

#include "Drive.h"
#include "NetFrame.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace kp;

namespace
{
    // ------------------------------------------------------------------ fake core

    struct FakeCore
    {
        std::vector<KpEvent> events;
        std::vector<KpRemotePose> poses;
    } g_core;
    KpCore* const kCoreHandle = reinterpret_cast<KpCore*>(&g_core);

    KpCore* CoreStart(const KpCoreConfig*) { return kCoreHandle; }
    void CoreStop(KpCore*) {}
    int CoreSetLocalPose(KpCore*, const KpPose*) { return 1; }
    int CoreTakeEvents(KpCore*, KpEvent* out, int max)
    {
        int n = 0;
        while (n < max && !g_core.events.empty()) { out[n++] = g_core.events.front(); g_core.events.erase(g_core.events.begin()); }
        return n;
    }
    int CoreTakePoses(KpCore*, KpRemotePose* out, int max)
    {
        int n = 0;
        while (n < max && !g_core.poses.empty()) { out[n++] = g_core.poses.front(); g_core.poses.erase(g_core.poses.begin()); }
        return n;
    }
    void CoreStatus(KpCore*, KpCoreStatus* s) { std::memset(s, 0, sizeof(*s)); }

    KpCoreApi MakeApi()
    {
        KpCoreApi a;
        std::memset(&a, 0, sizeof(a));
        a.size = sizeof(a);
        a.version = KP_CORE_VERSION;
        a.build = "fake";
        a.start = CoreStart;
        a.stop = CoreStop;
        a.setLocalPose = CoreSetLocalPose;
        a.takeEvents = CoreTakeEvents;
        a.takeRemotePoses = CoreTakePoses;
        a.status = CoreStatus;
        return a;
    }

    void PushJoin(unsigned short id, const char* name)
    {
        KpEvent e;
        std::memset(&e, 0, sizeof(e));
        e.kind = KP_EV_JOINED;
        e.a = id;
        strcpy_s(e.name, sizeof(e.name), name);
        g_core.events.push_back(e);
    }

    void PushLeft(unsigned short id)
    {
        KpEvent e;
        std::memset(&e, 0, sizeof(e));
        e.kind = KP_EV_LEFT;
        e.a = id;
        g_core.events.push_back(e);
    }

    KpRemotePose Pose(unsigned short id, float x, float z, float vx, float vz, unsigned int age, bool moving = true)
    {
        KpRemotePose p;
        std::memset(&p, 0, sizeof(p));
        p.playerId = id;
        p.ageMs = age;
        p.pose.pos[0] = x;
        p.pose.pos[1] = 10.0f;
        p.pose.pos[2] = z;
        p.pose.vel[0] = vx;
        p.pose.vel[2] = vz;
        p.pose.yaw = 90.0f;
        p.pose.flags = moving ? KP_POSE_MOVING : 0;
        return p;
    }

    // ------------------------------------------------------------------ fake game

    class FakeGame : public IGameAccess
    {
    public:
        FakeGame() : haveLocal(true), epoch(1), charEpoch(0), paused(false), haveNpc(true), bodyResolves(true), applyOk(true),
                     picks(0), releases(0), releasesWorldGone(0), aiSkip(false), walked(false), tick(0)
        {
            std::memset(local, 0, sizeof(local));
            body[0] = 50.0f; body[1] = 10.0f; body[2] = 0.0f;
        }
        bool LocalPose(NetLocalSample* out)
        {
            if (!haveLocal) return false;
            std::memset(out, 0, sizeof(*out));
            for (int i = 0; i < 3; ++i) { out->pos[i] = local[i]; out->dest[i] = local[i] + 1.0f; }
            out->tick = ++tick;
            out->epoch = epoch + charEpoch;
            out->worldEpoch = epoch;
            out->paused = paused ? 1 : 0;
            return true;
        }
        void Log(const char* line) { lines.push_back(line); }
        bool StandInPick(const float* nearPos, const DriveConfig& cfg, StandInInfo* out)
        {
            if (!haveNpc) return false;
            float d = DistXZ(nearPos, body);
            if (d > cfg.pickDist) return false;
            ++picks;
            strcpy_s(out->name, sizeof(out->name), "Npc");
            for (int i = 0; i < 3; ++i) out->home[i] = body[i];
            out->distance = d;
            return true;
        }
        bool StandInRead(BodyState* out)
        {
            if (!bodyResolves) return false;
            for (int i = 0; i < 3; ++i) out->pos[i] = body[i];
            out->speed = 0.0f;
            out->moving = walked ? 1 : 0;
            return true;
        }
        bool StandInApply(const DriveCmd& c, int)
        {
            cmds.push_back(c);
            if (!applyOk) return false;
            if (c.kind == DC_WALK) walked = true;
            if (c.kind == DC_PLACE || c.kind == DC_SNAP)
                for (int i = 0; i < 3; ++i) body[i] = c.pos[i];
            return true;
        }
        void StandInSkipAi(bool on) { aiSkip = on; }
        void StandInRelease(bool worldGone)
        {
            ++releases;
            if (worldGone) ++releasesWorldGone;
            aiSkip = false;
        }
        int Count(int kind) const
        {
            int n = 0;
            for (size_t i = 0; i < cmds.size(); ++i) n += cmds[i].kind == kind ? 1 : 0;
            return n;
        }
        bool Logged(const char* part) const
        {
            for (size_t i = 0; i < lines.size(); ++i)
                if (lines[i].find(part) != std::string::npos) return true;
            return false;
        }

        bool haveLocal;
        unsigned int epoch;          // world epoch
        unsigned int charEpoch;      // a click on another own character
        bool paused, haveNpc, bodyResolves, applyOk;
        int picks, releases, releasesWorldGone;
        bool aiSkip;
        bool walked;
        unsigned long long tick;
        float local[3], body[3];
        std::vector<DriveCmd> cmds;
        std::vector<std::string> lines;
    };

    // The bot's circle as the core would report it: one new pose every 100 ms, ~100 ms old.
    struct Bot
    {
        float angle;
        Bot() : angle(0.0f) {}
        void Step(unsigned short id)
        {
            angle += 0.05f;
            float r = 200.0f, s = 60.0f;
            g_core.poses.push_back(Pose(id, r * std::sin(angle), r * std::cos(angle), s * std::cos(angle),
                                        -s * std::sin(angle), 100));
        }
    };

    void Run(NetFrame& nf, unsigned long long& now, unsigned long long ms, Bot* bot, unsigned short id)
    {
        for (unsigned long long end = now + ms; now < end; now += 20)
        {
            if (bot && now % 100 == 0) bot->Step(id);
            nf.OnFrame(now);
        }
    }

    // ------------------------------------------------------------------ pure parts

    void TestConfig()
    {
        DriveConfig c;
        DriveParseConfig("host=1.2.3.4\n", &c);
        CHECK(c.mode == DM_OFF);
        CHECK(c.aiSkip == DA_AI);
        CHECK(c.snapDist == 300.0f);
        DriveParseConfig("standin = cycle\r\nstandin_ai=character\nstandin_snap=120\nstandin_name= Beep \nstandin_range=5", &c);
        CHECK(c.mode == DM_CYCLE);
        CHECK(c.aiSkip == DA_CHARACTER);
        CHECK(c.snapDist == 120.0f);
        CHECK(std::strcmp(c.name, "Beep") == 0);
        CHECK(c.pickDist == 600.0f);   // 5 is out of range: default kept
        CHECK(c.settleMs == 0);
        DriveParseConfig("standin_settle=5", &c);
        CHECK(c.settleMs == 5000);
        DriveParseConfig("standin=order", &c);
        CHECK(c.mode == DM_ORDER);
        DriveParseConfig("standin=jump\n#standin=place\nstandin_ai=none", &c);
        CHECK(c.mode == DM_OFF);
        CHECK(c.aiSkip == DA_NONE);
    }

    void TestTrack()
    {
        RemoteTrack t;
        TrackSample s;
        CHECK(!t.At(1000, &s));
        t.Add(1000, Pose(1, 0, 0, 10, 0, 100));      // taken at 900
        t.Add(1100, Pose(1, 1, 0, 10, 0, 100));      // taken at 1000
        CHECK(t.Count() == 2);
        t.Add(1200, Pose(1, 1, 0, 10, 0, 200));      // the same pose repeated, older: ignored
        CHECK(t.Count() == 2);
        CHECK(t.At(950, &s) && std::fabs(s.pos[0] - 0.5f) < 1e-4f);
        CHECK(t.At(800, &s) && s.pos[0] == 0.0f);    // before the first: the first
        CHECK(t.At(1100, &s) && std::fabs(s.pos[0] - 2.0f) < 1e-3f);   // 100 ms beyond: 1 + 10 * 0.1
        CHECK(t.At(5000, &s) && std::fabs(s.pos[0] - 4.0f) < 1e-3f);   // extrapolation stops at 300 ms
        t.Add(1300, Pose(1, 2, 0, 0, 0, 400, false));  // taken "at 900": jitter must not reorder
        CHECK(t.Count() == 3);
        CHECK(t.At(1001, &s) && s.pos[0] == 2.0f && !s.moving);
        CHECK(t.At(9000, &s) && s.pos[0] == 2.0f);     // not moving: no extrapolation
        // Yaw blends the short way across +-180.
        RemoteTrack y;
        KpRemotePose a = Pose(1, 0, 0, 0, 0, 0), b = Pose(1, 1, 0, 0, 0, 0);
        a.pose.yaw = 170.0f;
        b.pose.yaw = -170.0f;
        y.Add(1000, a);
        y.Add(1100, b);
        CHECK(y.At(1050, &s) && std::fabs(std::fabs(s.yaw) - 180.0f) < 1e-3f);
        for (int i = 0; i < 40; ++i) y.Add(1200 + 100 * i, Pose(1, (float)i + 2, 0, 0, 0, 0));
        CHECK(y.Count() == RemoteTrack::kMax);
    }

    TrackSample Target(float x, float z, float vx, float vz, bool moving)
    {
        TrackSample t;
        std::memset(&t, 0, sizeof(t));
        t.pos[0] = x; t.pos[2] = z; t.vel[0] = vx; t.vel[2] = vz; t.moving = moving ? 1 : 0;
        return t;
    }

    BodyState Body(float x, float z)
    {
        BodyState b;
        std::memset(&b, 0, sizeof(b));
        b.pos[0] = x; b.pos[2] = z;
        return b;
    }

    void TestPlanner()
    {
        DrivePlanner p;
        float gap = 0;
        // Place: halt first, then a place every 100 ms.
        p.Reset(DM_PLACE, 300.0f);
        CHECK(p.Step(1000, Body(0, 0), Target(5, 0, 0, 0, true), &gap).kind == DC_HALT);
        CHECK(p.Step(1020, Body(0, 0), Target(5, 0, 0, 0, true), &gap).kind == DC_PLACE);
        CHECK(gap == 5.0f);
        CHECK(p.Step(1060, Body(5, 0), Target(6, 0, 0, 0, true), &gap).kind == DC_NONE);
        DriveCmd c = p.Step(1120, Body(5, 0), Target(7, 0, 0, 0, true), &gap);
        CHECK(c.kind == DC_PLACE && c.pos[0] == 7.0f);

        // Path: lead point 0.6 s ahead, re-issued only when it moved > 15 or after 1 s.
        p.Reset(DM_PATH, 300.0f);
        p.Step(0, Body(0, 0), Target(0, 0, 0, 0, false), &gap);   // halt
        c = p.Step(100, Body(0, 0), Target(10, 0, 50, 0, true), &gap);
        CHECK(c.kind == DC_WALK && std::fabs(c.pos[0] - 40.0f) < 1e-3f);
        CHECK(c.speed >= 50.0f && c.speed <= 125.0f);
        CHECK(p.Step(120, Body(1, 0), Target(11, 0, 50, 0, true), &gap).kind == DC_NONE);
        CHECK(p.Step(200, Body(5, 0), Target(30, 0, 50, 0, true), &gap).kind == DC_WALK);  // lead moved 20
        CHECK(p.Step(1300, Body(25, 0), Target(31, 0, 50, 0, true), &gap).kind == DC_WALK); // 1 s passed
        // Catch-up speed is capped at 2.5x.
        c = p.Step(2400, Body(0, 0), Target(290, 0, 40, 0, true), &gap);
        CHECK(c.kind == DC_WALK && c.speed <= 100.0f + 1e-3f);
        // Remote stopped and close: halt once.
        CHECK(p.Step(2500, Body(0, 0), Target(10, 0, 0, 0, false), &gap).kind == DC_HALT);
        CHECK(p.Step(2600, Body(0, 0), Target(10, 0, 0, 0, false), &gap).kind == DC_NONE);
        // Remote stopped but far: walk to it.
        CHECK(p.Step(2700, Body(0, 0), Target(100, 0, 0, 0, false), &gap).kind == DC_WALK);
        // Drift beyond the snap distance: snap, then not again within 1 s.
        c = p.Step(3000, Body(0, 0), Target(400, 0, 50, 0, true), &gap);
        CHECK(c.kind == DC_SNAP && c.pos[0] == 400.0f);
        CHECK(p.Step(3500, Body(0, 0), Target(400, 0, 50, 0, true), &gap).kind != DC_SNAP);
        CHECK(p.Step(4100, Body(0, 0), Target(400, 0, 50, 0, true), &gap).kind == DC_SNAP);
        CHECK(p.Stats().snaps == 2);

        // Steer: velocity plus correction, capped; halt once when stopped on target.
        p.Reset(DM_STEER, 300.0f);
        p.Step(0, Body(0, 0), Target(0, 0, 0, 0, false), &gap);
        c = p.Step(20, Body(0, 0), Target(10, 0, 30, 0, true), &gap);
        CHECK(c.kind == DC_STEER && std::fabs(c.vel[0] - 50.0f) < 1e-3f && c.vel[1] == 0.0f);
        c = p.Step(40, Body(0, 0), Target(200, 0, 30, 0, true), &gap);
        CHECK(c.kind == DC_STEER && std::fabs(c.speed - 75.0f) < 1e-3f && c.limit == c.speed * 0.25f);
        CHECK(p.Step(60, Body(10, 0), Target(12, 0, 0, 0, false), &gap).kind == DC_HALT);
        CHECK(p.Step(80, Body(12, 0), Target(12, 0, 0, 0, false), &gap).kind == DC_NONE);
    }

    // ------------------------------------------------------------------ NetFrame lifecycle

    DriveConfig Cfg(int mode)
    {
        DriveConfig c;
        DriveParseConfig("", &c);
        c.mode = mode;
        c.settleMs = 0;
        return c;
    }

    void TestLifecycle()
    {
        KpCoreApi api = MakeApi();
        KpCoreConfig cc;
        NetParseConfig("host=x\n", &cc);
        unsigned long long now = 100000;

        {   // off: nothing is picked
            FakeGame g;
            NetFrame nf;
            CHECK(nf.Start(&api, cc, &g));
            Bot bot;
            PushJoin(7, "bot");
            Run(nf, now, 3000, &bot, 7);
            CHECK(g.picks == 0 && !nf.StandInDriving());
            nf.Stop();
            g_core.poses.clear();
        }
        {   // cycle: pick, place, phases, AI skip per phase, menu releases without calls
            FakeGame g;
            NetFrame nf;
            nf.SetDrive(Cfg(DM_CYCLE));
            CHECK(nf.Start(&api, cc, &g));
            Bot bot;
            PushJoin(7, "bot");
            Run(nf, now, 1000, &bot, 7);
            CHECK(nf.StandInDriving() && g.picks == 1);
            CHECK(nf.StandInMode() == DM_PLACE && g.aiSkip);
            CHECK(g.Count(DC_HALT) == 2 && g.Count(DC_PLACE) >= 5);   // wake-up halt, place halt
            CHECK(g.Logged("standin: waking it up") && g.Logged("standin: it moves"));
            // Placed onto the target: the body follows the circle.
            Run(nf, now, 2000, &bot, 7);
            CHECK(g.Logged("standin: driving Npc"));
            Run(nf, now, 17000, &bot, 7);
            // A click on another own character changes the character epoch only: still driving.
            ++g.charEpoch;
            Run(nf, now, 1000, &bot, 7);
            CHECK(nf.StandInDriving() && g.releases == 0);
            CHECK(nf.StandInPhase() == 1 && nf.StandInMode() == DM_PATH && g.aiSkip);
            CHECK(g.Logged("standin: phase place (AI skipped) done"));
            CHECK(g.Count(DC_WALK) > 0);
            Run(nf, now, 45000, &bot, 7);
            CHECK(nf.StandInPhase() == 2 && nf.StandInMode() == DM_ORDER && !g.aiSkip);   // the order's job needs the AI
            Run(nf, now, 45000, &bot, 7);
            CHECK(nf.StandInPhase() == 3 && nf.StandInMode() == DM_STEER);
            Run(nf, now, 1000, &bot, 7);
            CHECK(g.Count(DC_STEER) > 10);
            Run(nf, now, 45000, &bot, 7);
            CHECK(nf.StandInPhase() == 0);   // the cycle starts over
            // Paused: no commands.
            g.paused = true;
            size_t before = g.cmds.size();
            Run(nf, now, 1000, &bot, 7);
            CHECK(g.cmds.size() == before);
            g.paused = false;
            // Menu / load: released without calls into the old world, re-picked afterwards.
            g.haveLocal = false;
            Run(nf, now, 100, &bot, 7);
            CHECK(!nf.StandInDriving() && g.releases == 1 && g.releasesWorldGone == 1 && !g.aiSkip);
            g.haveLocal = true;
            ++g.epoch;
            Run(nf, now, 3000, &bot, 7);
            CHECK(nf.StandInDriving() && g.picks == 2);
            // A load without a menu gap (epoch change only): released the same way.
            ++g.epoch;
            Run(nf, now, 100, &bot, 7);
            CHECK(!nf.StandInDriving() && g.releasesWorldGone == 2);
            Run(nf, now, 3000, &bot, 7);
            CHECK(nf.StandInDriving() && g.picks == 3);
            // The bot stops sending: released after 3 s, and this time put back home (world still there).
            Run(nf, now, 3200, NULL, 7);
            CHECK(!nf.StandInDriving() && g.releases == 3 && g.releasesWorldGone == 2);
            CHECK(g.Logged("no fresh remote pose"));
            // The body stops resolving: released without calls.
            Run(nf, now, 3000, &bot, 7);
            CHECK(nf.StandInDriving());
            g.bodyResolves = false;
            Run(nf, now, 100, &bot, 7);
            CHECK(!nf.StandInDriving() && g.releasesWorldGone == 3);
            g.bodyResolves = true;
            // Faulting game calls: released after 3 in a row.
            Run(nf, now, 3000, &bot, 7);
            CHECK(nf.StandInDriving());
            g.applyOk = false;
            Run(nf, now, 1000, &bot, 7);
            CHECK(!nf.StandInDriving() && g.Logged("faulted 3 times"));
            g.applyOk = true;
            // The driving player leaves: released; another player takes over.
            Run(nf, now, 3000, &bot, 7);
            CHECK(nf.StandInDriving());
            PushLeft(7);
            Run(nf, now, 100, NULL, 7);
            CHECK(!nf.StandInDriving());
            PushJoin(8, "bot2");
            Bot bot2;
            Run(nf, now, 3000, &bot2, 8);
            CHECK(nf.StandInDriving() && g.Logged("from remote player 8"));
            // Stop: released without calls.
            int rg = g.releasesWorldGone;
            nf.Stop();
            CHECK(!nf.StandInDriving() && g.releasesWorldGone == rg + 1);
            g_core.poses.clear();
            g_core.events.clear();
        }
        {   // no NPC in range: no pick, logged at most every 10 s
            FakeGame g;
            g.haveNpc = false;
            NetFrame nf;
            nf.SetDrive(Cfg(DM_PATH));
            CHECK(nf.Start(&api, cc, &g));
            Bot bot;
            PushJoin(7, "bot");
            Run(nf, now, 12000, &bot, 7);
            int n = 0;
            for (size_t i = 0; i < g.lines.size(); ++i) n += g.lines[i].find("no NPC to drive") != std::string::npos ? 1 : 0;
            CHECK(!nf.StandInDriving() && n == 2);
            nf.Stop();
            g_core.poses.clear();
        }
        {   // settle: no pick until the world has been the same for settleMs, again after a load
            FakeGame g;
            NetFrame nf;
            DriveConfig c = Cfg(DM_PATH);
            c.settleMs = 5000;
            nf.SetDrive(c);
            CHECK(nf.Start(&api, cc, &g));
            Bot bot;
            PushJoin(7, "bot");
            Run(nf, now, 4000, &bot, 7);
            CHECK(!nf.StandInDriving() && g.picks == 0);
            Run(nf, now, 3000, &bot, 7);
            CHECK(nf.StandInDriving() && g.picks == 1);
            ++g.epoch;   // a load
            Run(nf, now, 4000, &bot, 7);
            CHECK(!nf.StandInDriving() && g.picks == 1);
            Run(nf, now, 3000, &bot, 7);
            CHECK(nf.StandInDriving() && g.picks == 2);
            nf.Stop();
            g_core.poses.clear();
        }
        {   // standin_ai=none: the skip stays off in every phase
            FakeGame g;
            NetFrame nf;
            DriveConfig c = Cfg(DM_STEER);
            c.aiSkip = DA_NONE;
            nf.SetDrive(c);
            CHECK(nf.Start(&api, cc, &g));
            Bot bot;
            PushJoin(7, "bot");
            Run(nf, now, 3000, &bot, 7);
            CHECK(nf.StandInDriving() && !g.aiSkip && !nf.StandInAiSkipped());
            nf.Stop();
            g_core.poses.clear();
        }
    }
}

int main()
{
    TestConfig();
    TestTrack();
    TestPlanner();
    TestLifecycle();
    return TEST_RESULT("kp-drive-test");
}
