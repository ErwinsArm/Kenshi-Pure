// See StandIn.h and docs/client.md "Stand-in body (M4)". Built with the VC++ 2010 (v100) toolset;
// keep to C++ that compiler accepts. Every game call runs on the frame thread inside __try and copies
// plain data only, so a bad pointer gives a failed call instead of a crash in Kenshi.
#include "StandIn.h"
#include "NetFrame.h"   // StandInInfo
#include "Hooks.h"
#include "Log.h"

#include <windows.h>
#include <math.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include <string>

#include "Game.h"

#define KP_SI_MAX_CHARS 1024
#define KP_SI_MAX_OWN 256

namespace
{
    // The AI skip: the detours compare one pointer-sized value (written only on the frame thread).
    // AK's own skip list is locked and double-buffered and it logs AI ticks that overrun, so the AI
    // tick may run on another thread (remote-bodies.md G4, inferred): read it once, never more.
    // Entry 0: the stand-in's AI* (standin_ai=ai); entries 1..KP_SKIP_REMOTE: the M5 remote bodies (AiSkipSet).
    void* volatile g_skipAi[1 + KP_SKIP_REMOTE];
    bool g_aiHooked = false;
    void* volatile g_skipChar = NULL;   // Character* of the stand-in (standin_ai=character)
    int g_aiMode = kp::DA_NONE;
    bool g_skipOn = false;
    volatile LONG g_skipped = 0;        // skipped ticks (any thread)
    volatile DWORD g_skipThread = 0;    // thread of the last skipped tick (is the AI tick on the frame thread?)

    void Skipped()
    {
        InterlockedIncrement(&g_skipped);
        g_skipThread = GetCurrentThreadId();
    }

    void (*g_aiOrig)(AI*, float) = NULL;
    void AiDetour(AI* self, float t)
    {
        if (self != NULL)
            for (int i = 0; i < 1 + KP_SKIP_REMOTE; ++i)
                if ((void*)self == g_skipAi[i]) { Skipped(); return; }
        g_aiOrig(self, t);
    }
    void (*g_charOrig)(Character*) = NULL;
    void CharDetour(Character* self)
    {
        if (self != NULL && (void*)self == g_skipChar) { Skipped(); return; }
        g_charOrig(self);
    }

    // The stand-in's hand, built in place by hand::hand(RootObjectBase*). KenshiLib declares no
    // destructor for hand (a few ints and a vtable pointer), so dropping it is just forgetting it.
    __declspec(align(16)) char g_handBuf[sizeof(hand)];
    bool g_haveHand = false;
    float g_home[3];
    float g_homeRot[4];   // w, x, y, z

    hand* Hand() { return reinterpret_cast<hand*>(g_handBuf); }

    __declspec(noinline) void CopyName(Character* c, char* out, size_t outSize)
    {
        std::string n = c->getName();
        _snprintf_s(out, outSize, _TRUNCATE, "%s", n.c_str());
    }
    __declspec(noinline) void CopyPos(Character* c, float* xyz)
    {
        Ogre::Vector3 v = c->getPosition();
        xyz[0] = v.x; xyz[1] = v.y; xyz[2] = v.z;
    }
    __declspec(noinline) void CopyRot(Character* c, float* wxyz)
    {
        Ogre::Quaternion q = c->getOrientation();
        wxyz[0] = q.w; wxyz[1] = q.x; wxyz[2] = q.y; wxyz[3] = q.z;
    }

    float DistXZ(const float* a, const float* b)
    {
        float dx = a[0] - b[0], dz = a[2] - b[2];
        return sqrtf(dx * dx + dz * dz);
    }

