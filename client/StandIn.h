// M4 game adapter: picks a nearby NPC as the stand-in, keeps it by hand (never a pointer across
// frames), skips its own AI tick, and carries out NetFrame's drive commands (Drive.h). Frame thread
// only; every game call inside __try. Built with the VC++ 2010 (v100) toolset.
#pragma once

#include "Drive.h"

#define KP_SKIP_REMOTE 16

class Character;

namespace kp
{
    struct StandInInfo;

    // Checks the rows the stand-in needs and installs the chosen AI-skip detour. On false the
    // stand-in stays off (logged) and cfg->mode is set to DM_OFF.
    // The AI::periodicUpdate detour shared by the stand-in and the M5 remote bodies: installed once; entries
    // 1..KP_SKIP_REMOTE of its skip list belong to RemoteGame.cpp (AI* or NULL, written on the frame thread).
    bool AiSkipInstall();
    void AiSkipSet(int index, void* ai);
    long AiSkippedTicks();
    // One drive command on a character (M4's calls, shared with the M5 bodies); inside __try, false on a fault.
    bool DriveApplyTo(Character* c, const DriveCmd& cmd, int mode);

    bool StandInInit(DriveConfig* cfg);
    bool StandInPickNpc(const float* nearPos, const DriveConfig& cfg, StandInInfo* out);
    bool StandInReadBody(BodyState* out);
    bool StandInApplyCmd(const DriveCmd& cmd, int mode);
    void StandInSetSkip(bool on);
    void StandInDrop(bool worldGone);
}
