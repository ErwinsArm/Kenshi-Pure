// kp-remote-test: the M5 remote-body lifecycle without the game and without a network. client/Remote.cpp
// (config, RemoteBodies) through client/NetFrame.cpp (the plugin's own frame side) against a fake core table
// and a fake game that simulates bodies walking: spawn after the settle time, drive, leave, stale, a world
// change (kept after the hand check, or forgotten without a call), menu, failed spawns, a destroy that does
// not remove, death, faulting commands, the session ending, out of range, the body limit, stop.
#include "Check.h"

#include "Drive.h"
#include "NetFrame.h"
#include "Remote.h"

#include <cmath>
#include <cstring>
#include <map>
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

    void PushEvent(int kind, unsigned short id, const char* name = "", unsigned int b = 0)
    {
        KpEvent e;
        std::memset(&e, 0, sizeof(e));
        e.kind = kind;
        e.a = id;
        e.b = b;
        strcpy_s(e.name, sizeof(e.name), name);
        g_core.events.push_back(e);
    }

    // ------------------------------------------------------------------ fake game

    struct FakeBody
    {
        float pos[3];
        float dest[3];
        float speed;          // asked
        float maxSpeed;       // kept by Update (the body's own is 50)
        bool walking;
        bool exists;          // the hand resolves
        bool identity;        // ... to the body spawned there
        bool destroyed;
        int down;
        bool aiSkip;
    };

    class FakeGame : public IGameAccess
    {
    public:
        FakeGame() : haveLocal(true), epoch(1), paused(false), spawnOk(true), applyOk(true), destroyRemoves(true),
                     spawns(0), destroys(0), verifies(0), worldCalls(0), tick(0)
        {
            std::memset(local, 0, sizeof(local));
        }
        bool LocalPose(NetLocalSample* out)
        {
            if (!haveLocal) return false;
            std::memset(out, 0, sizeof(*out));
            for (int i = 0; i < 3; ++i) { out->pos[i] = local[i]; out->dest[i] = local[i] + 1.0f; }
            out->tick = ++tick;
            out->epoch = epoch;
            out->worldEpoch = epoch;
            out->paused = paused ? 1 : 0;
            return true;
        }
        void Log(const char* line) { lines.push_back(line); }
        void Call() { if (!haveLocal) ++worldCalls; }   // a game call while there is no world

        bool RemoteSpawn(const RemoteConfig&, const char* name, const float* pos, float, RemoteSpawnInfo* out)
        {
            Call();
            ++spawns;
            spawnNames.push_back(name);
            if (!spawnOk) return false;
            int slot = 0;
            while (bodies.count(slot)) ++slot;
            FakeBody b;
            std::memset(&b, 0, sizeof(b));
            for (int i = 0; i < 3; ++i) { b.pos[i] = pos[i]; b.dest[i] = pos[i]; }
            b.maxSpeed = 50.0f;
            b.exists = b.identity = true;
            bodies[slot] = b;
            out->slot = slot;
            strcpy_s(out->faction, sizeof(out->faction), "No Faction");
            out->factionAsAsked = 1;
            return true;
        }
        FakeBody* Get(int slot)
        {
            std::map<int, FakeBody>::iterator it = bodies.find(slot);
            return it == bodies.end() ? 0 : &it->second;
        }
        bool RemoteUpdate(int slot, float maxSpeed, float, BodyState* out)
        {
            Call();
            FakeBody* b = Get(slot);
            if (!b || !b->exists || !b->identity || b->destroyed) return false;
            if (maxSpeed > 0.0f) b->maxSpeed = maxSpeed;
            for (int i = 0; i < 3; ++i) out->pos[i] = b->pos[i];
            out->moving = b->walking ? 1 : 0;
            out->speed = b->walking ? (b->speed < b->maxSpeed ? b->speed : b->maxSpeed) : 0.0f;
            out->maxSpeed = b->maxSpeed;
            out->down = b->down;
            return true;
        }
        bool RemoteApply(int slot, const DriveCmd& c, int)
        {
            Call();
            cmds.push_back(c);
            FakeBody* b = Get(slot);
            if (!b || !applyOk) return false;
            if (c.kind == DC_PLACE || c.kind == DC_SNAP)
                for (int i = 0; i < 3; ++i) b->pos[i] = c.pos[i];
            if (c.kind == DC_WALK)
            {
                for (int i = 0; i < 3; ++i) b->dest[i] = c.pos[i];
                b->speed = c.speed;
                b->walking = true;
            }
            if (c.kind == DC_HALT) b->walking = false;
            return true;
        }
        void RemoteSkipAi(int slot, bool on)
        {
            Call();
            FakeBody* b = Get(slot);
            if (b) b->aiSkip = on;
        }
        bool RemoteDestroy(int slot, const char*)
        {
            Call();
            ++destroys;
            FakeBody* b = Get(slot);
            if (!b || !b->exists) return false;
            b->destroyed = true;
            if (destroyRemoves) b->exists = false;
            return true;
        }
        bool RemoteExists(int slot)
        {
            Call();
            FakeBody* b = Get(slot);
            return b && b->exists && b->identity;
        }
        bool RemoteVerify(int slot)
        {
            Call();
            ++verifies;
            FakeBody* b = Get(slot);
            return b && b->exists && b->identity && !b->destroyed;
        }
        void RemoteFree(int slot) { bodies.erase(slot); ++frees; }
        void RemoteForgetAll() { bodies.clear(); }

        // Walk the bodies toward their destination for dt seconds (min(asked, max) speed).
        void Simulate(float dt)
        {
            for (std::map<int, FakeBody>::iterator it = bodies.begin(); it != bodies.end(); ++it)
            {
                FakeBody& b = it->second;
                if (!b.walking || !b.exists) continue;
                float dx = b.dest[0] - b.pos[0], dz = b.dest[2] - b.pos[2];
                float d = std::sqrt(dx * dx + dz * dz);
                float v = (b.speed < b.maxSpeed ? b.speed : b.maxSpeed) * dt;
                if (d <= v || d < 1e-3f) { b.pos[0] = b.dest[0]; b.pos[2] = b.dest[2]; b.walking = false; continue; }
                b.pos[0] += dx / d * v;
                b.pos[2] += dz / d * v;
            }
        }
        int Live() const
        {
            int n = 0;
            for (std::map<int, FakeBody>::const_iterator it = bodies.begin(); it != bodies.end(); ++it)
                n += it->second.exists && !it->second.destroyed ? 1 : 0;
            return n;
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
        unsigned int epoch;
        bool paused, spawnOk, applyOk, destroyRemoves;
        int spawns, destroys, verifies, worldCalls;
        int frees = 0;
        unsigned long long tick;
        float local[3];
        std::map<int, FakeBody> bodies;
        std::vector<DriveCmd> cmds;
        std::vector<std::string> lines;
        std::vector<std::string> spawnNames;
    };

    // A remote player walking a circle (radius 200, 60 units/s) around the local character: one new pose
    // every 100 ms, ~100 ms old, as the core reports them.
    struct Walker
    {
        unsigned short id;
        float angle, speed, cx;
        bool on;
        explicit Walker(unsigned short i, float centerX = 0.0f) : id(i), angle(0.0f), speed(60.0f), cx(centerX), on(true) {}
        void Step()
        {
            if (!on) return;
            angle += speed * 0.1f / 200.0f;
            KpRemotePose p;
            std::memset(&p, 0, sizeof(p));
            p.playerId = id;
            p.ageMs = 100;
            p.pose.pos[0] = cx + 200.0f * std::sin(angle);
            p.pose.pos[1] = 10.0f;
            p.pose.pos[2] = 200.0f * std::cos(angle);
            p.pose.vel[0] = speed * std::cos(angle);
            p.pose.vel[2] = -speed * std::sin(angle);
            p.pose.yaw = 90.0f;
            p.pose.flags = speed > 0.0f ? KP_POSE_MOVING : 0;
            g_core.poses.push_back(p);
        }
    };

    void Run(NetFrame& nf, FakeGame& g, unsigned long long& now, unsigned long long ms, std::vector<Walker*> walkers)
    {
        for (unsigned long long end = now + ms; now < end; now += 20)
        {
            if (now % 100 == 0)
                for (size_t i = 0; i < walkers.size(); ++i) walkers[i]->Step();
            nf.OnFrame(now);
            g.Simulate(0.02f);
        }
    }

    RemoteConfig Cfg(int drive = DM_ORDER)
    {
        RemoteConfig c;
        RemoteParseConfig("", &c);
        c.drive = drive;
        return c;
    }

    // ------------------------------------------------------------------ tests

    void TestConfig()
    {
        RemoteConfig c;
        RemoteParseConfig("", &c);
        CHECK(c.on == 1 && c.drive == DM_AUTO && std::strcmp(c.faction, "nofac") == 0 && c.maxSpeed == 150.0f);
        CHECK(c.range == 3000.0f && c.settleMs == 3000 && c.squadPermanent == 1 && c.snapDist == 300.0f && c.saveGuard == 1);
        RemoteParseConfig("remote=off\nremote_drive=steer\nremote_faction= Drifters \nremote_maxspeed=0\nremote_range=500\n"
                          "remote_settle=10\nremote_squad_permanent=0\nremote_snap=150\nremote_save_guard=off\n", &c);
        CHECK(c.on == 0 && c.drive == DM_STEER && std::strcmp(c.faction, "Drifters") == 0 && c.maxSpeed == 0.0f);
        CHECK(c.range == 500.0f && c.settleMs == 10000 && c.squadPermanent == 0 && c.snapDist == 150.0f && c.saveGuard == 0);
        RemoteParseConfig("remote=maybe\nremote_drive=fly\nremote_maxspeed=5\nremote_range=1\nremote_settle=-3\n#remote=off\n", &c);
        CHECK(c.on == 1 && c.drive == DM_AUTO && c.maxSpeed == 150.0f && c.range == 3000.0f && c.settleMs == 3000);
    }

    void TestLifecycle()
    {
        KpCoreApi api = MakeApi();
        KpCoreConfig cc;
        NetParseConfig("host=x\n", &cc);
        unsigned long long now = 100000;

        {   // off: never spawns
            FakeGame g;
            NetFrame nf;
            CHECK(nf.Start(&api, cc, &g));
            Walker w(7);
            PushEvent(KP_EV_JOINED, 7, "bob");
            Run(nf, g, now, 6000, std::vector<Walker*>(1, &w));
            CHECK(g.spawns == 0);
            nf.Stop();
            g_core.poses.clear();
        }
        {   // the M4 stand-in on: remote bodies stay off
            FakeGame g;
            NetFrame nf;
            DriveConfig d;
            DriveParseConfig("standin=order\n", &d);
            nf.SetDrive(d);
            nf.SetRemote(Cfg());
            CHECK(nf.Start(&api, cc, &g));
            CHECK(!nf.Bodies().Config().on && g.Logged("remote: off (the M4 stand-in is on"));
            nf.Stop();
        }
        {   // join, settle, spawn, drive, leave
            FakeGame g;
            NetFrame nf;
            nf.SetRemote(Cfg());
            CHECK(nf.Start(&api, cc, &g));
            Walker w(7);
            std::vector<Walker*> ws(1, &w);
            PushEvent(KP_EV_JOINED, 7, "bob");
            Run(nf, g, now, 2500, ws);
            CHECK(g.spawns == 0);   // the world has not settled for 3 s yet
            Run(nf, g, now, 1000, ws);
            CHECK(g.spawns == 1 && g.Live() == 1 && g.spawnNames[0] == "bob");
            CHECK(g.Logged("remote: spawned a body for bob (7)") && g.Logged("faction No Faction, drive order, AI on"));
            const RemoteBodies::Body* b = nf.Bodies().Find(7);
            CHECK(b && b->state == RemoteBodies::RB_LIVE && !g.bodies[0].aiSkip);
            Run(nf, g, now, 20000, ws);
            CHECK(g.spawns == 1 && g.Count(DC_WALK) > 20 && g.bodies[0].maxSpeed == 150.0f);
            b = nf.Bodies().Find(7);
            CHECK(b && b->lastGap < 60.0f);   // the raised max speed lets it keep up with a 60 units/s walker
            CHECK(b && b->stats.snaps <= 1);
            CHECK(g.Logged("remote: bob (7) order body"));
            PushEvent(KP_EV_LEFT, 7, "", 0);
            size_t before = g.cmds.size();
            w.on = false;
            Run(nf, g, now, 500, ws);
            CHECK(g.destroys == 1 && g.Logged("remote: body of bob (7) removed (left)") && g.Logged("remote: body of bob (7) is gone"));
            CHECK(nf.Bodies().Find(7) == 0 && g.bodies.empty());
            CHECK(g.cmds.size() <= before + 1);
            Run(nf, g, now, 5000, ws);
            CHECK(g.destroys == 1 && g.spawns == 1);
            nf.Stop();
            g_core.poses.clear();
        }
        {   // stale: no snapshot for 3 s removes it; poses again respawn it
            FakeGame g;
            NetFrame nf;
            nf.SetRemote(Cfg());
            CHECK(nf.Start(&api, cc, &g));
            Walker w(7);
            std::vector<Walker*> ws(1, &w);
            PushEvent(KP_EV_JOINED, 7, "bob");
            Run(nf, g, now, 4000, ws);
            CHECK(g.Live() == 1);
            w.on = false;
            Run(nf, g, now, 3500, ws);
            CHECK(g.destroys == 1 && g.Logged("removed (no snapshot for 3 s)") && g.Live() == 0);
            w.on = true;
            Run(nf, g, now, 1000, ws);
            CHECK(g.spawns == 2 && g.Live() == 1);
            nf.Stop();
            CHECK(g.destroys == 1);   // Stop forgets without a game call
            g_core.poses.clear();
        }
        {   // the remote's pose ages (it is loading): removed once older than 15 s
            FakeGame g;
            NetFrame nf;
            nf.SetRemote(Cfg());
            CHECK(nf.Start(&api, cc, &g));
            Walker w(7);
            std::vector<Walker*> ws(1, &w);
            PushEvent(KP_EV_JOINED, 7, "bob");
            Run(nf, g, now, 4000, ws);
            CHECK(g.Live() == 1);
            w.on = false;
            KpRemotePose old;
            std::memset(&old, 0, sizeof(old));
            old.playerId = 7;
            old.pose.pos[0] = 10.0f;
            for (unsigned int age = 1000; age <= 16000; age += 500)
            {
                old.ageMs = age;
                g_core.poses.push_back(old);
                Run(nf, g, now, 500, ws);
            }
            CHECK(g.destroys == 1 && g.Logged("removed (its newest pose is older than 15 s)"));
            nf.Stop();
            g_core.poses.clear();
        }
        {   // a world change (load stall or load): no call while there is no world; the hand is checked when it is back
            FakeGame g;
            NetFrame nf;
            nf.SetRemote(Cfg());
            CHECK(nf.Start(&api, cc, &g));
            Walker w(7);
            std::vector<Walker*> ws(1, &w);
            PushEvent(KP_EV_JOINED, 7, "bob");
            Run(nf, g, now, 4000, ws);
            CHECK(g.Live() == 1);
            // A 2 s stall: new world epoch, same world. The hand still resolves: kept, no second body.
            ++g.epoch;
            Run(nf, g, now, 500, ws);
            CHECK(g.verifies == 1 && g.spawns == 1 && g.destroys == 0 && g.Logged("kept 1 body, forgot 0"));
            CHECK(nf.Bodies().Kept() == 1);
            // Menu, then a real load: no game call while there is no own character.
            g.haveLocal = false;
            Run(nf, g, now, 3000, ws);
            CHECK(g.worldCalls == 0 && g.spawns == 1);
            g.bodies[0].identity = false;   // the old hand resolves to someone else in the new world
            g.haveLocal = true;
            ++g.epoch;
            Run(nf, g, now, 100, ws);
            CHECK(g.verifies == 2 && g.destroys == 0 && g.Logged("kept 0 bodies, forgot 1"));
            CHECK(g.spawns == 1);   // not before the settle time
            Run(nf, g, now, 3500, ws);
            CHECK(g.spawns == 2 && g.Live() == 1 && g.destroys == 0);
            nf.Stop();
            g_core.poses.clear();
        }
        {   // a spawn that fails three times: given up on that player
            FakeGame g;
            g.spawnOk = false;
            NetFrame nf;
            nf.SetRemote(Cfg());
            CHECK(nf.Start(&api, cc, &g));
            Walker w(7);
            std::vector<Walker*> ws(1, &w);
            PushEvent(KP_EV_JOINED, 7, "bob");
            Run(nf, g, now, 30000, ws);
            CHECK(g.spawns == 3 && g.Logged("failed 3 times - giving up"));
            const RemoteBodies::Body* b = nf.Bodies().Find(7);
            CHECK(b && b->state == RemoteBodies::RB_GIVEUP);
            nf.Stop();
            g_core.poses.clear();
        }
        {   // a destroy that leaves the body standing: no respawn while it may stand there; asked 3 times, then forgotten
            FakeGame g;
            g.destroyRemoves = false;
            NetFrame nf;
            nf.SetRemote(Cfg());
            CHECK(nf.Start(&api, cc, &g));
            Walker w(7);
            std::vector<Walker*> ws(1, &w);
            PushEvent(KP_EV_JOINED, 7, "bob");
            Run(nf, g, now, 4000, ws);
            w.on = false;
            Run(nf, g, now, 3500, ws);   // stale -> destroy
            CHECK(g.destroys == 1);
            // The fake keeps the body resolving but marks it destroyed; Exists must say so to keep waiting.
            g.bodies[0].destroyed = false;
            w.on = true;
            Run(nf, g, now, 25000, ws);
            CHECK(g.destroys == 3 && g.spawns == 1 && g.Logged("still there 10 s after destroy"));
            Run(nf, g, now, 12000, ws);
            CHECK(g.Logged("did not go away after 3 destroy calls"));
            CHECK(g.spawns == 2);   // then a new one
            nf.Stop();
            g_core.poses.clear();
        }
        {   // death and knock-out replace the body; faulting commands remove it
            FakeGame g;
            NetFrame nf;
            nf.SetRemote(Cfg());
            CHECK(nf.Start(&api, cc, &g));
            Walker w(7);
            std::vector<Walker*> ws(1, &w);
            PushEvent(KP_EV_JOINED, 7, "bob");
            Run(nf, g, now, 4000, ws);
            g.bodies[0].down = 2;
            Run(nf, g, now, 100, ws);
            CHECK(g.destroys == 1 && g.Logged("removed (it died)"));
            Run(nf, g, now, 6000, ws);
            CHECK(g.spawns == 2 && g.Live() == 1);
            int slot = g.bodies.begin()->first;
            g.bodies[slot].down = 1;
            Run(nf, g, now, 9000, ws);
            CHECK(g.destroys == 1);
            Run(nf, g, now, 1500, ws);
            CHECK(g.destroys == 2 && g.Logged("removed (knocked out for 10 s)"));
            Run(nf, g, now, 6000, ws);
            CHECK(g.spawns == 3);
            g.applyOk = false;
            Run(nf, g, now, 3000, ws);
            CHECK(g.destroys == 3 && g.Logged("removed (game calls faulted 3 times in a row)"));
            nf.Stop();
            g_core.poses.clear();
        }
        {   // the session ends: every body goes; a body whose hand stopped resolving is not destroyed
            FakeGame g;
            NetFrame nf;
            nf.SetRemote(Cfg());
            CHECK(nf.Start(&api, cc, &g));
            Walker a(7), b(8, 50.0f);
            std::vector<Walker*> ws;
            ws.push_back(&a);
            ws.push_back(&b);
            PushEvent(KP_EV_JOINED, 7, "bob");
            PushEvent(KP_EV_JOINED, 8, "eve");
            Run(nf, g, now, 4000, ws);
            CHECK(g.spawns == 2 && g.Live() == 2);
            PushEvent(KP_EV_LOST, 0);
            a.on = b.on = false;
            Run(nf, g, now, 500, ws);
            CHECK(g.destroys == 2 && g.Live() == 0 && nf.Bodies().CountState(RemoteBodies::RB_LIVE) == 0);
            nf.Stop();
            g_core.poses.clear();
        }
        {   // steer: the AI is skipped; range and the body limit
            FakeGame g;
            NetFrame nf;
            RemoteConfig c = Cfg(DM_STEER);
            c.range = 1000.0f;
            nf.SetRemote(c);
            CHECK(nf.Start(&api, cc, &g));
            std::vector<Walker*> ws;
            std::vector<Walker> pool;
            pool.reserve(11);
            for (unsigned short i = 0; i < 10; ++i) pool.push_back(Walker((unsigned short)(10 + i), (float)i * 10.0f));
            pool.push_back(Walker(30, 5000.0f));   // far away: never spawned
            for (size_t i = 0; i < pool.size(); ++i) { ws.push_back(&pool[i]); PushEvent(KP_EV_JOINED, pool[i].id, "p"); }
            Run(nf, g, now, 5000, ws);
            CHECK(g.spawns == RemoteBodies::kMaxBodies && g.Live() == RemoteBodies::kMaxBodies);
            CHECK(g.bodies[0].aiSkip && g.Count(DC_STEER) > 0 && g.Count(DC_WALK) == 0);
            // One walks out of range: removed, and a waiting one gets its place.
            pool[0].cx = 3000.0f;
            Run(nf, g, now, 1000, ws);
            CHECK(g.Logged("removed (out of range)") && g.spawns == RemoteBodies::kMaxBodies + 1);
            nf.Stop();
            CHECK(g.bodies.empty());
            g_core.poses.clear();
        }
        {   // auto: steers first (AI skipped); a body that does not move under steering (the fake ignores steer, as a body
            // stuck at a rock) switches to the walk order with its AI on, and back to steering once it is close
            FakeGame g;
            NetFrame nf;
            nf.SetRemote(Cfg(DM_AUTO));
            CHECK(nf.Start(&api, cc, &g));
            Walker w(7);
            std::vector<Walker*> ws(1, &w);
            PushEvent(KP_EV_JOINED, 7, "bob");
            Run(nf, g, now, 3300, ws);
            const RemoteBodies::Body* b = nf.Bodies().Find(7);
            CHECK(b && b->state == RemoteBodies::RB_LIVE && b->mode == DM_STEER && g.bodies[0].aiSkip);
            Run(nf, g, now, 3000, ws);
            b = nf.Bodies().Find(7);
            CHECK(b && b->switches >= 1 && g.Count(DC_WALK) > 0);
            Run(nf, g, now, 20000, ws);
            b = nf.Bodies().Find(7);
            CHECK(b && b->switches >= 2);   // close again: back to steering at least once
            nf.Stop();
            g_core.poses.clear();
        }
        {   // paused: no commands, bodies kept
            FakeGame g;
            NetFrame nf;
            nf.SetRemote(Cfg());
            CHECK(nf.Start(&api, cc, &g));
            Walker w(7);
            std::vector<Walker*> ws(1, &w);
            PushEvent(KP_EV_JOINED, 7, "bob");
            Run(nf, g, now, 4000, ws);
            g.paused = true;
            size_t before = g.cmds.size();
            Run(nf, g, now, 2000, ws);
            CHECK(g.cmds.size() == before && g.Live() == 1);
            nf.Stop();
            g_core.poses.clear();
        }
    }
}

int main()
{
    TestConfig();
    TestLifecycle();
    return TEST_RESULT("kp-remote-test");
}
