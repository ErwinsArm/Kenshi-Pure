// G10 (M6): while connected to a server the game runs at x1 and the player cannot pause it (the user's rule).
// Detours on GameWorld::userPause, setGameSpeed and togglePause (feature optional:speedlock-write); only the game's
// loading pauses (zone loading, save load) and a new game's character editor still pause. See docs/client.md "Game speed (M6)".
// Built with the VC++ 2010 (v100) toolset; keep to C++ that compiler accepts.
#pragma once

class GameWorld;

namespace kp
{
    // Installs the three detours (logs; the lock stays off when a row or a detour is missing). Call once from kpStart.
    void SpeedLockInit();
    // Frame listener part (game thread). connected: welcomed by a server this frame; from the first such frame on the
    // lock holds until the game exits.
    void SpeedLockOnFrame(GameWorld* gw, bool connected);
}
