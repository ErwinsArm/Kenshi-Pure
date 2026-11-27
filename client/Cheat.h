// KenshiPure cheat hotkeys: the user's single-player testing tool (heal, damage, knock out, spawn
// item, teleport). Off unless mods\KenshiPure\KenshiPure-cheats.cfg holds cheats=1. See docs/client.md "Cheat hotkeys".
// Built with the VC++ 2010 (v100) toolset; keep to C++ that compiler accepts.
#pragma once

class GameWorld;

namespace kp
{
    // Reads KenshiPure-cheats.cfg (cheats, cheat_item, cheat_weapon_maker) and checks the rows the cheats
    // need; logs the keys or why the cheats are off. Call once from kpStart.
    void CheatInit();
    // Frame listener part (game thread, after the game's frame): polls the hotkeys and runs them.
    void CheatOnFrame(GameWorld* gw);
}
