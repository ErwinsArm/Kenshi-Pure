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
        Log("cheat: KenshiPureCheats.dll started (window and log: KenshiPureCheats.log)");
    }

    void CheatOnFrame(GameWorld* gw)
    {
        if (g_frame) g_frame(gw);
    }
}
