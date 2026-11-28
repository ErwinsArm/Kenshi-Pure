// G10 (M6): x1 and no player pause while connected (SpeedLock.h).
//
// The game (1.0.65 exe, M; the same callers measured in 1.0.68): the speed is GameWorld+0x700
// (getFrameSpeedMultiplier), paused is +0x8B9 (isPaused). setGameSpeed(s, click) writes the speed, then
// togglePause(s == 0); togglePause(on) sets paused = on || speed == 0. userPause(p) sets the speed to 0 (p) or back
// to the speed saved before the pause, with the same tail. Every direct caller (whole .text):
//   userPause(true):   GameWorld::processKeys (pause key toggles), DialogueWindow::show(Dialogue*),
//                      CharacterEditWindow::initCharacters (a new game's editor), the level editor
//   setGameSpeed:      processKeys (speed keys), MainBarGUI::changeSpeed (buttons, tail jump), the editor (1.0)
//   togglePause(true): the build mode (unnamed function, row BuildMode::pauseOnOpen, called from
//                      MainBarGUI::buildButton and processKeys), SaveManager::loadGame, ZoneManager::processLoading,
//                      ZoneManager::activateAllActiveZones (tail jump), two functions reached only indirectly (one
//                      near LoadingWindow), and the tails of setGameSpeed / userPause
//
// Lock (from the first welcome by a server until the game exits, also through a drop and a reconnect): userPause(true) passes only from the character editor, every other is dropped;
// after userPause(false) the speed is set back to x1; setGameSpeed(s) with s != 1 runs as setGameSpeed(1);
// togglePause(true) from the build mode is dropped (the loading pauses stay: they keep the world from running
// half loaded). Each frame: a speed other than 1 (from before the connection, or a path not seen statically) is
// set to x1, except while the editor's pause holds (speed 0) or the game paused itself (paused with a speed above 0:
// setGameSpeed would end that pause through togglePause). Changes of speed and pause are logged.
// Built with the VC++ 2010 (v100) toolset; keep to C++ that compiler accepts.
#include <windows.h>
#include <intrin.h>

#include "SpeedLock.h"
#include "Game.h"
#include "Hooks.h"
#include "Log.h"

#pragma intrinsic(_ReturnAddress)

namespace
{
    typedef void (*UserPauseFn)(GameWorld* gw, bool p);
    typedef void (*SetGameSpeedFn)(GameWorld* gw, float speed, bool click);
    typedef void (*TogglePauseFn)(GameWorld* gw, bool on);

    UserPauseFn g_userPauseOrig = NULL;
    SetGameSpeedFn g_setSpeedOrig = NULL;
    TogglePauseFn g_toggleOrig = NULL;
    bool g_ready = false;
    volatile LONG g_locked = 0;      // set on the frame thread; the detours run on the game thread too
    bool g_passedPause = false;      // the character editor's userPause(true), until userPause(false)
    unsigned int g_blockedPauses = 0, g_forcedSpeeds = 0;

    // Frame-side state for the change log.
    bool g_wasLocked = false;
    bool g_haveLast = false;
    float g_lastSpeed = 1.0f;
    bool g_lastPaused = false;
    ULONGLONG g_nextFrameLog = 0;    // frame lines at most every 500 ms (a fight with another writer must not flood the log)
    unsigned int g_quietLines = 0;

    bool FrameLogAllowed()
    {
        ULONGLONG now = GetTickCount64();
        if (now < g_nextFrameLog) { ++g_quietLines; return false; }
        g_nextFrameLog = now + 500;
        if (g_quietLines) { kp::Log("speed: (%u lines left out)", g_quietLines); g_quietLines = 0; }
        return true;
    }

    enum Caller { C_OTHER, C_KEYS, C_DIALOGUE, C_EDITOR, C_BUILD };
    const char* const kCallerNames[] = { "another caller", "pause key", "dialogue", "character editor", "build mode" };

    // Which game function the return address lies in (the exe's unwind table names the function start).
    Caller CallerOf(void* ret)
    {
        DWORD64 base = 0;
        PRUNTIME_FUNCTION f = RtlLookupFunctionEntry((DWORD64)ret, &base, NULL);
        if (!f) return C_OTHER;
        void* start = (void*)(base + f->BeginAddress);
        if (start == kp::SiteAddress(KPS_GameWorld__processKeys)) return C_KEYS;
        if (start == kp::SiteAddress(KPS_DialogueWindow__show_Dialogue_)) return C_DIALOGUE;
        if (start == kp::SiteAddress(KPS_CharacterEditWindow__initCharacters)) return C_EDITOR;
        if (start == kp::SiteAddress(KPS_BuildMode__pauseOnOpen)) return C_BUILD;
        return C_OTHER;
    }

