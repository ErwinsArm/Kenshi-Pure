// The frame-thread side of networking (M3): takes the local pose from the game through IGameAccess,
// hands it to the core DLL at 10 Hz, drains the core's events and remote poses each frame and logs
// them. It never calls the game except through IGameAccess, so the test (net/test/CoreTest.cpp)
// runs exactly this code with a fake game. Compiled by VC++ 2010 (the plugin) and MSVC 14.44 (the
// test): keep to C++ both accept (no C++11).
#pragma once

#include "KpCore.h"

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
    };

    // Everything the network side needs from the game. Only called on the frame thread.
    class IGameAccess
    {
    public:
        virtual ~IGameAccess() {}
        // Latest local pose; false when there is none (menu, loading, no character).
        virtual bool LocalPose(NetLocalSample* out) = 0;
        virtual void Log(const char* line) = 0;
        // M4/M5 (stand-in and remote bodies): not used yet.
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

        NetFrame();
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
    };
}
