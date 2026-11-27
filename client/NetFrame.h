// The frame-thread side of networking (M3): takes the local pose from the game through IGameAccess,
// hands it to the core DLL at 10 Hz, drains the core's events and remote poses each frame and logs
// them. It never calls the game except through IGameAccess, so the test (net/test/CoreTest.cpp)
// runs exactly this code with a fake game. Compiled by VC++ 2010 (the plugin) and MSVC 14.44 (the
// test): keep to C++ both accept (no C++11).
#pragma once

#include "KpCore.h"
#include "Drive.h"

namespace kp
{
    // One local pose as the game adapter reads it (client/Pose.h: PoseSample).
    struct NetLocalSample
    {
        float pos[3];
        float vel[3];            // AbstractMovementBase::currentMotion
        float yawDeg;
        float dest[3];           // AbstractMovementBase::destination
        int moving;              // currentlyMoving
        int paused;
        unsigned long long tick; // when it was sampled (ms); a new tick = a new sample
        unsigned int epoch;      // changes when the local character changes or comes back after a load
        unsigned int worldEpoch; // changes only when the world may have changed (menu, load, load stall)
    };

    // The stand-in body (M4) as the game adapter reports it when it picks one.
    struct StandInInfo
    {
        char name[64];
        float home[3];           // where it stood when picked (it is put back there on release)
        float distance;          // x/z distance to the local character when picked
    };

    // Everything the network side needs from the game. Only called on the frame thread.
    class IGameAccess
    {
    public:
        virtual ~IGameAccess() {}
        // Latest local pose; false when there is none (menu, loading, no character).
        virtual bool LocalPose(NetLocalSample* out) = 0;
        virtual void Log(const char* line) = 0;
        // M4 stand-in: a nearby NPC that is not the player's, driven by NetFrame. Pick stores a handle
        // (never a pointer) and turns the AI skip on; Read re-resolves it (false: gone); Apply carries
        // one command out (false: a game call faulted); SkipAi switches the skip; Release halts it and
        // puts it back home, or, with worldGone, only forgets it (no call into the old world).
        virtual bool StandInPick(const float* /*nearPos*/, const DriveConfig& /*cfg*/, StandInInfo* /*out*/) { return false; }
        virtual bool StandInRead(BodyState* /*out*/) { return false; }
        virtual bool StandInApply(const DriveCmd& /*cmd*/, int /*mode*/) { return false; }
        virtual void StandInSkipAi(bool /*on*/) {}
        virtual void StandInRelease(bool /*worldGone*/) {}
        // M5 (remote bodies): not used yet.
        virtual void SpawnRemote(unsigned short /*playerId*/, const KpPose& /*pose*/) {}
        virtual void MoveRemote(unsigned short /*playerId*/, const KpPose& /*pose*/) {}
        virtual void RemoveRemote(unsigned short /*playerId*/) {}
    };

    // Reads host, port and name from a KenshiPure.cfg text (key=value lines, '#' comments).
    // Returns false when there is no host line (networking off). Defaults: port 27850, name "player".
    bool NetParseConfig(const char* text, KpCoreConfig* cfg);

    // After an epoch change the first sample is dropped, and later ones until the destination is
    // set (the first sample after a load can be half set up: destination 0/0/0, yaw not matching
    // facing; docs/client.md "Own pose (M1)"), at most kSettleMaxMs.
    struct NetStats
    {
        unsigned int posesSent;        // handed to the core
        unsigned int posesUnsettled;   // dropped by the settle rule
        unsigned int posesInvalid;     // non-finite or out of the protocol's range
        unsigned int events;           // core events seen
        unsigned int remoteUpdates;    // remote pose updates seen
    };

    class NetFrame
    {
    public:
        enum { kRemoteLogMs = 1000, kSettleMaxMs = 2000, kMaxPlayers = 64 };
        enum { kPickEveryMs = 2000, kRemoteStaleMs = 3000, kStatusMs = 1000, kMaxApplyFails = 3 };

        NetFrame();
        // Stand-in driving (M4); off unless cfg.mode says otherwise. Call before Start.
        void SetDrive(const DriveConfig& cfg) { drive_ = cfg; }
        bool StandInDriving() const { return siOn_; }
        // Current cycle phase (0-based; fixed modes stay in 0), its planner mode and AI skip.
        int StandInPhase() const { return phase_; }
        int StandInMode() const { return planner_.Mode(); }
        bool StandInAiSkipped() const { return phaseAiSkip_; }
        // Starts the core; false (and logged) if it refuses the config. api and game must outlive Stop.
        bool Start(const KpCoreApi* api, const KpCoreConfig& cfg, IGameAccess* game);
        // Once per frame on the frame thread; nowMs from a monotonic clock. Sends each new local
        // sample (a new tick) once; the game adapter samples at 10 Hz.
        void OnFrame(unsigned long long nowMs);
        // Leaves the server and stops the core (blocks up to ~300 ms).
        void Stop();
        bool Running() const { return core_ != 0; }
        const NetStats& Stats() const { return stats_; }
        // Latest remote pose of a player seen by this frame side; false if none.
        bool Remote(unsigned short playerId, KpRemotePose* out) const;

    private:
        struct Player
        {
            unsigned short id;          // 0 = free slot
            char name[KP_CORE_NAME_MAX];
            bool havePose;
            KpRemotePose last;
            unsigned long long lastLog;
            bool logged;
        };
        Player* Find(unsigned short id);
        const Player* Find(unsigned short id) const;
        Player* Add(unsigned short id, const char* name);
        void SendLocal(unsigned long long nowMs);
        void Drain(unsigned long long nowMs);
        void Logf(const char* fmt, ...);
        void DriveFrame(unsigned long long nowMs);
        void StartPhase(unsigned long long nowMs, int phase);
        void LogPhaseSummary(const char* why);
        void Release(unsigned long long nowMs, bool worldGone, const char* why);
        void ForgetTrack(unsigned short playerId);

        const KpCoreApi* api_;
        KpCore* core_;
        IGameAccess* game_;
        NetStats stats_;
        unsigned long long lastTick_;
        bool haveEpoch_;
        unsigned int epoch_;
        unsigned long long epochStart_;
        bool firstOfEpochDropped_;
        bool settled_;
        Player players_[kMaxPlayers];

        // Latest local sample (for the stand-in), set each frame by SendLocal.
        bool localOk_;
        float localPos_[3];
        unsigned int localEpoch_;
        bool localPaused_;

        DriveConfig drive_;
        RemoteTrack track_;
        unsigned short trackId_;         // the remote player driving the stand-in (the first with a pose)
        DrivePlanner planner_;
        bool siOn_;
        StandInInfo si_;
        unsigned int siEpoch_;
        unsigned long long nextPick_, lastStatus_, lastNoPickLog_, phaseStart_;
        int phase_;
        bool phaseAiSkip_;
        int applyFails_;
        float lastGap_;
        BodyState lastBody_;
        DriveCmd lastCmd_;
    };
}
