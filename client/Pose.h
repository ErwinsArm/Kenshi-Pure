// M1: the local character's pose (position, yaw, velocity, walking), read on the frame thread.
// Built with the VC++ 2010 (v100) toolset; keep to C++ that compiler accepts.
#pragma once

class GameWorld;

namespace kp
{
    struct PoseSample
    {
        char name[64];
        int selected;          // 1: the player's selected character; 0: first of the player's characters
        unsigned int owned;    // number of the player's characters
        float pos[3];          // Character::getPosition()
        float quat[4];         // RootObject::getOrientation(): w, x, y, z
        float yawDeg;          // from quat (PoseMath.h)
        float facing[3];       // AbstractMovementBase::direction
        float motion[3];       // AbstractMovementBase::currentMotion (unit not measured yet)
        float speed;           // AbstractMovementBase::currentSpeed
        float desiredSpeed;    // AbstractMovementBase::desiredSpeed
        int moving;            // AbstractMovementBase::currentlyMoving
        float dest[3];         // AbstractMovementBase::destination
        float pathDest[3];     // AbstractMovementBase::pathDestination (AK's "destination" at +0xe8)
        int walking;           // AK's rule: x/z distance^2 from pos to pathDest > 4
        int paused;
        unsigned long long tick;   // GetTickCount64() when sampled
        unsigned int epoch;        // +1 whenever the local character changes or comes back after menu/loading
        unsigned int worldEpoch;   // +1 only when it comes back after menu/loading or a load stall (not on a click on another own character)
    };

    // Checks the rows M1 needs; logs and returns false when one is unresolved.
    bool PoseInit();
    // Frame listener part: samples at 10 Hz, logs once a second. Game thread only.
    void PoseOnFrame(GameWorld* gw);
    // Latest valid sample; false if none (menu, loading, no character).
    bool PoseLatest(PoseSample* out);
}
