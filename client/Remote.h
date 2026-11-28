// M5: one own body per remote player, as pure logic (no game calls): when to spawn it, how to drive it
// (Drive.h's track and planner), when to remove it, and forgetting every body without a game call when
// the local world goes away (menu, save load, load stall). The game adapter (RemoteGame.cpp) keeps one
// hand per slot and carries the calls out; NetFrame feeds events and poses. Tested without the game by
// net/test/RemoteTest.cpp. Compiled by VC++ 2010 (the plugin) and MSVC 14.44 (the test): keep to C++
// both accept (no C++11). docs/client.md "Remote bodies (M5)".
#pragma once

#include "KpCore.h"
#include "Drive.h"

namespace kp
{
    class IGameAccess;

    struct RemoteConfig
    {
        int on;              // cfg remote=on|off (default on; NetFrame keeps it off while the M4 stand-in runs)
        int drive;           // DM_AUTO (default), DM_ORDER or DM_STEER; cfg remote_drive=auto|order|steer
        char faction[64];    // string id of the bodies' faction; cfg remote_faction (default nofac = "No Faction")
        float maxSpeed;      // the body's max speed, kept every frame; cfg remote_maxspeed (default 150, 0 = its own)
        float range;         // spawn only this close (x/z) to the local character; cfg remote_range (default 3000)
        unsigned int settleMs;   // no spawn sooner than this after a world change; cfg remote_settle (s, default 3)
        int squadPermanent;  // Faction::createNewEmptyActivePlatoon's permanent flag; cfg remote_squad_permanent (default 0:
                             // the game destroys an empty or unloaded temporary squad, a permanent one stays as an
                             // unloaded squad, 1.0.65 Faction::updateActivePlatoons 0x6ba884..0x6ba916, M)
        float snapDist;      // snap when the body is this far off; cfg remote_snap (default 300)
        int saveGuard;       // saves skip our squads (Platoon::serialiseEverything detour); cfg remote_save_guard (default 1)
    };

    // Reads the remote* keys of a KenshiPure.cfg text; unknown or bad values keep the defaults.
    void RemoteParseConfig(const char* text, RemoteConfig* cfg);

    // What the game adapter reports about a body it spawned.
    struct RemoteSpawnInfo
    {
        int slot;                // the adapter's slot holding the body's hand (>= 0)
        char faction[64];        // the faction the body ended up in (read back)
        int factionAsAsked;      // 1 if that is the faction it was built for
    };

    struct RemoteBodyStats
    {
        unsigned int samples, moving, snaps, orders, down;
        float gapSum, gapMax;        // x/z gap to the drive target (the remote pose 150 ms ago)
        float lagSum, lagMax;        // x/z gap to the newest remote pose carried to now (what a viewer sees as lag)
        float speedSum;              // body speed while the remote moves
        unsigned int speedSamples;
    };

    class RemoteBodies
    {
    public:
        enum
        {
            kMaxBodies = 8,
            kMaxPlayers = 64,
            kStaleRecvMs = 3000,     // no snapshot for this long: remove (server or connection gone quiet)
            kStaleAgeMs = 15000,     // the remote's newest pose is this old: remove (it is loading or stalled)
            kRemoveWaitMs = 10000,   // after destroy: wait until the hand stops resolving, at most this long
            kRetryMs = 5000,         // after a failed spawn or a lost body
            kMaxSpawnFails = 3,      // then give up on that player (logged)
            kMaxApplyFails = 3,
            kMaxDestroyTries = 3,    // destroy again after each kRemoveWaitMs while the hand still resolves
            kDownMaxMs = 10000,      // knocked out this long: replace it
            kStatusMs = 1000,
            kRangeSlack = 500,       // remove when farther than range + this
            kOutsideMs = 1000        // the body stands outside the loaded zones this long: remove (the game would unload
                                     // its squad after 4.5 s, ActivePlatoon::unloadCheck 1.0.65 0x4fe28d, M)
        };
        enum State { RB_FREE = 0, RB_WAIT, RB_LIVE, RB_REMOVING, RB_GIVEUP };

