// M5 game adapter for remote bodies (see RemoteGame.cpp; the logic is Remote.h). Frame thread only.
// Built with the VC++ 2010 (v100) toolset.
#pragma once

#include "Remote.h"

namespace kp
{
    // Checks the rows, installs the AI-skip detour (steer) and the save guard. false (logged): remote bodies off.
    bool RemoteGameInit(const RemoteConfig& cfg);
    bool RemoteGameSpawn(const RemoteConfig& cfg, const char* playerName, const float* pos, float yaw, RemoteSpawnInfo* out);
    bool RemoteGameUpdate(int slot, float maxSpeed, BodyState* out);
    bool RemoteGameApply(int slot, const DriveCmd& cmd, int mode);
    void RemoteGameSkipAi(int slot, bool on);
    bool RemoteGameDestroy(int slot, const char* reason);
    bool RemoteGameExists(int slot);
    bool RemoteGameVerify(int slot);
    void RemoteGameFree(int slot);
    void RemoteGameForgetAll();
    // Destroys characters named "[KP] ..." that are not ours (a save kept them); returns how many. Logged.
    int RemoteGameSweep();
}
