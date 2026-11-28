// Loads the optional KenshiPureCheats.dll (see Cheat.h, CheatApi.h).
// Built with the VC++ 2010 (v100) toolset; keep to C++ that compiler accepts.
#include "Cheat.h"
#include "CheatApi.h"
#include "Log.h"

#include <windows.h>
#include <stdio.h>

namespace
{
    KpCheatFrameFn g_frame = NULL;
    KpCheatStopFn g_stop = NULL;
}

namespace kp
{
    void CheatInit(const KpHost* host)
    {
        wchar_t path[MAX_PATH];
        _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%sKenshiPureCheats.dll", PluginDir());
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        {
            Log("cheat: not installed (no KenshiPureCheats.dll)");
            return;
        }
        HMODULE m = LoadLibraryW(path);
        if (!m)
        {
            Log("cheat: KenshiPureCheats.dll did not load (error %lu)", GetLastError());
            return;
        }
        KpCheatStartFn start = (KpCheatStartFn)GetProcAddress(m, "kpCheatStart");
        KpCheatFrameFn frame = (KpCheatFrameFn)GetProcAddress(m, "kpCheatFrame");
        if (!start || !frame)
        {
            Log("cheat: KenshiPureCheats.dll lacks kpCheatStart/kpCheatFrame - not used");
            return;
        }
        KpCheatHost h;
        h.version = KP_CHEAT_API_VERSION;
        h.size = sizeof(h);
        h.exePath = host->exePath;
        h.exeSha256 = host->exeSha256 && host->exeSha256[0] ? host->exeSha256 : NULL;
        if (!start(&h))
        {
            Log("cheat: KenshiPureCheats.dll stayed off (see KenshiPureCheats.log)");
            return;
        }
        g_frame = frame;   // the DLL stays loaded for the whole game
        g_stop = (KpCheatStopFn)GetProcAddress(m, "kpCheatStop");
        Log("cheat: KenshiPureCheats.dll started (window and log: KenshiPureCheats.log)");
    }

    void CheatOnFrame(GameWorld* gw, bool connected)
    {
        if (!g_frame) return;
        if (connected)
        {
            // Multiplayer: no cheats for the rest of this run (also after a disconnect).
            if (g_stop) g_stop();
            g_frame = NULL;
            Log("cheat: off for the rest of this game run (connected to a multiplayer server)%s",
                g_stop ? "" : "; this KenshiPureCheats.dll has no kpCheatStop, its window stays but does nothing");
            return;
        }
        g_frame(gw);
    }
}