    // POD copies of the update list and the player's own characters.
    bool CopyLists(GameWorld* gw, Character** all, unsigned int* nAll, Character** own, unsigned int* nOwn)
    {
        __try
        {
            *nAll = 0;
            *nOwn = 0;
            const ogre_unordered_set<Character*>::type& set = gw->getCharacterUpdateList();
            for (ogre_unordered_set<Character*>::type::const_iterator it = set.begin(); it != set.end(); ++it)
                if (*nAll < KP_SI_MAX_CHARS) all[(*nAll)++] = *it;
            PlayerInterface* pi = KP_FIELD(gw, GameWorld, player);
            if (pi)
            {
                lektor<Character*>& chars = KP_FIELD(pi, PlayerInterface, playerCharacters);
                unsigned int n = chars.stuff ? chars.count : 0;
                if (n > KP_SI_MAX_OWN) n = KP_SI_MAX_OWN;
                for (unsigned int i = 0; i < n; ++i) own[i] = chars.stuff[i];
                *nOwn = n;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // 1: usable candidate (pos filled); 0: not a candidate; -1: faulted.
    int Candidate(Character* c, float* pos, char* name, size_t nameSize)
    {
        __try
        {
            if (!c || c->isPlayerCharacter() || c->isDead()) return 0;
            MedicalSystem* m = c->getMedical();
            if (m && m->isUnconcious()) return 0;
            if (!c->getMovement() || !KP_FIELD(c, Character, ai)) return 0;
            CopyPos(c, pos);
            CopyName(c, name, nameSize);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    __declspec(noinline) void BuildHand(Character* c)
    {
        new (g_handBuf) hand(static_cast<RootObjectBase*>(c));
    }

    bool MakeHand(Character* c, float* home, float* rot)
    {
        __try
        {
            BuildHand(c);
            CopyPos(c, home);
            CopyRot(c, rot);
            return Hand()->getCharacter() == c;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // The stand-in now, or NULL when its hand no longer resolves (or faults).
    Character* Resolve()
    {
        if (!g_haveHand) return NULL;
        __try
        {
            return Hand()->getCharacter();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return NULL;
        }
    }

    void SetSkipPointers(Character* c)
    {
        void* ai = NULL;
        if (c && g_skipOn && g_aiMode == kp::DA_AI)
        {
            __try { ai = KP_FIELD(c, Character, ai); }
            __except (EXCEPTION_EXECUTE_HANDLER) { ai = NULL; }
        }
        g_skipAi[0] = ai;
        g_skipChar = (c && g_skipOn && g_aiMode == kp::DA_CHARACTER) ? (void*)c : NULL;
    }

    char g_name[64];   // the stand-in's name at pick: a hand that resolves to someone else is refused

    bool ReadBody(Character* c, kp::BodyState* out)
    {
        __try
        {
            // After a load an old hand might resolve to another object (critic, inferred): never drive
            // a player's character, nor anyone whose name differs from the one picked.
            if (c->isPlayerCharacter()) return false;
            char name[64];
            CopyName(c, name, sizeof(name));
            if (strcmp(name, g_name) != 0) return false;
            CopyPos(c, out->pos);
            CharMovement* cm = c->getMovement();
            if (!cm) return false;
            AbstractMovementBase* m = cm;
            out->speed = KP_FIELD(m, AbstractMovementBase, currentSpeed);
            out->moving = KP_FIELD(m, AbstractMovementBase, currentlyMoving) ? 1 : 0;
            out->desired = KP_FIELD(m, AbstractMovementBase, desiredSpeed);
            out->maxSpeed = KP_FIELD(m, AbstractMovementBase, maxSpeed);
            out->walkSpeed = KP_FIELD(m, AbstractMovementBase, walkSpeed);
            out->speedOrders = (int)KP_FIELD(m, AbstractMovementBase, speedOrders);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Quaternion turning +z to the heading yaw (degrees, 0 = +z, 90 = +x): a rotation about +y.
    Ogre::Quaternion YawQuat(float yawDeg)
    {
        float h = yawDeg * 3.14159265f / 360.0f;
        return Ogre::Quaternion(cosf(h), 0.0f, sinf(h), 0.0f);
    }

    __declspec(noinline) void DoTeleportRot(CharMovement* cm, const float* p, const Ogre::Quaternion& q)
    {
        Ogre::Vector3 v(p[0], p[1], p[2]);
        cm->_NV__setPositionDirectionAndTeleport(v, q);
    }
    // AK's place (remote-bodies.md G5): position without a teleport, then the visual follows.
    __declspec(noinline) void DoPlaceSimple(Character* c, CharMovement* cm, const float* p, const Ogre::Quaternion& q)
    {
        Ogre::Vector3 v(p[0], p[1], p[2]);
        cm->_NV__setPositionSimple(v);
        c->teleportVisuallyOnly(v, q);
    }
    // KenshiCoop makes both calls; M4 tries each half alone so the log says which one moves the body.
    // Character::setDestination clears the NPC's job list and adds a TASK_MOVE job that the AI ticks
    // (critic's reading of the 1.0.65 exe at 0x5c8b20; measured there, not by a run).
    __declspec(noinline) void DoWalk(Character* c, CharMovement* cm, const float* p, float speed, bool order)
    {
        Ogre::Vector3 v(p[0], p[1], p[2]);
        if (order) c->setDestination(v, false);
        else cm->_NV_setDestination(v, HIGH_PRIORITY, false);
        // RUN first: the move task puts desired back from the speed orders (restoreDesiredSpeed inlined in
        // CharBody::_move, 1.0.65 0x5c5ecc, M), and an NPC's orders are usually WALK (its walk speed).
        cm->setDesiredSpeedOrders(RUN);
        cm->setDesiredSpeed(speed);
    }
    __declspec(noinline) void DoSteer(CharMovement* cm, const float* vel, float speed, float limit)
    {
        Ogre::Vector3 v(vel[0], vel[1], vel[2]);
        cm->setDesiredSpeedOrders(RUN);
        cm->setDesiredSpeed(speed);
        cm->setDirectMovement(v, limit);
    }

    bool Apply(Character* c, const kp::DriveCmd& cmd, int mode)
    {
        __try
        {
            CharMovement* cm = c->getMovement();
            if (!cm) return false;
            switch (cmd.kind)
            {
            case kp::DC_PLACE:
            case kp::DC_SNAP:
                if (mode == kp::DM_STEER) DoPlaceSimple(c, cm, cmd.pos, YawQuat(cmd.yaw));
                else DoTeleportRot(cm, cmd.pos, YawQuat(cmd.yaw));
                break;
            case kp::DC_WALK: DoWalk(c, cm, cmd.pos, cmd.speed, mode == kp::DM_ORDER); break;
            case kp::DC_STEER: DoSteer(cm, cmd.vel, cmd.speed, cmd.limit); break;
            case kp::DC_HALT: cm->_NV_halt(); break;
            default: break;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool PutBack(Character* c)
    {
        __try
        {
            CharMovement* cm = c->getMovement();
            if (!cm) return false;
            cm->_NV_halt();
            DoTeleportRot(cm, g_home, Ogre::Quaternion(g_homeRot[0], g_homeRot[1], g_homeRot[2], g_homeRot[3]));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

namespace kp
{
    bool AiSkipInstall()
    {
        if (g_aiHooked) return true;
        if (!SiteOk(KPS_AI__periodicUpdate)) return false;
        g_aiHooked = HookSite(KPS_AI__periodicUpdate, (void*)&AiDetour, reinterpret_cast<void**>(&g_aiOrig));
        return g_aiHooked;
    }

    void AiSkipSet(int index, void* ai)
    {
        if (index >= 1 && index <= KP_SKIP_REMOTE) g_skipAi[index] = ai;
    }

    long AiSkippedTicks() { return g_skipped; }

    bool DriveApplyTo(Character* c, const DriveCmd& cmd, int mode) { return c && Apply(c, cmd, mode); }

    bool StandInInit(DriveConfig* cfg)
    {
        if (cfg->mode == DM_OFF)
        {
            Log("standin: off (no standin= line in KenshiPure.cfg)");
            return false;
        }
        static const int need[] = {
            KPS_GameWorld__getCharacterUpdateList, KPS_Character__isPlayerCharacter, KPS_Character__isDead,
            KPS_Character__getMedical, KPS_MedicalSystem__isUnconcious, KPS_Character__getMovement,
            KPS_hand__hand_RootObjectBase__, KPS_hand__getCharacter, KPS_Character__ai,
            KPS_vslot_Character__getName, KPS_vslot_Character__getPosition, KPS_vslot_Character__getOrientation,
            KPS_AbstractMovementBase__currentSpeed, KPS_AbstractMovementBase__currentlyMoving,
            KPS_Character__setDestination, KPS_CharMovement__setDestination, KPS_AbstractMovementBase__setDesiredSpeed,
            KPS_CharMovement__setDirectMovement, KPS_CharMovement__setPositionDirectionAndTeleport,
            KPS_CharMovement__setPositionSimple, KPS_Character__teleportVisuallyOnly, KPS_CharMovement__halt,
            KPS_AI__periodicUpdate, KPS_Character__periodicUpdate, KPS_AbstractMovementBase__setDesiredSpeedOrders,
            KPS_AbstractMovementBase__desiredSpeed, KPS_AbstractMovementBase__maxSpeed, KPS_AbstractMovementBase__walkSpeed,
            KPS_AbstractMovementBase__speedOrders,
        };
        for (size_t i = 0; i < sizeof(need) / sizeof(need[0]); ++i)
            if (!SiteOk(need[i]))
            {
                Log("standin: off (row %s is unresolved for this build)", g_sites[need[i]].name);
                cfg->mode = DM_OFF;
                return false;
            }
        g_aiMode = cfg->aiSkip;
        bool hooked = true;
        if (g_aiMode == DA_AI)
            hooked = AiSkipInstall();
        else if (g_aiMode == DA_CHARACTER)
            hooked = HookSite(KPS_Character__periodicUpdate, (void*)&CharDetour, reinterpret_cast<void**>(&g_charOrig));
        if (!hooked)
        {
            Log("standin: off (the AI-skip detour could not be installed)");
            cfg->mode = DM_OFF;
            return false;
        }
        Log("standin: on (mode %s, AI skip %s, snap %.0f units, range %.0f units%s%s)", DriveModeName(cfg->mode),
            g_aiMode == DA_AI ? "AI::periodicUpdate" : (g_aiMode == DA_CHARACTER ? "Character::periodicUpdate" : "none"),
            cfg->snapDist, cfg->pickDist, cfg->name[0] ? ", name " : "", cfg->name);
        return true;
    }

    bool StandInPickNpc(const float* nearPos, const DriveConfig& cfg, StandInInfo* out)
    {
        GameWorld* gw = GameWorldGlobal();
        static Character* all[KP_SI_MAX_CHARS];
        Character* own[KP_SI_MAX_OWN];
        unsigned int nAll = 0, nOwn = 0;
        if (!gw || !CopyLists(gw, all, &nAll, own, &nOwn)) return false;
        Character* best = NULL;
        float bestD = cfg.pickDist;
        char bestName[64] = "";
        for (unsigned int i = 0; i < nAll; ++i)
        {
            bool mine = false;
            for (unsigned int k = 0; k < nOwn; ++k)
                if (own[k] == all[i]) { mine = true; break; }
            if (mine) continue;   // never one of the player's own characters (squad members included)
            float pos[3];
            char name[64];
            if (Candidate(all[i], pos, name, sizeof(name)) != 1) continue;
            if (cfg.name[0] && _stricmp(name, cfg.name) != 0) continue;
            float d = DistXZ(pos, nearPos);
            if (d <= bestD)
            {
                bestD = d;
                best = all[i];
                strcpy_s(bestName, sizeof(bestName), name);
            }
        }
        if (!best) return false;
        float home[3], rot[4];
        if (!MakeHand(best, home, rot))
        {
            Log("standin: %s found, but its hand did not resolve back to it - not driving it", bestName);
            return false;
        }
        g_haveHand = true;
        strcpy_s(g_name, sizeof(g_name), bestName);
        memcpy(g_home, home, sizeof(g_home));
        memcpy(g_homeRot, rot, sizeof(g_homeRot));
        strcpy_s(out->name, sizeof(out->name), bestName);
        memcpy(out->home, home, sizeof(out->home));
        out->distance = bestD;
        SetSkipPointers(best);
        return true;
    }

    bool StandInReadBody(BodyState* out)
    {
        Character* c = Resolve();
        if (!c || !ReadBody(c, out))
        {
            g_skipAi[0] = NULL;
            g_skipChar = NULL;
            return false;
        }
        SetSkipPointers(c);   // refreshed each frame from the re-resolved body
        return true;
    }

    bool StandInApplyCmd(const DriveCmd& cmd, int mode)
    {
        Character* c = Resolve();
        return c && Apply(c, cmd, mode);
    }

    void StandInSetSkip(bool on)
    {
        g_skipOn = on;
        SetSkipPointers(on ? Resolve() : NULL);
    }

    void StandInDrop(bool worldGone)
    {
        g_skipOn = false;
        g_skipAi[0] = NULL;
        g_skipChar = NULL;
        if (!worldGone)
        {
            Character* c = Resolve();
            if (c && !PutBack(c)) Log("standin: putting it back faulted");
        }
        g_haveHand = false;
        DWORD t = g_skipThread;
        Log("standin: %ld AI ticks skipped so far, the last on thread %lu (%s)", g_skipped, t,
            t == 0 ? "none yet" : (t == GetCurrentThreadId() ? "the frame thread" : "NOT the frame thread"));
    }
}
