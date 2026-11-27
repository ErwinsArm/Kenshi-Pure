// KenshiPure's hook dispatcher: MinHook 1.3.4 underneath, one detour per hooked function, and
// listeners behind it so several features can share one game function.
// Built with the VC++ 2010 (v100) toolset; keep to C++ that compiler accepts.
#pragma once

class GameWorld;

namespace kp
{
    // Puts a detour on `target` and enables it; *original receives the trampoline. Refuses (and
    // logs) a target that already has one of our detours, or one MinHook cannot handle.
    bool HookInstall(const char* name, void* target, void* detour, void** original);
    // Same for a resolved code row of the address table.
    bool HookSite(int siteId, void* detour, void** original);

    // Frame: called on the game thread after the game's own frame (GameWorld main loop).
    typedef void (*FrameListener)(GameWorld* gw, float time);
    bool OnFrame(FrameListener listener);       // max 8 listeners
    bool FrameHookInstall();                    // the one detour on the main loop
}
