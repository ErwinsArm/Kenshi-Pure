// M1: read the local character's pose each 100 ms on the frame thread and log it once a second.
// Local character = the player's selected character if it is one of theirs, else the first of the
// player's characters (docs/client.md "Own pose"). Every game read is inside __try and copies plain
// data only (as Probe.cpp does), so a bad pointer skips a sample instead of crashing Kenshi.
// Built with the VC++ 2010 (v100) toolset; keep to C++ that compiler accepts.
#include "Pose.h"
#include "PoseMath.h"
#include "Log.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>

#include "Game.h"

#define KP_POSE_SAMPLE_MS 100
#define KP_POSE_LOG_MS 1000
#define KP_POSE_LIMIT 1e7f      // larger coordinates are treated as garbage
#define KP_POSE_MAX_OWNED 256

namespace
{
    bool g_ready = false;
    kp::PoseSample g_latest;
    bool g_haveLatest = false;
    kp::RateLimiter g_sampleRate, g_logRate, g_badRate;
    // Last logged sample, for the measured x/z step per real second.
    float g_lastLoggedPos[3];
    unsigned long long g_lastLoggedTick = 0;
    bool g_haveLogged = false;
    // State edges that get their own log line.
    enum Where { W_UNKNOWN, W_NONE, W_CHAR };
    Where g_where = W_UNKNOWN;
    Character* g_lastChar = NULL;

    __declspec(noinline) void CopyName(Character* c, char* out, size_t outSize)
    {
        std::string n = c->getName();
        _snprintf_s(out, outSize, _TRUNCATE, "%s", n.c_str());
    }

    __declspec(noinline) void CopyPosition(Character* c, float* xyz)
    {
        Ogre::Vector3 v = c->getPosition();
        xyz[0] = v.x; xyz[1] = v.y; xyz[2] = v.z;
    }

    __declspec(noinline) void CopyOrientation(Character* c, float* wxyz)
    {
        Ogre::Quaternion q = c->getOrientation();
        wxyz[0] = q.w; wxyz[1] = q.x; wxyz[2] = q.y; wxyz[3] = q.z;
    }

    __declspec(noinline) void CopyVec(const Ogre::Vector3& v, float* xyz)
    {
        xyz[0] = v.x; xyz[1] = v.y; xyz[2] = v.z;
    }

    // Picks the local character: selected if it is among the player's characters, else the first.
    Character* PickLocal(PlayerInterface* pi, int* selected, unsigned int* owned)
    {
        *selected = 0;
        *owned = 0;
        lektor<Character*>& chars = KP_FIELD(pi, PlayerInterface, playerCharacters);
        if (!chars.stuff || chars.count == 0) return NULL;
        unsigned int n = chars.count;
        *owned = n;
        if (n > KP_POSE_MAX_OWNED) n = KP_POSE_MAX_OWNED;
        Character* sel = KP_FIELD(pi, PlayerInterface, selectedCharacter).getCharacter();
        if (sel)
            for (unsigned int i = 0; i < n; ++i)
                if (chars.stuff[i] == sel) { *selected = 1; return sel; }
        return chars.stuff[0];
    }