    bool ReadSpeed(GameWorld* gw, float* speed, bool* paused)
    {
        __try
        {
            *speed = gw->getFrameSpeedMultiplier();
            *paused = gw->isPaused();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SetSpeedOne(GameWorld* gw)
    {
        __try
        {
            g_setSpeedOrig(gw, 1.0f, false);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void UserPauseDetour(GameWorld* gw, bool p)
    {
        if (g_locked && p)
        {
            Caller c = CallerOf(_ReturnAddress());
            if (c != C_EDITOR)
            {
                ++g_blockedPauses;
                kp::Log("speed: pause blocked (%s; connected: always x1), %u so far", kCallerNames[c], g_blockedPauses);
                return;
            }
            g_passedPause = true;
            kp::Log("speed: the character editor's pause passed");
        }
        g_userPauseOrig(gw, p);
        if (!p)
        {
            g_passedPause = false;
            float s = 1.0f;
            bool paused = false;
            if (g_locked && ReadSpeed(gw, &s, &paused) && s != 1.0f)
            {
                ++g_forcedSpeeds;
                g_setSpeedOrig(gw, 1.0f, false);
            }
        }
    }

    void SetGameSpeedDetour(GameWorld* gw, float speed, bool click)
    {
        if (g_locked && speed != 1.0f)
        {
            ++g_forcedSpeeds;
            kp::Log("speed: x%.0f asked - kept at x1 (connected), %u so far", speed, g_forcedSpeeds);
            speed = 1.0f;
        }
        g_setSpeedOrig(gw, speed, click);
    }

    void TogglePauseDetour(GameWorld* gw, bool on)
    {
        if (g_locked && on && CallerOf(_ReturnAddress()) == C_BUILD)
        {
            ++g_blockedPauses;
            kp::Log("speed: pause blocked (build mode; connected: always x1), %u so far", g_blockedPauses);
            return;
        }
        g_toggleOrig(gw, on);
    }
}

namespace kp
{
    void SpeedLockInit()
    {
        if (!FeatureOn("optional:speedlock-write"))
        {
            Log("speed: lock off (its addresses are not all resolved for this build) - pause and speed stay free");
            return;
        }
        if (!HookSite(KPS_GameWorld__userPause, (void*)&UserPauseDetour, reinterpret_cast<void**>(&g_userPauseOrig)) ||
            !HookSite(KPS_GameWorld__setGameSpeed, (void*)&SetGameSpeedDetour, reinterpret_cast<void**>(&g_setSpeedOrig)) ||
            !HookSite(KPS_GameWorld__togglePause, (void*)&TogglePauseDetour, reinterpret_cast<void**>(&g_toggleOrig)))
        {
            Log("speed: lock off (a detour could not be installed) - pause and speed stay free");
            return;
        }
        g_ready = true;
        Log("speed: lock ready (from the first connection on: always x1; only the character editor and loading may pause)");
    }

    void SpeedLockOnFrame(GameWorld* gw, bool connected)
    {
        if (!g_ready) return;
        // Latched like the cheats: a dropped connection must not open a window for pause or speed.
        if (connected && !g_wasLocked)
        {
            g_wasLocked = true;
            InterlockedExchange(&g_locked, 1);
            Log("speed: locked to x1 until the game exits (connected to a multiplayer server)");
        }
        bool lock = g_wasLocked;
        float s = 1.0f;
        bool paused = false;
        if (!ReadSpeed(gw, &s, &paused)) return;
        if (!g_haveLast || s != g_lastSpeed || paused != g_lastPaused)
        {
            if (g_haveLast && FrameLogAllowed()) Log("speed: now x%.2f, %s", s, paused ? "paused" : "running");
            g_haveLast = true;
            g_lastSpeed = s;
            g_lastPaused = paused;
        }
        if (!lock || s == 1.0f) return;
        if (s == 0.0f && g_passedPause) return;   // the character editor's pause holds
        if (paused && s > 0.0f) return;            // the game paused itself (togglePause); do not undo it
        if (SetSpeedOne(gw))
        {
            ++g_forcedSpeeds;
            if (FrameLogAllowed()) Log("speed: was x%.2f%s - set to x1 (connected), %u so far", s, s == 0.0f ? " (paused)" : "", g_forcedSpeeds);
        }
    }
}
