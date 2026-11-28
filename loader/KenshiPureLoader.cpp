// KenshiPureLoader.dll: the Ogre plugin that starts KenshiPure, early and without any other mod.
// Plugins_x64.cfg holds `Plugin=mods/KenshiPure/KenshiPureLoader` (after `Plugin=RE_Kenshi` when that
// is there, else before the first Plugin= line), so Ogre loads us while the Root is being built,
// before the render system and the game window exist (scripts/install-client.ps1 adds the line).
// Order: log pid/exe/hash -> AnarchyKenshi check -> display fix armed -> exit hook -> KenshiPure.dll kpStart().
// Built with the VC++ 2010 (v100) toolset, /MD (Kenshi's CRT); keep to C++ that compiler accepts.

#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <MinHook.h>

#include "KpHost.h"
#include "Log.h"
#include "Display.h"
#include "DxgiFix.h"

#define KPL_VERSION "0.0.4-m3"

namespace
{
    char g_sha[65] = "";
    wchar_t g_exe[MAX_PATH] = L"";
    char g_build[160] = "";
    KpHost g_host;
    HMODULE g_kpDll = NULL;   // KenshiPure.dll, pinned once started

    // KenshiPure's kpStop, once: from the end of the game's render loop or from dllStopPlugin.
    void StopKenshiPure(const char* why)
    {
        static volatile LONG stopped = 0;
        if (InterlockedExchange(&stopped, 1) != 0) return;
        kp::Log("loader: %s - stopping KenshiPure", why);
        // KenshiPure.dll is pinned (never unloaded), so its kpStop is still there; older builds have none.
        if (!g_kpDll) return;
        typedef void (*KpStopFn)();
        KpStopFn stop = (KpStopFn)GetProcAddress(g_kpDll, "kpStop");
        if (stop) stop();
    }

    // Exit hook. Kenshi never destroys Ogre's Root, so Ogre never calls dllStopPlugin (docs/client.md
    // "Exit"). Quitting (title screen Exit, the window's close button) sets the game's quit flag; the
    // frame callback then returns false, Ogre::Root::startRendering returns to Renderer::go and
    // KingOfRenderThread::startup, WinMain returns and the C runtime calls exit(). startRendering is
    // the game's render loop, called once (after the Root and our plugin exist), on the thread that runs
    // every frame: when it returns no frame runs any more and every thread is still alive, so the
    // network thread can still send the leave.
    typedef void (*StartRenderingFn)(void* root);
    StartRenderingFn g_startRenderingOrig = NULL;