    // 1: sample taken; 0: no own character (menu, loading); -1: a read faulted.
    int Sample(GameWorld* gw, kp::PoseSample* s, Character** who)
    {
        __try
        {
            *who = NULL;
            PlayerInterface* pi = KP_FIELD(gw, GameWorld, player);
            if (!pi) return 0;
            Character* c = PickLocal(pi, &s->selected, &s->owned);
            if (!c) return 0;
            *who = c;
            s->paused = gw->isPaused() ? 1 : 0;
            CopyName(c, s->name, sizeof(s->name));
            CopyPosition(c, s->pos);
            CopyOrientation(c, s->quat);
            CharMovement* cm = c->getMovement();
            if (!cm) return -1;
            AbstractMovementBase* m = cm;
            CopyVec(KP_FIELD(m, AbstractMovementBase, direction), s->facing);
            CopyVec(KP_FIELD(m, AbstractMovementBase, currentMotion), s->motion);
            s->speed = KP_FIELD(m, AbstractMovementBase, currentSpeed);
            s->desiredSpeed = KP_FIELD(m, AbstractMovementBase, desiredSpeed);
            s->moving = KP_FIELD(m, AbstractMovementBase, currentlyMoving) ? 1 : 0;
            CopyVec(KP_FIELD(m, AbstractMovementBase, destination), s->dest);
            CopyVec(KP_FIELD(m, AbstractMovementBase, pathDestination), s->pathDest);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    bool Plausible(const kp::PoseSample& s)
    {
        return kp::AllFinite(s.pos, 3, KP_POSE_LIMIT) && kp::AllFinite(s.quat, 4, 2.0f) &&
               kp::AllFinite(s.facing, 3, KP_POSE_LIMIT) && kp::AllFinite(s.motion, 3, KP_POSE_LIMIT) &&
               kp::AllFinite(&s.speed, 1, KP_POSE_LIMIT) && kp::AllFinite(&s.desiredSpeed, 1, KP_POSE_LIMIT) &&
               kp::AllFinite(s.dest, 3, KP_POSE_LIMIT) && kp::AllFinite(s.pathDest, 3, KP_POSE_LIMIT);
    }

    void LogSample(const kp::PoseSample& s)
    {
        char step[32] = "-";
        if (g_haveLogged)
            _snprintf_s(step, sizeof(step), _TRUNCATE, "%.1f",
                        kp::StepPerSecond(g_lastLoggedPos, s.pos, s.tick - g_lastLoggedTick));
        kp::Log("pose: %s pos %.1f %.1f %.1f yaw %.1f facing %.2f %.2f %.2f motion %.2f %.2f %.2f speed %.2f "
                "want %.2f moving %d walking %d dest %.1f %.1f %.1f path %.1f %.1f %.1f step/s %s%s",
                s.name, s.pos[0], s.pos[1], s.pos[2], s.yawDeg, s.facing[0], s.facing[1], s.facing[2],
                s.motion[0], s.motion[1], s.motion[2], s.speed, s.desiredSpeed, s.moving, s.walking,
                s.dest[0], s.dest[1], s.dest[2], s.pathDest[0], s.pathDest[1], s.pathDest[2], step,
                s.paused ? " paused" : "");
        memcpy(g_lastLoggedPos, s.pos, sizeof(g_lastLoggedPos));
        g_lastLoggedTick = s.tick;
        g_haveLogged = true;
    }
}

namespace kp
{
    bool PoseInit()
    {
        static const int need[] = {
            KPS_PlayerInterface__selectedCharacter, KPS_hand__getCharacter, KPS_Character__getMovement,
            KPS_vslot_Character__getName, KPS_vslot_Character__getPosition, KPS_vslot_Character__getOrientation,
        };
        for (size_t i = 0; i < sizeof(need) / sizeof(need[0]); ++i)
            if (!SiteOk(need[i]))
            {
                Log("pose: off (row %s is unresolved for this build)", g_sites[need[i]].name);
                return false;
            }
        if (!FeatureOn("optional:pose"))
        {
            Log("pose: off (its addresses are not all resolved for this build)");
            return false;
        }
        g_ready = true;
        Log("pose: on (sample every %d ms, log every %d ms)", KP_POSE_SAMPLE_MS, KP_POSE_LOG_MS);
        return true;
    }

    void PoseOnFrame(GameWorld* gw)
    {
        if (!g_ready) return;
        unsigned long long now = GetTickCount64();
        if (!g_sampleRate.Due(now, KP_POSE_SAMPLE_MS)) return;

        PoseSample s;
        memset(&s, 0, sizeof(s));
        Character* who = NULL;
        int r = Sample(gw, &s, &who);
        if (r == 0)
        {
            g_haveLatest = false;
            if (g_where != W_NONE) Log("pose: no own character (menu or loading)");
            g_where = W_NONE;
            g_lastChar = NULL;
            g_haveLogged = false;
            return;
        }
        if (r < 0 || !Plausible(s))
        {
            g_haveLatest = false;
            if (g_badRate.Due(now, 10000))
                Log("pose: %s - sample skipped", r < 0 ? "a game read faulted" : "implausible values");
            return;
        }
        s.tick = now;
        s.yawDeg = YawDegFromQuat(s.quat[0], s.quat[1], s.quat[2], s.quat[3]);
        s.walking = DistXZ2(s.pos, s.pathDest) > 4.0f ? 1 : 0;
        if (g_where != W_CHAR || who != g_lastChar)
        {
            Log("pose: local character %s (%s, %u owned)", s.name, s.selected ? "selected" : "first owned", s.owned);
            g_haveLogged = false;
        }
        g_where = W_CHAR;
        g_lastChar = who;
        g_latest = s;
        g_haveLatest = true;
        if (g_logRate.Due(now, KP_POSE_LOG_MS)) LogSample(s);
    }

    bool PoseLatest(PoseSample* out)
    {
        if (!g_haveLatest) return false;
        *out = g_latest;
        return true;
    }
}
