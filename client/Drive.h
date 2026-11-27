// M4: driving a stand-in body from a remote player's poses, as pure logic (no game calls): the
// remote pose track with interpolation ~150 ms behind, and the planner that turns target and body
// state into one command per frame (place, snap, walk, steer, halt). The game adapter (StandIn.cpp)
// carries the commands out; NetFrame owns the stand-in's lifecycle. Tested without the game by
// net/test/DriveTest.cpp. Compiled by VC++ 2010 (the plugin) and MSVC 14.44 (the test): keep to C++
// both accept (no C++11).
#pragma once

#include "KpCore.h"

namespace kp
{
    // path: CharMovement::setDestination (the movement controller); order: Character::setDestination (the
    // click-to-move order, a TASK_MOVE job ticked by the AI); steer: setDirectMovement each frame.
    enum DriveMode { DM_OFF = 0, DM_PLACE, DM_PATH, DM_ORDER, DM_STEER, DM_CYCLE };
    enum DriveAiSkip { DA_NONE = 0, DA_AI, DA_CHARACTER };

    struct DriveConfig
    {
        int mode;           // DriveMode; cfg standin=off|place|path|order|steer|cycle (default off)
        int aiSkip;         // DriveAiSkip; cfg standin_ai=ai|character|none (default ai)
        float snapDist;     // x/z gap that snaps the body onto the target; cfg standin_snap (default 150)
        float pickDist;     // stand-in must be this close to the local character; cfg standin_range (default 600)
        char name[64];      // cfg standin_name: pick this NPC by exact name (case-insensitive); empty = nearest
        unsigned int settleMs;   // no pick sooner than this after a world change; cfg standin_settle (s, default 0)
    };

    // Reads the standin* keys of a KenshiPure.cfg text; unknown values keep the defaults.
    void DriveParseConfig(const char* text, DriveConfig* cfg);
    const char* DriveModeName(int mode);

    struct TrackSample
    {
        unsigned long long t;   // local ms when the pose was taken (receive time - age)
        float pos[3];
        float vel[3];
        float yaw;              // degrees, 0 = +z, 90 = +x
        int moving;
        int paused;
    };

    // Recent poses of one remote player, oldest first.
    class RemoteTrack
    {
    public:
        enum { kMax = 16, kExtrapolateMs = 300 };
        RemoteTrack() { Clear(); }
        void Clear();
        // A pose as the core reports it at local time nowMs. A repeat of the newest pose is ignored.
        void Add(unsigned long long nowMs, const KpRemotePose& p);
        bool Empty() const { return n_ == 0; }
        int Count() const { return n_; }
        unsigned long long LastAddMs() const { return lastAdd_; }
        // Pose at local time t: interpolated between the samples around t, before the first = the first,
        // after the newest = extrapolated along its velocity for at most kExtrapolateMs, then held.
        bool At(unsigned long long t, TrackSample* out) const;

    private:
        TrackSample s_[kMax];
        int n_;
        unsigned long long lastAdd_;
    };

    enum DriveCmdKind { DC_NONE = 0, DC_PLACE, DC_SNAP, DC_WALK, DC_STEER, DC_HALT };

    struct DriveCmd
    {
        int kind;          // DriveCmdKind
        float pos[3];      // PLACE / SNAP: where; WALK: destination
        float yaw;         // PLACE / SNAP
        float speed;       // WALK / STEER: wanted speed (units per game second)
        float vel[3];      // STEER: wanted velocity (x/z, y = 0)
        float limit;       // STEER: speed limit
    };

    struct BodyState
    {
        float pos[3];
        float speed;       // AbstractMovementBase::currentSpeed
        int moving;        // currentlyMoving
        // For the log only: what limits its speed. CharMovement::update passes min(desired, max) on
        // (1.0.65 0x65f5c7, M); max is set from the character each periodic update (0x66036c, M).
        float desired;     // desiredSpeed (+0xBC)
        float maxSpeed;    // maxSpeed (+0xB4)
        float walkSpeed;   // walkSpeed (+0xC0): what MoveSpeed WALK sets desired to (0x6646e6, M)
        int speedOrders;   // MoveSpeed: 0 walk, 1 jog (desired 55), 2 run / 3 grouped (999), 4 no change
    };

    struct DriveStats
    {
        unsigned int samples, moving, snaps, orders;
        float gapSum, gapMax;
    };

    // One mode's rules (place, path or steer; cycle is NetFrame's schedule of these).
    class DrivePlanner
    {
    public:
        enum
        {
            kRenderDelayMs = 150,   // target = the remote pose this long ago
            kPlaceEveryMs = 100,
            kSnapCooldownMs = 1000,
            kReissueMs = 1000
        };
        DrivePlanner();
        // New mode (or the same one again): the next Step halts the body once, stats start over.
        void Reset(int mode, float snapDist);
        int Mode() const { return mode_; }
        // One frame: the command for the body. gap (x/z distance body -> target) is returned too.
        DriveCmd Step(unsigned long long nowMs, const BodyState& body, const TrackSample& target, float* gap);
        const DriveStats& Stats() const { return stats_; }

    private:
        DriveCmd Walk(unsigned long long nowMs, const float dest[3], float speed);
        int mode_;
        float snapDist_;
        bool haltPending_;
        bool halted_;
        bool haveOrder_;
        float orderPos_[3];
        unsigned long long orderAt_, lastPlace_, lastSnap_;
        bool snapped_;
        DriveStats stats_;
    };

    float DistXZ(const float a[3], const float b[3]);
}
