// M3 game adapter for networking: reads KenshiPure.cfg, loads KenshiPureCore.dll from the plugin
// folder, and runs NetFrame on the frame thread with the pose from Pose.cpp (no other game access).
// Any problem (no host line, no core DLL, wrong core version, bad config) leaves networking off and
// the rest of the plugin untouched. Built with the VC++ 2010 (v100) toolset.
#include "Net.h"
#include "NetFrame.h"
#include "Pose.h"
#include "Log.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

namespace
{
    class GameAccess : public kp::IGameAccess
    {
    public:
        bool LocalPose(kp::NetLocalSample* out)
        {
            kp::PoseSample s;
            if (!kp::PoseLatest(&s)) return false;
            for (int i = 0; i < 3; ++i) { out->pos[i] = s.pos[i]; out->vel[i] = s.motion[i]; out->dest[i] = s.dest[i]; }
            out->yawDeg = s.yawDeg;
            out->moving = s.moving;
            out->paused = s.paused;
            out->tick = s.tick;
            out->epoch = s.epoch;
            return true;
        }
        void Log(const char* line) { kp::Log("%s", line); }
    };

    GameAccess g_game;
    kp::NetFrame g_net;
    bool g_on = false;

    void CoreLog(void*, const char* line) { kp::Log("%s", line); }

    // Reads the whole cfg (small) into buf; false if there is none.
    bool ReadCfg(char* buf, size_t size)
    {
        wchar_t path[MAX_PATH];
        _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%sKenshiPure.cfg", kp::PluginDir());
        FILE* f = NULL;
        if (_wfopen_s(&f, path, L"rb") != 0 || !f) return false;
        size_t n = fread(buf, 1, size - 1, f);
        fclose(f);
        buf[n] = 0;
        return true;
    }
}

namespace kp
{
    void NetStart()
    {
        static char text[8192];
        KpCoreConfig cfg;
        if (!ReadCfg(text, sizeof(text))) text[0] = 0;
        if (!NetParseConfig(text, &cfg))
        {
            Log("net: off (no host= line in KenshiPure.cfg)");
            return;
        }
        wchar_t dll[MAX_PATH];
        _snwprintf_s(dll, MAX_PATH, _TRUNCATE, L"%sKenshiPureCore.dll", PluginDir());
        HMODULE core = LoadLibraryExW(dll, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        KpCoreGetApiFn get = core ? (KpCoreGetApiFn)GetProcAddress(core, "kpCoreGetApi") : NULL;
        if (!get)
        {
            Log("net: off (cannot load %ls or its kpCoreGetApi, error %lu)", dll, GetLastError());
            return;
        }
        const KpCoreApi* api = get(KP_CORE_VERSION);
        if (!api || api->version != KP_CORE_VERSION || api->size < sizeof(KpCoreApi))
        {
            Log("net: off (KenshiPureCore.dll does not speak core interface version %d)", KP_CORE_VERSION);
            return;
        }
        cfg.log = CoreLog;
        cfg.logCtx = NULL;
        Log("net: %s; server %s:%u, name %s", api->build ? api->build : "?", cfg.host, cfg.port, cfg.name);
        if (!g_net.Start(api, cfg, &g_game)) return;   // the core logged why
        g_on = true;
    }

    void NetOnFrame()
    {
        if (g_on) g_net.OnFrame(GetTickCount64());
    }

    void NetStop()
    {
        if (!g_on) return;
        g_on = false;
        g_net.Stop();
        Log("net: stopped");
    }
}
