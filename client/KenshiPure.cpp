// KenshiPure client plugin, milestone M0: build check, per-frame hook, one harmless game call.
// Loaded by RE_Kenshi from mods/KenshiPure/ (RE_Kenshi.json); built against KenshiLib 0.5.0 with
// the VC++ 2010 (v100) x64 toolset, /MD, so every C++ object crossing into Kenshi has Kenshi's
// own layout and heap. See docs/plan-first-playable.md (M0) and docs/findings/kenshi-build.md.

#include <windows.h>
#include <bcrypt.h>
#include <float.h>
#include <string>

#include <core/Functions.h>
#include <kenshi/Kenshi.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>

#include "Log.h"
#include "Probe.h"

#define KP_VERSION "0.0.1-m0"

namespace
{
    // SHA-256 of kenshi_x64.exe builds we have measured (docs/findings/kenshi-build.md).
    struct KnownBuild { const char* sha256; const char* name; };
    const KnownBuild kKnownBuilds[] = {
        { "504b362cde850d56afb1cea6f5b7b0ee014d9dd7b47e188599d91c804502cd3e", "1.0.65 Steam (RE_Kenshi 0.3.5 copy)" },
        { "a596ab4e407c67b58599c54ffb32dc1bf2b64510cdebd3fa9359ef05a576aeb1", "1.0.68 Steam" },
    };

    void (*g_mainLoopOrig)(GameWorld*, float) = NULL;

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

    bool Sha256File(const wchar_t* path, char out[65], unsigned long long* size)
    {
        bool ok = false;
        BCRYPT_ALG_HANDLE alg = NULL;
        BCRYPT_HASH_HANDLE hash = NULL;
        HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                               OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (f == INVALID_HANDLE_VALUE) return false;
        *size = 0;
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 &&
            BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0) == 0)
        {
            static unsigned char buf[1 << 16];
            DWORD got = 0;
            ok = true;
            while (ReadFile(f, buf, sizeof(buf), &got, NULL) && got > 0)
            {
                *size += got;
                if (BCryptHashData(hash, buf, got, 0) != 0) { ok = false; break; }
            }
            unsigned char digest[32];
            if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0)
            {
                static const char hex[] = "0123456789abcdef";
                for (int i = 0; i < 32; ++i) { out[2 * i] = hex[digest[i] >> 4]; out[2 * i + 1] = hex[digest[i] & 15]; }
                out[64] = 0;
            }
            else ok = false;
        }
        if (hash) BCryptDestroyHash(hash);
        if (alg) BCryptCloseAlgorithmProvider(alg, 0);
        CloseHandle(f);
        return ok;
    }

    // Logs exe path, size, hash and KenshiLib's version. Returns false if we must stay inactive.
    bool BuildCheck()
    {
        wchar_t exe[MAX_PATH];
        DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) { kp::Log("build: cannot read the exe path"); return false; }
        char exeUtf8[MAX_PATH * 3];
        WideCharToMultiByte(CP_UTF8, 0, exe, -1, exeUtf8, sizeof(exeUtf8), NULL, NULL);
        kp::Log("build: exe %s", exeUtf8);

        char sha[65] = "";
        unsigned long long size = 0;
        const char* known = NULL;
        if (Sha256File(exe, sha, &size))
        {
            for (size_t i = 0; i < sizeof(kKnownBuilds) / sizeof(kKnownBuilds[0]); ++i)
                if (strcmp(sha, kKnownBuilds[i].sha256) == 0) known = kKnownBuilds[i].name;
            kp::Log("build: exe size %llu, sha256 %s (%s)", size, sha, known ? known : "not a build we measured");
        }
        else kp::Log("build: could not hash the exe");

        KenshiLib::BinaryVersion v = KenshiLib::GetKenshiVersion();
        KenshiLib::BinaryVersion::KenshiPlatform platform = v.GetPlatform();
        std::string version = v.GetVersion();
        kp::Log("build: KenshiLib reports Kenshi %s %s", version.c_str(), v.GetPlatformStr().c_str());

        if (platform == KenshiLib::BinaryVersion::UNKNOWN || version.empty())
        {
            kp::Log("build: unknown build - staying inactive");
            return false;
        }
        kp::ProbeSetBuild(known ? known : "not a build we measured", version.c_str());
        kp::Log("build: %s build - active", known ? "known" : "KenshiLib-supported (hash not measured by us)");
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
        int sameWorld;   // KenshiLib's global `ou` is the GameWorld* Kenshi passed to the hook
        float speed;     // GameWorld::getFrameSpeedMultiplier()
        int paused;      // GameWorld::isPaused()
        double stamp;    // GameWorld::getTimeStamp()
    };

    // POD only, so a fault in a game call is caught here instead of reaching Kenshi.
    bool SampleAbi(GameWorld* gw, AbiSample* s)
    {
        __try
        {
            s->sameWorld = (ou == gw) ? 1 : 0;
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
            kp::Log("abi: PASS - 5 seconds of plausible values from KenshiLib calls on the frame thread");
        }
    }

    void MainLoopHook(GameWorld* gw, float time)
    {
        g_mainLoopOrig(gw, time);  // Kenshi's own frame first; we only observe

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
        if (g_abi != ABI_FAIL) kp::ProbeOnFrame(gw, g_frames, g_frameThread);
    }

    bool AnarchyLoaded()
    {
        return GetModuleHandleW(L"AnarchyKenshi.dll") || GetModuleHandleW(L"AnarchyLoader.dll") ||
               GetModuleHandleW(L"AnarchyCore.dll");
    }
}

// RE_Kenshi finds this by its C++ name (?startPlugin@@YAXXZ); do not make it extern "C".
__declspec(dllexport) void startPlugin()
{
    kp::LogOpen();
    kp::Log("KenshiPure " KP_VERSION " loaded (built " __DATE__ " " __TIME__ ", MSVC %d)", _MSC_FULL_VER);
    AddVectoredExceptionHandler(0, OnException);

    if (AnarchyLoaded())
    {
        kp::Log("AnarchyKenshi is loaded in this game - staying inactive (switch it off for KenshiPure runs)");
        return;
    }
    if (!BuildCheck()) return;

    // Take the function's address from KenshiLib.dll's export, not `&GameWorld::...`: without
    // whole-program optimisation the compiler hands GetRealAddress a jump stub in our own DLL,
    // which KenshiLib rejects with an assertion box (measured in the first M0 run).
    HMODULE kl = GetModuleHandleW(L"KenshiLib.dll");
    void* exported = kl ? (void*)GetProcAddress(kl, "?_NV_mainLoop_GPUSensitiveStuff@GameWorld@@QEAAXM@Z") : NULL;
    if (!exported)
    {
        kp::Log("frame: KenshiLib export of GameWorld::_NV_mainLoop_GPUSensitiveStuff not found - staying inactive");
        return;
    }
    intptr_t target = KenshiLib::GetRealAddress(exported);
    HMODULE exe = GetModuleHandleW(NULL);
    if (target == 0)
    {
        kp::Log("frame: KenshiLib could not map the main loop - staying inactive");
        return;
    }
    kp::Log("frame: GameWorld::_NV_mainLoop_GPUSensitiveStuff at exe+0x%llx",
            (unsigned long long)(target - (intptr_t)exe));
    if (KenshiLib::AddHook(target, (void*)&MainLoopHook, &g_mainLoopOrig) != KenshiLib::SUCCESS)
    {
        kp::Log("frame: hook FAILED - staying inactive");
        return;
    }
    kp::Log("frame: hook installed; waiting for the first frame");
    kp::ProbeStart();
}
