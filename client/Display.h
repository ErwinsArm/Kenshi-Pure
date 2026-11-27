// KenshiPure client: display fix. Kenshi can start full screen in a smaller, stretched mode until the
// player presses Alt+Enter. Our plugin loads after the swap chain exists, so we cannot change how it
// is created; instead, once the game window is up and in front, we minimize and restore it once
// (what AnarchyKenshi does after start, docs/findings/AnarchyKenshi.dll.md section 5).
// Built with the VC++ 2010 (v100) toolset; keep to C++ that compiler accepts.
#pragma once

namespace kp
{
    // Starts the worker thread if kenshi.cfg asks for full screen and KenshiPure.cfg does not hold
    // display_fix=0. Uses only Win32 window calls; never touches the game's objects.
    void DisplayFixStart();
}
