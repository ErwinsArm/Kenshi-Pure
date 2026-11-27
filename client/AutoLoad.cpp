// See AutoLoad.h. Built with the VC++ 2010 (v100) toolset; keep to C++ that compiler accepts.
#include "AutoLoad.h"
#include "Log.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>

#include "Game.h"

namespace
{
    char g_save[256] = "";
    bool g_pending = false;
    unsigned long long g_menuSince = 0;
    const unsigned long long kMenuWaitMs = 5000;   // let the main menu finish building first

    __declspec(noinline) void CallLoad(SaveManager* sm, const char* name)
    {
        std::string s(name);
        sm->load(s);
    }

    // 1 asked, 0 no save manager yet, -1 faulted.
    int TryLoad(const char* name)
    {
        __try
        {
            SaveManager* sm = SaveManager::getSingleton();
            if (!sm) return 0;
            CallLoad(sm, name);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    // Kenshi's start dialog (video settings, Mods, OK) runs in this process after the plugins load.
    // For a test run nobody is there to press OK: find a visible top-level window of this process
    // with a button "OK" and click it, once, within 60 s.
    BOOL CALLBACK FindOk(HWND w, LPARAM out)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        if (pid != GetCurrentProcessId() || !IsWindowVisible(w)) return TRUE;
        HWND ok = FindWindowExW(w, NULL, L"Button", L"OK");
        if (!ok) ok = FindWindowExW(w, NULL, L"Button", L"&OK");
        if (!ok) return TRUE;
        *reinterpret_cast<HWND*>(out) = ok;
        return FALSE;
    }

    // A test run should not cover the user's main monitor: put the game window on a monitor that is
    // not the primary one (if there is one).
    BOOL CALLBACK FindOther(HMONITOR m, HDC, LPRECT, LPARAM out)
    {
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(m, &mi) && !(mi.dwFlags & MONITORINFOF_PRIMARY))
        {
            *reinterpret_cast<RECT*>(out) = mi.rcMonitor;
            return FALSE;
        }
        return TRUE;
    }
    // The game window: a visible top-level window of this process that is not the start dialog
    // (no OK button) and at least 640 pixels wide.
    BOOL CALLBACK FindGameWindow(HWND w, LPARAM out)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        if (pid != GetCurrentProcessId() || !IsWindowVisible(w) || GetWindow(w, GW_OWNER)) return TRUE;
        if (FindWindowExW(w, NULL, L"Button", L"OK") || FindWindowExW(w, NULL, L"Button", L"&OK")) return TRUE;
        RECT r;
        if (!GetClientRect(w, &r) || r.right - r.left < 640) return TRUE;
        *reinterpret_cast<HWND*>(out) = w;
        return FALSE;
    }

    // GameWorld::mainLoop (our frame hook) does not run at the main menu, and an Ogre frame listener
    // added at plugin start never fired there either (both measured). The game window's messages are
    // handled on the game's main thread, also at the menu: a posted message runs the load there.
    const UINT kMsgAutoLoad = WM_APP + 0x4b50;
    WNDPROC g_origProc = NULL;
    LRESULT CALLBACK AutoLoadProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == kMsgAutoLoad)
        {
            kp::AutoLoadOnFrame(kp::GameWorldGlobal());
            return 0;
        }
        return CallWindowProcW(g_origProc, w, msg, wp, lp);
    }

    DWORD WINAPI AutoLoadThread(LPVOID)
    {
        bool pressed = false;
        HWND game = NULL;
        for (int i = 0; i < 3000 && g_pending; ++i)   // at most ~5 minutes
        {
            Sleep(100);
            if (!pressed)
            {
                HWND ok = NULL;
                EnumWindows(FindOk, reinterpret_cast<LPARAM>(&ok));
                if (ok)
                {
                    Sleep(500);   // let the dialog finish building
                    PostMessageW(ok, BM_CLICK, 0, 0);
                    pressed = true;
                    kp::Log("autoload: pressed OK in Kenshi's start dialog");
                }
                continue;
            }
            if (!game)
            {
                EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&game));
                if (!game) continue;
                RECT other = { 0, 0, 0, 0 };
                EnumDisplayMonitors(NULL, NULL, FindOther, reinterpret_cast<LPARAM>(&other));
                if (other.right != other.left)
                {
                    SetWindowPos(game, NULL, other.left + 40, other.top + 40, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                    kp::Log("autoload: game window moved to the second monitor");
                }
                g_origProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(game, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&AutoLoadProc)));
                if (!g_origProc)
                {
                    kp::Log("autoload: cannot hook the game window's messages - not loading");
                    return 0;
                }
                continue;
            }
            if (i % 10 == 0) PostMessageW(game, kMsgAutoLoad, 0, 0);   // handled only while the main thread pumps
        }
        return 0;
    }

    bool InWorld(GameWorld* gw)
    {
        __try
        {
            PlayerInterface* pi = KP_FIELD(gw, GameWorld, player);
            return pi && KP_FIELD(pi, PlayerInterface, playerCharacters).count > 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

namespace kp
{
    void AutoLoadInit()
    {
        wchar_t path[MAX_PATH];
        _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%sautoload.txt", PluginDir());
        FILE* f = NULL;
        if (_wfopen_s(&f, path, L"rb") != 0 || !f) return;
        size_t n = fread(g_save, 1, sizeof(g_save) - 1, f);
        fclose(f);
        g_save[n] = 0;
        while (n > 0 && (g_save[n - 1] == '\r' || g_save[n - 1] == '\n' || g_save[n - 1] == ' ')) g_save[--n] = 0;
        DeleteFileW(path);   // one shot: the next normal start must not load anything
        if (!g_save[0]) return;
        if (!SiteOk(KPS_SaveManager__getSingleton) || !SiteOk(KPS_SaveManager__load_name_))
        {
            Log("autoload: off (SaveManager rows unresolved for this build)");
            return;
        }
        g_pending = true;
        Log("autoload: will load save \"%s\" at the main menu", g_save);
        HANDLE t = CreateThread(NULL, 0, AutoLoadThread, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }

    void AutoLoadOnFrame(GameWorld* gw)
    {
        if (!g_pending) return;
        unsigned long long now = GetTickCount64();
        if (InWorld(gw))
        {
            g_pending = false;   // already in a world (someone loaded by hand)
            Log("autoload: already in a world - not loading");
            return;
        }
        if (g_menuSince == 0) g_menuSince = now;
        if (now - g_menuSince < kMenuWaitMs) return;
        g_pending = false;
        int r = TryLoad(g_save);
        Log("autoload: load \"%s\" %s", g_save, r == 1 ? "asked" : (r == 0 ? "skipped (no save manager)" : "FAULTED"));
    }
}
