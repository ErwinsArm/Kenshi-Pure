// KenshiPure client plugin: build check, address layer, per-frame hook, own pose (M1), live probe.
// Started by KenshiPureLoader.dll (an Ogre plugin) through kpStart (KpHost.h). Built with KenshiLib
// 0.5.0's headers (declarations only; KenshiLib is not linked or loaded) and the VC++ 2010 (v100)
// x64 toolset, /MD, so every C++ object crossing into Kenshi has Kenshi's own layout and heap.
// Addresses: our own table client/sites.tsv (Sites.h). See docs/client.md.

#include <windows.h>
#include <bcrypt.h>
#include <float.h>
#include <string>

#include "Game.h"
#include "KpHost.h"
#include "Hooks.h"
#include "Log.h"
#include "Display.h"
#include "Probe.h"
#include "Pose.h"

#define KP_VERSION "0.0.3-m1"

namespace
{
    // Frame and once-per-second state; touched only on the frame thread.
    unsigned long long g_frames = 0;
    unsigned long long g_framesAtLastLine = 0;
    ULONGLONG g_lastLine = 0;
    bool g_threadLogged = false;
    DWORD g_frameThread = 0;

    // ABI proof state.
    enum AbiState { ABI_PENDING, ABI_PASS, ABI_FAIL };
    AbiState g_abi = ABI_PENDING;
    int g_abiGoodSeconds = 0;
    double g_lastStamp = -1.0;

    __declspec(thread) int t_inHandler = 0;

    // ---------------------------------------------------------------- build check

    // Resolves the address table for the exe the loader hashed. Returns false if we must stay
    // inactive (a required row is unresolved).
    bool BuildCheck(const KpHost* host)
    {
        char exeUtf8[MAX_PATH * 3] = "?";
        if (host->exePath) WideCharToMultiByte(CP_UTF8, 0, host->exePath, -1, exeUtf8, sizeof(exeUtf8), NULL, NULL);
        kp::Log("build: exe %s, sha256 %s", exeUtf8, host->exeSha256 && host->exeSha256[0] ? host->exeSha256 : "unknown");
        if (!kp::SitesResolve(host->exePath, host->exeSha256 && host->exeSha256[0] ? host->exeSha256 : NULL))
        {
            kp::Log("build: a required address is unresolved - staying inactive");
            return false;
        }
        kp::ProbeSetBuild(kp::SitesKnownBuild() ? "known (sites.tsv)" : "unknown (pattern search)", kp::SitesBuildName());
        kp::Log("build: %s build %s - active", kp::SitesKnownBuild() ? "known" : "unknown", kp::SitesBuildName());
        return true;
    }

    // ---------------------------------------------------------------- crash logging

