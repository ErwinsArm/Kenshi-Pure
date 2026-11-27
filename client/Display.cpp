// KenshiPure client: display fix (see Display.h). Runs on its own thread and only uses Win32 window
// calls (ShowWindowAsync, which posts to the window's own thread, so we never block the game).

#include <windows.h>
#include <process.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "Display.h"
#include "Log.h"

namespace
{
    struct FindState { HWND best; long long area; };

    BOOL CALLBACK FindGameWindow(HWND w, LPARAM lp)
    {
        FindState* s = reinterpret_cast<FindState*>(lp);
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        if (pid != GetCurrentProcessId() || !IsWindowVisible(w) || GetWindow(w, GW_OWNER) != NULL) return TRUE;
        RECT r;
        if (!GetWindowRect(w, &r)) return TRUE;
        long long area = (long long)(r.right - r.left) * (r.bottom - r.top);
        if (area > s->area) { s->area = area; s->best = w; }
        return TRUE;
    }

    HWND GameWindow()
    {
        FindState s = { NULL, 0 };
        EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&s));
        return s.best;
    }

    // Window, client and monitor sizes plus DPI, so the log shows whether the picture was scaled.
    void LogState(HWND w, const char* when)
    {
        RECT wr = { 0 }, cr = { 0 };
        GetWindowRect(w, &wr);
        GetClientRect(w, &cr);
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        HMONITOR mon = MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST);
        GetMonitorInfoW(mon, &mi);
        DEVMODEW dm;
        ZeroMemory(&dm, sizeof(dm));
        dm.dmSize = sizeof(dm);
        MONITORINFOEXW mix;
        mix.cbSize = sizeof(mix);
        bool haveMode = GetMonitorInfoW(mon, &mix) && EnumDisplaySettingsW(mix.szDevice, ENUM_CURRENT_SETTINGS, &dm);
        UINT dpi = 0;
        typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
        GetDpiForWindowFn getDpi = (GetDpiForWindowFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
        if (getDpi) dpi = getDpi(w);
        LONG style = GetWindowLongW(w, GWL_STYLE);
        kp::Log("display: %s - window %ldx%ld at %ld,%ld, client %ldx%ld, monitor %ldx%ld, display mode %lux%lu @%luHz, "
                "dpi %u, %s, %s",
                when, wr.right - wr.left, wr.bottom - wr.top, wr.left, wr.top, cr.right, cr.bottom,
                mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                haveMode ? dm.dmPelsWidth : 0, haveMode ? dm.dmPelsHeight : 0, haveMode ? dm.dmDisplayFrequency : 0,
                dpi, (style & WS_CAPTION) == WS_CAPTION ? "with frame" : "no frame",
                GetForegroundWindow() == w ? "in front" : "not in front");
    }

    // kenshi.cfg lives in the game folder: the working directory, or the parent of the exe's folder
    // when RE_Kenshi starts its copy in <Kenshi>\RE_Kenshi\.
    bool KenshiCfgFullScreen(bool* found)
    {
        wchar_t paths[2][MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, paths[0]);
        wcscat_s(paths[0], MAX_PATH, L"\\kenshi.cfg");
        GetModuleFileNameW(NULL, paths[1], MAX_PATH);
        for (int i = 0; i < 2; ++i)  // strip file name and folder
        {
            wchar_t* slash = wcsrchr(paths[1], L'\\');
            if (slash) *slash = 0;
        }
        wcscat_s(paths[1], MAX_PATH, L"\\kenshi.cfg");
        for (int p = 0; p < 2; ++p)
        {
            FILE* f = NULL;
            if (_wfopen_s(&f, paths[p], L"r") != 0 || !f) continue;
            char line[256];
            bool full = false;
            *found = false;
            while (fgets(line, sizeof(line), f))
                if (strncmp(line, "Full Screen=", 12) == 0) { *found = true; full = strncmp(line + 12, "Yes", 3) == 0; }
            fclose(f);
            if (*found) return full;
        }
        *found = false;
        return false;
    }

    bool FixDisabled()
    {
        wchar_t path[MAX_PATH];
        _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%sKenshiPure.cfg", kp::PluginDir());
        FILE* f = NULL;
        if (_wfopen_s(&f, path, L"r") != 0 || !f) return false;
        char line[256];
        bool off = false;
        while (fgets(line, sizeof(line), f))
            if (strncmp(line, "display_fix=", 12) == 0) off = atoi(line + 12) == 0;
        fclose(f);
        return off;
    }

    bool Responsive(HWND w)
    {
        DWORD_PTR r = 0;
        return SendMessageTimeoutW(w, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 200, &r) != 0;
    }

    unsigned __stdcall DisplayThread(void*)
    {
        // Wait (up to 2 min) until the game window is in front and its thread answers messages
        // 3 checks in a row, 0.5 s apart: the main menu is up and the game is not loading.
        HWND w = NULL;
        int good = 0;
        for (int i = 0; i < 240 && good < 3; ++i)
        {
            Sleep(500);
            w = GameWindow();
            RECT cr = { 0 };
            bool sized = w && GetClientRect(w, &cr) && cr.right > 0 && cr.bottom > 0;   // a real game window
            if (sized && GetForegroundWindow() == w && !IsIconic(w) && Responsive(w)) ++good;
            else good = 0;
        }
        if (!w) { kp::Log("display: game window not found - no minimize-restore"); return 0; }
        if (good < 3) { LogState(w, "the game window never came to the front - no minimize-restore"); return 0; }

        LogState(w, "before");
        RECT cr = { 0 };
        GetClientRect(w, &cr);
        // Target: the desktop mode saved in Windows (registry), not the current one - in exclusive full
        // screen the current mode is whatever smaller mode the game got.
        MONITORINFOEXW mi;
        mi.cbSize = sizeof(mi);
        DEVMODEW dm;
        ZeroMemory(&dm, sizeof(dm));
        dm.dmSize = sizeof(dm);
        if (GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi) &&
            EnumDisplaySettingsW(mi.szDevice, ENUM_REGISTRY_SETTINGS, &dm))
        {
            if ((DWORD)cr.right == dm.dmPelsWidth && (DWORD)cr.bottom == dm.dmPelsHeight)
            {
                kp::Log("display: client area %ldx%ld matches the desktop mode - nothing to do", cr.right, cr.bottom);
                return 0;
            }
            kp::Log("display: client area %ldx%ld, desktop mode %lux%lu - minimize-restore", cr.right, cr.bottom,
                    dm.dmPelsWidth, dm.dmPelsHeight);
        }
        ShowWindowAsync(w, SW_MINIMIZE);
        kp::Log("display: minimized, restoring in 0.7 s");
        Sleep(700);
        ShowWindowAsync(w, SW_RESTORE);
        Sleep(300);
        SetForegroundWindow(w);
        Sleep(1500);
        LogState(w, "after the minimize-restore");
        return 0;
    }
}

namespace kp
{
    bool DisplayCfgFullScreen(bool* found) { return KenshiCfgFullScreen(found); }
    bool DisplayFixOff() { return FixDisabled(); }

    void DisplayFixStart()
    {
        bool found = false;
        bool full = KenshiCfgFullScreen(&found);
        if (!found) { Log("display: no 'Full Screen=' in kenshi.cfg - display fix off"); return; }
        if (!full) { Log("display: kenshi.cfg says windowed - display fix off"); return; }
        if (FixDisabled()) { Log("display: display_fix=0 in KenshiPure.cfg - display fix off"); return; }
        uintptr_t h = _beginthreadex(NULL, 0, DisplayThread, NULL, 0, NULL);
        if (h) { CloseHandle((HANDLE)h); Log("display: full screen in kenshi.cfg - minimize-restore once the window is in front"); }
        else Log("display: could not start the display thread");
    }
}
