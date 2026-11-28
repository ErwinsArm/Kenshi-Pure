// KenshiPure side of the optional cheats (CheatHost.cpp): loads KenshiPureCheats.dll if it is next
// to KenshiPure.dll and forwards the frame to it. See docs/client.md "Cheat window".
// Built with the VC++ 2010 (v100) toolset; keep to C++ that compiler accepts.
#pragma once

#include "KpHost.h"

class GameWorld;

namespace kp
{
    // Loads and starts KenshiPureCheats.dll when present (logs either way). Call once from kpStart.
    void CheatInit(const KpHost* host);
    // Frame listener part (game thread, after the game's frame). connected: welcomed by a multiplayer server;
    // from then on the cheats stay off for the rest of the game run.
    void CheatOnFrame(GameWorld* gw, bool connected);
}
