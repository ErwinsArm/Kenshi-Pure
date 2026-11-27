// KenshiPure's hook dispatcher (see Hooks.h).

#include <windows.h>
#include <MinHook.h>

#include "Hooks.h"
#include "Sites.h"
#include "Log.h"

namespace
{
    CRITICAL_SECTION g_lock;
    bool g_init = false;
    const int kMaxHooks = 64;
    void* g_targets[kMaxHooks];
    int g_count = 0;

    bool Init()
    {
        if (g_init) return true;
        InitializeCriticalSection(&g_lock);
        MH_STATUS s = MH_Initialize();
        if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED)
        {
            kp::Log("hooks: MinHook init failed (%s)", MH_StatusToString(s));
            return false;
        }
        g_init = true;
        return true;
    }

    // Main loop: one detour, listeners behind it.
    const int kMaxListeners = 8;
    kp::FrameListener g_listeners[kMaxListeners];
    volatile LONG g_listenerCount = 0;
    void (*g_mainLoopOrig)(GameWorld*, float) = NULL;

    void MainLoopDetour(GameWorld* gw, float time)
    {
        g_mainLoopOrig(gw, time);   // Kenshi's own frame first; listeners only observe after it
        LONG n = g_listenerCount;
        for (LONG i = 0; i < n; ++i) g_listeners[i](gw, time);
    }
}

namespace kp
{
    bool HookInstall(const char* name, void* target, void* detour, void** original)
    {
        if (!Init()) return false;
        EnterCriticalSection(&g_lock);
        bool ok = false;
        for (int i = 0; i < g_count; ++i)
            if (g_targets[i] == target)
            {
                Log("hooks: %s at %p already has a detour - add a listener instead", name, target);
                LeaveCriticalSection(&g_lock);
                return false;
            }
        MH_STATUS s = g_count < kMaxHooks ? MH_CreateHook(target, detour, original) : MH_ERROR_MEMORY_ALLOC;
        if (s == MH_OK) s = MH_EnableHook(target);
        if (s == MH_OK)
        {
            g_targets[g_count++] = target;
            ok = true;
            Log("hooks: %s at %p hooked", name, target);
        }
        else Log("hooks: %s at %p NOT hooked (%s)", name, target, MH_StatusToString(s));
        LeaveCriticalSection(&g_lock);
        return ok;
    }

    bool HookSite(int siteId, void* detour, void** original)
    {
        void* target = SiteAddress(siteId);
        if (!target || g_sites[siteId].kind != SK_CODE)
        {
            Log("hooks: %s is not a resolved code row - not hooked", g_sites[siteId].name);
            return false;
        }
        return HookInstall(g_sites[siteId].name, target, detour, original);
    }

    bool OnFrame(FrameListener listener)
    {
        if (g_listenerCount >= kMaxListeners) return false;
        g_listeners[g_listenerCount] = listener;
        MemoryBarrier();
        InterlockedIncrement(&g_listenerCount);
        return true;
    }

    bool FrameHookInstall()
    {
        return HookSite(KPS_GameWorld__mainLoop, (void*)&MainLoopDetour, reinterpret_cast<void**>(&g_mainLoopOrig));
    }
}