    // Shader compile count and time (docs/client.md "Start time without RE_Kenshi"): Ogre's D3D11 render
    // system imports D3DCompile from the game's D3DCompiler_43.dll; Kenshi never switches Ogre's microcode
    // cache on, so every start compiles every shader again (RE_Kenshi keeps its own cache). Counting only.
    typedef HRESULT (WINAPI* D3DCompileFn)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR, UINT, UINT, void**, void**);
    D3DCompileFn g_d3dCompileOrig = NULL;
    volatile LONG g_compiles = 0;
    volatile LONGLONG g_compileTicks = 0;
    LARGE_INTEGER g_qpf;

    HRESULT WINAPI D3DCompileDetour(LPCVOID src, SIZE_T size, LPCSTR name, const void* defines, void* include, LPCSTR entry,
                                    LPCSTR target, UINT flags1, UINT flags2, void** code, void** errors)
    {
        LARGE_INTEGER a, b;
        QueryPerformanceCounter(&a);
        HRESULT r = g_d3dCompileOrig(src, size, name, defines, include, entry, target, flags1, flags2, code, errors);
        QueryPerformanceCounter(&b);
        InterlockedIncrement(&g_compiles);
        InterlockedExchangeAdd64(&g_compileTicks, b.QuadPart - a.QuadPart);
        return r;
    }

    void LogCompiles(const char* when)
    {
        if (!g_d3dCompileOrig) return;
        kp::Log("loader: shaders compiled %s: %ld, %.1f s in D3DCompile", when, g_compiles,
                g_qpf.QuadPart ? (double)g_compileTicks / g_qpf.QuadPart : 0.0);
    }

    void ArmCompileCounter()
    {
        QueryPerformanceFrequency(&g_qpf);
        // The game's own copy (the exe's folder comes first in the search order); the render system,
        // loaded after us, binds to this same module.
        HMODULE c = LoadLibraryW(L"D3DCompiler_43.dll");
        void* target = c ? (void*)GetProcAddress(c, "D3DCompile") : NULL;
        if (!target) { kp::Log("loader: no D3DCompiler_43.dll D3DCompile - shader compiles not counted"); return; }
        MH_STATUS s = MH_CreateHook(target, (void*)&D3DCompileDetour, reinterpret_cast<void**>(&g_d3dCompileOrig));
        if (s == MH_OK) s = MH_EnableHook(target);
        if (s != MH_OK) g_d3dCompileOrig = NULL;
        kp::Log("loader: D3DCompile at %p %s", target, s == MH_OK ? "hooked (counting shader compiles)" : MH_StatusToString(s));
    }

    void StartRenderingDetour(void* root)
    {
        LogCompiles("before the render loop");
        g_startRenderingOrig(root);
        LogCompiles("by the end of the render loop");
        StopKenshiPure("the game's render loop ended (the game is closing)");
    }

    void ArmExitHook()
    {
        HMODULE ogre = GetModuleHandleW(L"OgreMain_x64.dll");
        void* target = ogre ? (void*)GetProcAddress(ogre, "?startRendering@Root@Ogre@@QEAAXXZ") : NULL;
        if (!target) { kp::Log("loader: OgreMain_x64.dll has no Root::startRendering export - no exit hook"); return; }
        MH_STATUS s = MH_CreateHook(target, (void*)&StartRenderingDetour, reinterpret_cast<void**>(&g_startRenderingOrig));
        if (s == MH_OK) s = MH_EnableHook(target);
        kp::Log("loader: Ogre::Root::startRendering at %p %s", target, s == MH_OK ? "hooked (exit hook)" : MH_StatusToString(s));
    }

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

    // <game>\ = two folders above this DLL (<game>\mods\KenshiPure\KenshiPureLoader.dll).
    void GameDir(wchar_t* out, size_t n)
    {
        _snwprintf_s(out, n, _TRUNCATE, L"%s", kp::PluginDir());
        for (int i = 0; i < 2; ++i)   // drop "KenshiPure\", then "mods\"
        {
            size_t len = wcslen(out);
            if (len) out[len - 1] = 0;
            wchar_t* slash = wcsrchr(out, L'\\');
            if (slash) slash[1] = 0;
        }
    }

    // Is AnarchyKenshi switched on in Plugins_x64.cfg? Read from the file, not from loaded modules:
    // AnarchyKenshi may be listed after us and load later.
    bool AnarchyInCfg(const wchar_t* gameDir)
    {
        wchar_t path[MAX_PATH];
        _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%sPlugins_x64.cfg", gameDir);
        FILE* f = NULL;
        if (_wfopen_s(&f, path, L"r") != 0 || !f) { kp::Log("loader: cannot read %ls", path); return false; }
        char line[512];
        bool on = false;
        while (fgets(line, sizeof(line), f))
        {
            const char* p = line;
            while (*p == ' ' || *p == '\t') ++p;
            if (_strnicmp(p, "Plugin=", 7) != 0) continue;
            for (const char* q = p; *q; ++q)
                if (_strnicmp(q, "AnarchyLoader", 13) == 0) on = true;
        }
        fclose(f);
        return on;
    }

    bool AnarchyLoaded()
    {
        return GetModuleHandleW(L"AnarchyKenshi.dll") || GetModuleHandleW(L"AnarchyLoader.dll") ||
               GetModuleHandleW(L"AnarchyCore.dll");
    }

    void Start()
    {
        kp::LogOpen(L"KenshiPureLoader");
        _snprintf_s(g_build, sizeof(g_build), _TRUNCATE, "KenshiPureLoader " KPL_VERSION " (built " __DATE__ " " __TIME__ ", MSVC %d)", _MSC_FULL_VER);
        kp::Log("%s loaded as an Ogre plugin; pid %lu", g_build, GetCurrentProcessId());

        // Stay loaded for the life of the process (hooks and KenshiPure point into us).
        HMODULE self = NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&Start), &self);

        DWORD n = GetModuleFileNameW(NULL, g_exe, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) g_exe[0] = 0;
        unsigned long long size = 0;
        if (g_exe[0] && Sha256File(g_exe, g_sha, &size)) kp::Log("loader: exe %ls, size %llu, sha256 %s", g_exe, size, g_sha);
        else { g_sha[0] = 0; kp::Log("loader: exe %ls - could not hash it", g_exe); }
        kp::Log("loader: RE_Kenshi %s", GetModuleHandleW(L"RE_Kenshi.dll") ? "is loaded" : "is not loaded");

        wchar_t game[MAX_PATH];
        GameDir(game, MAX_PATH);
        if (AnarchyInCfg(game) || AnarchyLoaded())
        {
            kp::Log("loader: AnarchyKenshi is switched on in Plugins_x64.cfg - KenshiPure stays off (switch one of them off)");
            return;
        }

        MH_STATUS mh = MH_Initialize();
        if (mh != MH_OK) kp::Log("loader: MinHook init failed (%s)", MH_StatusToString(mh));
        bool found = false;
        bool full = kp::DisplayCfgFullScreen(&found);
        int displayHook = 0;
        if (!found) kp::Log("dxgi: no 'Full Screen=' in kenshi.cfg - display fix off");
        else if (!full) kp::Log("dxgi: kenshi.cfg says windowed - display fix off");
        else if (kp::DisplayFixOff()) kp::Log("dxgi: display_fix=0 in KenshiPure.cfg - display fix off");
        else if (mh == MH_OK && kp::DxgiFixArm()) displayHook = 1;
        if (mh == MH_OK) ArmExitHook();
        if (mh == MH_OK) ArmCompileCounter();

        wchar_t dll[MAX_PATH];
        _snwprintf_s(dll, MAX_PATH, _TRUNCATE, L"%sKenshiPure.dll", kp::PluginDir());
        HMODULE kpDll = LoadLibraryExW(dll, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        KpStartFn start = kpDll ? (KpStartFn)GetProcAddress(kpDll, "kpStart") : NULL;
        if (!start) { kp::Log("loader: cannot load %ls or its kpStart (error %lu)", dll, GetLastError()); return; }
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(start), &kpDll);

        ZeroMemory(&g_host, sizeof(g_host));
        g_host.size = sizeof(g_host);
        g_host.version = KP_HOST_VERSION;
        g_host.loaderBuild = g_build;
        g_host.exeSha256 = g_sha;
        g_host.exePath = g_exe;
        g_host.displayHook = displayHook;
        g_kpDll = kpDll;
        int active = start(&g_host);
        kp::Log("loader: KenshiPure.dll started (%s); see KenshiPure.log", active ? "active" : "inactive");
    }
}

// Ogre calls these by name (OgreDynLib getSymbol) for each Plugin= line.
extern "C" __declspec(dllexport) void dllStartPlugin()
{
    // One start per process, even if Plugins_x64.cfg lists us twice.
    static volatile LONG started = 0;
    if (InterlockedExchange(&started, 1) != 0) return;
    Start();
}

extern "C" __declspec(dllexport) void dllStopPlugin()
{
    StopKenshiPure("Ogre unloads its plugins (game is closing)");
}
