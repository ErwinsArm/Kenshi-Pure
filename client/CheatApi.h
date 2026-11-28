// Plain C interface between KenshiPure.dll and the optional KenshiPureCheats.dll (docs/client.md
// "Cheat window"). KenshiPure loads the cheats DLL only if the file is next to it.
// Built with the VC++ 2010 (v100) toolset; keep to C++ that compiler accepts.
#pragma once

class GameWorld;

#define KP_CHEAT_API_VERSION 1

extern "C"
{
    struct KpCheatHost
    {
        unsigned int version;     // KP_CHEAT_API_VERSION
        unsigned int size;        // sizeof(KpCheatHost)
        const wchar_t* exePath;   // the running kenshi_x64.exe
        const char* exeSha256;    // its SHA-256 as hex (from the loader), or NULL
    };

    // Exported by KenshiPureCheats.dll. kpCheatStart returns 1 when the cheats are on.
    typedef int (*KpCheatStartFn)(const KpCheatHost* host);
    // Called on the game thread after each game frame.
    typedef void (*KpCheatFrameFn)(GameWorld* gw);
    // Switches the cheats off for the rest of the game run (multiplayer); game thread. Optional for the host.
    typedef void (*KpCheatStopFn)();
}
