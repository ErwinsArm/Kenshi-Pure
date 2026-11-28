// M5 game adapter for remote bodies (see RemoteGame.cpp; the logic is Remote.h). Frame thread only.
// Built with the VC++ 2010 (v100) toolset.
#pragma once

#include "Remote.h"

namespace kp
{
    // Checks the rows, installs the AI-skip detour (steer) and the save skip (saves leave our squads out). false (logged): remote bodies off.
    bool RemoteGameInit(const RemoteConfig& cfg);
    bool RemoteGameSpawn(const RemoteConfig& cfg, const char* playerName, const float* pos, float yaw, RemoteSpawnInfo* out);
    // wantSpeed > 0: hold that desired speed (speed orders RUN) every frame.
    bool RemoteGameUpdate(int slot, float maxSpeed, float wantSpeed, BodyState* out);
    bool RemoteGameApply(int slot, const DriveCmd& cmd, int mode);
    void RemoteGameSkipAi(int slot, bool on);
    bool RemoteGameDestroy(int slot, const char* reason);
    bool RemoteGameExists(int slot);
    bool RemoteGameVerify(int slot);
    void RemoteGameFree(int slot);
    void RemoteGameForgetAll();
    // Destroys characters of our faction named "[KP] ..." that are not ours (an older build's save kept them); how many.
    int RemoteGameSweep();
    // 1: pos lies in a loaded zone (as ActivePlatoon::unloadCheck tests), 0: not, -1: unknown.
    int RemoteGameLoadedAt(const float* pos);
}
