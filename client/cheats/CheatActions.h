// KenshiPureCheats.dll: the cheat actions (CheatActions.cpp), used by the window (CheatWindow.cpp).
// All functions run on the game thread (from the frame listener). Each action writes its result to
// the log and to LastResult(); true = something changed.
// Built with the VC++ 2010 (v100) toolset; keep to C++ that compiler accepts.
#pragma once

#include <stddef.h>

class GameWorld;

namespace kp
{
    namespace cheat
    {
        bool Init();                 // reads KenshiPure-cheats.cfg, checks the address features
        int ItemCount();             // item names for the window's list
        const char* ItemName(int i);
        const char* LastResult();
        void SelectedName(GameWorld* gw, char* out, size_t outSize);   // "" when no own character is selected

        bool Heal(GameWorld* gw);
        bool Damage(GameWorld* gw);
        bool KnockOut(GameWorld* gw);
        bool Teleport(GameWorld* gw);                          // to the camera centre
        bool Spawn(GameWorld* gw, const char* key, int count);  // name or string ID, count 1..100
        void OnFrame(GameWorld* gw);                           // the teleport check
    }
}