        struct Body
        {
            unsigned short id;           // remote player id; 0 = free entry
            char name[KP_CORE_NAME_MAX];
            int state;
            bool left;                   // the player left (or the session ended): delete once no body is left
            int slot;                    // the adapter's slot while LIVE / REMOVING, else -1
            RemoteTrack track;
            DrivePlanner planner;
            unsigned long long stateAt, lastRecv, nextTry, lastStatus;
            unsigned int lastAge;
            int spawnFails, applyFails;
            bool aiSkipped;
            int mode;                    // the planner mode now (DM_ORDER or DM_STEER; auto switches between them)
            unsigned long long condSince;   // auto: since when the switch condition holds (0 = not)
            unsigned int switches;
            bool verify;                 // the world may have changed: check the hand once the world is back
            int destroyTries;
            unsigned long long downSince;
            unsigned long long outSince; // since when the body stands outside the loaded zones (0 = inside)
            bool waitLogged;             // "waiting for a loaded zone" was logged for this wait
            BodyState body;
            DriveCmd lastCmd;
            float lastGap, lastLag;
            RemoteBodyStats stats;
        };

        RemoteBodies();
        void Configure(const RemoteConfig& cfg) { cfg_ = cfg; }
        const RemoteConfig& Config() const { return cfg_; }
        void SetGame(IGameAccess* game) { game_ = game; }

        // Call every frame before the events: the local world as the pose reader sees it. A missing own
        // character or a new world epoch makes no game call; once the world is back each body's hand is checked
        // (resolves, not a player character, its name and faction as spawned): kept if it passes, else forgotten.
        // A load stall of 2 s also starts a new world epoch (Pose.cpp), so forgetting blindly would leave live bodies.
        void World(unsigned long long nowMs, bool localOk, unsigned int worldEpoch, const float* localPos, bool paused);
        void OnJoined(unsigned short id, const char* name);
        void OnLeft(unsigned short id, const char* why);
        // The session ended or restarted (connection lost, a new Welcome): every remote player is gone.
        void OnSessionEnd(const char* why);
        void OnPose(unsigned long long nowMs, const KpRemotePose& p, const char* name);
        // Spawns, drives and removes; once per frame after the events.
        void Frame(unsigned long long nowMs);
        // Networking stops (game exit): forget everything, no game call.
        void Stop();

        const Body* Find(unsigned short id) const;
        int CountState(int state) const;
        unsigned int Spawns() const { return spawns_; }
        unsigned int Removes() const { return removes_; }
        unsigned int Forgets() const { return forgets_; }
        unsigned int Kept() const { return kept_; }
        unsigned int Sweeps() const { return sweeps_; }

    private:
        Body* FindMutable(unsigned short id);
        Body* Add(unsigned short id, const char* name);
        void Free(Body* b);
        void Logf(const char* fmt, ...);
        void Remove(Body* b, unsigned long long nowMs, const char* why);
        void ForgetAll(const char* why);
        void MarkVerify();
        void VerifyAll();
        void Forget(Body* b);
        void LogSummary(const Body* b, const char* why);
        bool TrySpawn(Body* b, unsigned long long nowMs);
        void Drive(Body* b, unsigned long long nowMs);
        void SetMode(Body* b, int mode, bool halt);

        RemoteConfig cfg_;
        IGameAccess* game_;
        Body bodies_[kMaxPlayers];
        bool worldLive_;
        bool sweepPending_;              // a new world: sweep once it has settled
        unsigned int sweeps_;
        bool haveEpoch_;
        unsigned int epoch_;
        unsigned long long worldSince_;
        float localPos_[3];
        bool paused_;
        unsigned long long now_;
        unsigned int spawns_, removes_, forgets_, kept_;
    };
}
