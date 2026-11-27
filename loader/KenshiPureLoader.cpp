// KenshiPureLoader.dll: the Ogre plugin that starts KenshiPure, early and without any other mod.
// Plugins_x64.cfg holds `Plugin=mods/KenshiPure/KenshiPureLoader` (after `Plugin=RE_Kenshi` when that
// is there, else before the first Plugin= line), so Ogre loads us while the Root is being built,
// before the render system and the game window exist (scripts/install-client.ps1 adds the line).
// Order: log pid/exe/hash -> AnarchyKenshi check -> display fix armed -> KenshiPure.dll kpStart().
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

#define KPL_VERSION "0.0.2-m0b"

namespace
{
    char g_sha[65] = "";
    wchar_t g_exe[MAX_PATH] = L"";
    char g_build[160] = "";
    KpHost g_host;

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
    kp::Log("loader: Ogre unloads its plugins (game is closing)");
}
