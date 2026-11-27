// M4 game adapter: picks a nearby NPC as the stand-in, keeps it by hand (never a pointer across
// frames), skips its own AI tick, and carries out NetFrame's drive commands (Drive.h). Frame thread
// only; every game call inside __try. Built with the VC++ 2010 (v100) toolset.
#pragma once

#include "Drive.h"

namespace kp
{
    struct StandInInfo;

    // Checks the rows the stand-in needs and installs the chosen AI-skip detour. On false the
    // stand-in stays off (logged) and cfg->mode is set to DM_OFF.
    bool StandInInit(DriveConfig* cfg);
    bool StandInPickNpc(const float* nearPos, const DriveConfig& cfg, StandInInfo* out);
    bool StandInReadBody(BodyState* out);
    bool StandInApplyCmd(const DriveCmd& cmd, int mode);
    void StandInSetSkip(bool on);
    void StandInDrop(bool worldGone);
}
