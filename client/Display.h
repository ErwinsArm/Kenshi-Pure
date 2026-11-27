// KenshiPure client: display fix, fallback part. Kenshi can start full screen in a smaller, stretched
// mode until the player presses Alt+Enter. The real fix is in KenshiPureLoader (DXGI hook before the
// swap chain exists, loader/DxgiFix.cpp); this fallback, once the game window is up and in front,
// minimizes and restores it once if its client area still differs from the desktop mode (what
// AnarchyKenshi does after start, docs/findings/AnarchyKenshi.dll.md section 5).
// Built with the VC++ 2010 (v100) toolset; keep to C++ that compiler accepts.
#pragma once

namespace kp
{
    // Starts the worker thread if kenshi.cfg asks for full screen and KenshiPure.cfg does not hold
    // display_fix=0. Uses only Win32 window calls; never touches the game's objects.
    void DisplayFixStart();
    // kenshi.cfg "Full Screen=Yes"? *found is false when the key is missing.
    bool DisplayCfgFullScreen(bool* found);
    // display_fix=0 in KenshiPure.cfg (next to the DLL that logs)?
    bool DisplayFixOff();
}