    LONG CALLBACK OnException(EXCEPTION_POINTERS* info)
    {
        DWORD code = info->ExceptionRecord->ExceptionCode;
        if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
            code != EXCEPTION_STACK_OVERFLOW)
            return EXCEPTION_CONTINUE_SEARCH;
        static volatile LONG s_count = 0;
        if (t_inHandler || InterlockedIncrement(&s_count) > 20) return EXCEPTION_CONTINUE_SEARCH;
        t_inHandler = 1;
        void* at = info->ExceptionRecord->ExceptionAddress;
        HMODULE mod = NULL;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(at), &mod))
        {
            char full[MAX_PATH];
            if (GetModuleFileNameA(mod, full, MAX_PATH))
            {
                const char* base = strrchr(full, '\\');
                strncpy_s(name, base ? base + 1 : full, _TRUNCATE);
            }
        }
        unsigned long long rva = mod ? (unsigned long long)((char*)at - (char*)mod) : (unsigned long long)at;
        kp::LogTry("exception (first chance, may be handled): code 0x%08lx in %s+0x%llx", code, name, rva);
        t_inHandler = 0;
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // ---------------------------------------------------------------- frame hook and ABI proof

    // Thread that owns the game's main window (the thread Kenshi's UI runs on).
    DWORD g_windowThread = 0;
    BOOL CALLBACK FindGameWindow(HWND w, LPARAM)
    {
        DWORD pid = 0;
        DWORD tid = GetWindowThreadProcessId(w, &pid);
        if (pid == GetCurrentProcessId() && IsWindowVisible(w) && GetWindow(w, GW_OWNER) == NULL)
        {
            g_windowThread = tid;
            return FALSE;
        }
        return TRUE;
    }

    struct AbiSample
    {
        int sameWorld;   // the global GameWorld (row "ou") is the GameWorld* Kenshi passed to the hook
        float speed;     // GameWorld::getFrameSpeedMultiplier()
        int paused;      // GameWorld::isPaused()
        double stamp;    // GameWorld::getTimeStamp()
    };

    // POD only, so a fault in a game call is caught here instead of reaching Kenshi.
    bool SampleAbi(GameWorld* gw, AbiSample* s)
    {
        __try
        {
            s->sameWorld = (kp::GameWorldGlobal() == gw) ? 1 : 0;
            s->speed = gw->getFrameSpeedMultiplier();
            s->paused = gw->isPaused() ? 1 : 0;
            s->stamp = gw->getTimeStamp();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void AbiProof(GameWorld* gw)
    {
        if (g_abi == ABI_FAIL) return;
        AbiSample s;
        if (!SampleAbi(gw, &s))
        {
            g_abi = ABI_FAIL;
            kp::Log("abi: FAIL - a game call faulted; no more game calls this run");
            return;
        }
        bool speedOk = _finite(s.speed) && s.speed >= 0.0f && s.speed <= 20.0f;
        bool stampOk = _finite(s.stamp) && s.stamp >= 0.0 && (g_lastStamp < 0.0 || s.stamp >= g_lastStamp);
        kp::Log("abi: ou==gw %s, frameSpeed %.3f, paused %d, timeStamp %.3f (%+.3f) - %s",
                s.sameWorld ? "yes" : "NO", s.speed, s.paused, s.stamp,
                g_lastStamp < 0.0 ? 0.0 : s.stamp - g_lastStamp,
                (s.sameWorld && speedOk && stampOk) ? "ok" : "unexpected");
        g_lastStamp = s.stamp;
        if (!(s.sameWorld && speedOk && stampOk))
        {
            g_abi = ABI_FAIL;
            kp::Log("abi: FAIL - value out of the expected range (ou==gw, 0 <= frameSpeed <= 20, timeStamp not decreasing)");
            return;
        }
        if (g_abi == ABI_PENDING && ++g_abiGoodSeconds >= 5)
        {
            g_abi = ABI_PASS;
            kp::Log("abi: PASS - 5 seconds of plausible values from game calls on the frame thread");
        }
    }

    // Frame listener: runs on the game thread after the game's own frame (Hooks.cpp).
    void OnFrame(GameWorld* gw, float time)
    {
        ++g_frames;
        if (!g_threadLogged)
        {
            g_threadLogged = true;
            g_frameThread = GetCurrentThreadId();
            EnumWindows(FindGameWindow, 0);
            kp::Log("frame: first tick on thread %lu; game window thread %lu (%s)", g_frameThread, g_windowThread,
                    g_frameThread == g_windowThread ? "same thread" : "DIFFERENT thread");
        }
        ULONGLONG now = GetTickCount64();
        if (now - g_lastLine >= 1000)
        {
            kp::Log("tick: frames %llu (+%llu), time arg %.4f", g_frames, g_frames - g_framesAtLastLine, time);
            g_framesAtLastLine = g_frames;
            g_lastLine = now;
            AbiProof(gw);
        }
        if (g_abi != ABI_FAIL) kp::PoseOnFrame(gw);
        if (g_abi != ABI_FAIL && kp::FeatureOn("optional:probe")) kp::ProbeOnFrame(gw, g_frames, g_frameThread);
    }

    bool AnarchyLoaded()
    {
        return GetModuleHandleW(L"AnarchyKenshi.dll") || GetModuleHandleW(L"AnarchyLoader.dll") ||
               GetModuleHandleW(L"AnarchyCore.dll");
    }
}

// Called by KenshiPureLoader.dll (the Ogre plugin) while Ogre builds its Root, before the game window
// exists. Returns 1 when active.
extern "C" __declspec(dllexport) int kpStart(const KpHost* host)
{
    kp::LogOpen();
    kp::Log("KenshiPure " KP_VERSION " loaded (built " __DATE__ " " __TIME__ ", MSVC %d)", _MSC_FULL_VER);
    if (!host || host->version != KP_HOST_VERSION || host->size < sizeof(KpHost))
    {
        kp::Log("host: loader interface version %u (size %u) - this KenshiPure needs version %d; staying inactive",
                host ? host->version : 0, host ? host->size : 0, KP_HOST_VERSION);
        return 0;
    }
    kp::Log("host: %s; display fix %s", host->loaderBuild ? host->loaderBuild : "?",
            host->displayHook ? "armed early by the loader" : "not armed by the loader");
    AddVectoredExceptionHandler(0, OnException);

    if (AnarchyLoaded())
    {
        kp::Log("AnarchyKenshi is loaded in this game - staying inactive (switch it off for KenshiPure runs)");
        return 0;
    }
    // Fallback minimize-restore only when the loader could not arm the early DXGI fix (measured in the
    // first loader run: started this early it found no real window yet and minimized nothing useful).
    if (!host->displayHook) kp::DisplayFixStart();
    else kp::Log("display: early DXGI fix armed by the loader - no minimize-restore fallback");
    if (!BuildCheck(host)) return 0;
    if (!kp::SitesKnownBuild())
    {
        // A pattern hit in an unmeasured build is not proof enough to patch code: a wrong hit would
        // put a detour into the wrong function. Only the loader's display fix runs there.
        kp::Log("build: unknown build - no game hooks; only the display fix runs");
        return 0;
    }

    if (!kp::OnFrame(&OnFrame) || !kp::FrameHookInstall())
    {
        kp::Log("frame: hook FAILED - staying inactive");
        return 0;
    }
    kp::Log("frame: hook installed; waiting for the first frame");
    kp::PoseInit();
    if (kp::FeatureOn("optional:probe")) kp::ProbeStart();
    else kp::Log("probe: off (its addresses are not all resolved for this build)");
    return 1;
}
