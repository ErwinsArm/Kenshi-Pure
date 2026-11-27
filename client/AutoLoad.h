// Test helper: loads a save once at the main menu when <plugin dir>\autoload.txt names one (the
// file is deleted when read, so a normal game start is never affected). scripts/run-game-test.ps1
// writes it. Frame thread only. Built with the VC++ 2010 (v100) toolset.
#pragma once

class GameWorld;

namespace kp
{
    // Reads and deletes autoload.txt (call once at start).
    void AutoLoadInit();
    // Each frame: once the main menu has been up for a few seconds, asks the game to load the save.
    void AutoLoadOnFrame(GameWorld* gw);
}
