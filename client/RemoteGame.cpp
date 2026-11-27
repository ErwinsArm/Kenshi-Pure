// M5 game adapter for remote bodies (Remote.h has the logic): spawns a body with the G3 recipe
// (docs/findings/remote-bodies.md), keeps one hand per slot (never a pointer across frames), keeps its max
// speed, carries out drive commands (StandIn.cpp's calls), destroys it, and removes every body right
// before the game writes a save (SaveManager::saveGame detour). Frame thread only (the save detour runs
// on the game's main thread too); every game call inside __try. Built with the VC++ 2010 (v100) toolset.
#include "RemoteGame.h"
#include "NetFrame.h"
#include "StandIn.h"
#include "Hooks.h"
#include "Log.h"

#include <windows.h>
#include <math.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include <string>

#include "Game.h"
#include <kenshi/Platoon.h>
#include <kenshi/Animation/AnimationClass.h>

namespace
{
    // The name every body gets: a fixed prefix nobody's NPC carries, so a hand that resolves to anything
    // else after a load is refused, and a save that kept a body can be told from the game's own people.
    const char kNamePrefix[] = "[KP] ";

    struct Slot
    {
        bool used;
        bool destroyed;                       // destroy was asked (by us or by the save guard)
        __declspec(align(16)) char hand[sizeof(hand)];
        char name[64];                        // the body's name as set at spawn
        char faction[64];                     // the faction's name as read back at spawn
    };
    Slot g_slots[KP_SKIP_REMOTE];
    bool g_saveGuard = false;
    unsigned int g_savesGuarded = 0;

    hand* HandOf(Slot& s) { return reinterpret_cast<hand*>(s.hand); }
    bool SlotOk(int slot) { return slot >= 0 && slot < KP_SKIP_REMOTE && g_slots[slot].used; }

    __declspec(noinline) void CopyName(Character* c, char* out, size_t outSize)
    {
        std::string n = c->getName();
        _snprintf_s(out, outSize, _TRUNCATE, "%s", n.c_str());
    }
    __declspec(noinline) void CopyFactionName(Faction* f, char* out, size_t outSize)
    {
        const std::string& n = f->getName();
        _snprintf_s(out, outSize, _TRUNCATE, "%s", n.c_str());
    }
    __declspec(noinline) void SetBodyName(Character* c, const char* name)
    {
        std::string s(name);
        c->setName(s);
    }
    __declspec(noinline) Faction* FactionById(FactionManager* fm, const char* id)
    {
        std::string s(id);
        return fm->getFactionByStringID(s);
    }
    __declspec(noinline) Platoon* NewSquad(Faction* f, bool permanent, const float* p)
    {
        Ogre::Vector3 v(p[0], p[1], p[2]);
        return f->createNewEmptyActivePlatoon(NULL, permanent, v);
    }
    __declspec(noinline) RootObject* NewBody(RootObjectFactory* fac, Faction* f, const float* p, ActivePlatoon* owner)
    {
        Ogre::Vector3 v(p[0], p[1], p[2]);
        // Template NULL: the factory takes "random character base-copy" (1.0.65 0x582cb3, M); home NULL; age 1
        // (AK and WoK pass 1.0; remote-bodies.md G3).
        return fac->createRandomCharacter(f, v, owner, NULL, NULL, 1.0f);
    }
    __declspec(noinline) void Teleport(Character* c, const float* p, float yawDeg)
    {
        float h = yawDeg * 3.14159265f / 360.0f;
        Ogre::Quaternion q(cosf(h), 0.0f, sinf(h), 0.0f);
        Ogre::Vector3 v(p[0], p[1], p[2]);
        CharMovement* cm = c->getMovement();
        if (cm) cm->_NV__setPositionDirectionAndTeleport(v, q);
    }
    __declspec(noinline) void BuildHand(Slot& s, Character* c)
    {
        new (s.hand) hand(static_cast<RootObjectBase*>(c));
    }

    Character* Resolve(int slot)
    {
        if (!SlotOk(slot)) return NULL;
        __try
        {
            return HandOf(g_slots[slot])->getCharacter();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return NULL;
        }
    }

    // The hand resolves to the body spawned in this slot: not a player character, our name, our faction.
    bool Identity(Character* c, const Slot& s)
    {
        __try
        {
            if (!c || c->isPlayerCharacter()) return false;
            char name[64];
            CopyName(c, name, sizeof(name));
            if (strcmp(name, s.name) != 0) return false;
            Faction* f = c->getFaction();
            if (!f) return false;
            char fn[64];
            CopyFactionName(f, fn, sizeof(fn));
            return strcmp(fn, s.faction) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // 0 fault/none; the spawn steps in one __try (no C++ objects with destructors in this function).
    Character* SpawnCalls(GameWorld* gw, const kp::RemoteConfig& cfg, const float* pos, char* why, size_t whySize,
                          Faction** factionOut)
    {
        __try
        {
            FactionManager* fm = KP_FIELD(gw, GameWorld, factionMgr);
            RootObjectFactory* fac = KP_FIELD(gw, GameWorld, theFactory);
            if (!fm || !fac) { _snprintf_s(why, whySize, _TRUNCATE, "no faction manager or factory"); return NULL; }
            Faction* f = FactionById(fm, cfg.faction);
            if (!f) { _snprintf_s(why, whySize, _TRUNCATE, "no faction with string id \"%s\"", cfg.faction); return NULL; }
            *factionOut = f;
            Platoon* squad = NewSquad(f, cfg.squadPermanent != 0, pos);
            ActivePlatoon* ap = squad ? squad->getActivePlatoon() : NULL;
            if (!ap) { _snprintf_s(why, whySize, _TRUNCATE, "the faction gave no squad"); return NULL; }
            RootObject* ro = NewBody(fac, f, pos, ap);
            if (!ro) { _snprintf_s(why, whySize, _TRUNCATE, "the factory built nothing"); return NULL; }
            return static_cast<Character*>(ro);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            _snprintf_s(why, whySize, _TRUNCATE, "a game call faulted");
            return NULL;
        }
    }

    bool SetupBody(Character* c, Slot& s, Faction* asked, const float* pos, float yaw, int* asAsked)
    {
        __try
        {
            Faction* f = c->getFaction();
            *asAsked = f == asked ? 1 : 0;
            if (f) CopyFactionName(f, s.faction, sizeof(s.faction));
            else s.faction[0] = 0;
            c->clearAllAIGoals();   // once, right after the spawn (G4; never while it walks: it cancels the move)
            SetBodyName(c, s.name);
            CopyName(c, s.name, sizeof(s.name));   // what the game kept (Identity compares with this)
            BuildHand(s, c);
            if (HandOf(s)->getCharacter() != c) return false;
            Teleport(c, pos, yaw);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // State, max speed and the AI-skip pointer of a verified body.
    bool UpdateBody(Character* c, int slot, float maxSpeed, float wantSpeed, kp::BodyState* out)
    {
        __try
        {
            CharMovement* cm = c->getMovement();
            if (!cm) return false;
            Ogre::Vector3 p = c->getPosition();
            out->pos[0] = p.x; out->pos[1] = p.y; out->pos[2] = p.z;
            AbstractMovementBase* m = cm;
            if (maxSpeed > 0.0f)
            {
                // Its own body only: the max speed chain is CharStats::moveSpeed -> AnimationClass max (+0x19C) ->
                // AbstractMovementBase::maxSpeed each periodic update (1.0.65 0x885590, 0x644840, 0x66036c, M).
                CharStats* st = c->getStats();
                if (st) KP_FIELD(st, CharStats, moveSpeed) = maxSpeed;
                AnimationClass* an = c->getAnimationClass();
                if (an) an->setMaximumSpeed(maxSpeed);
                KP_FIELD(m, AbstractMovementBase, maxSpeed) = maxSpeed;
            }
            if (wantSpeed > 0.0f && KP_FIELD(m, AbstractMovementBase, desiredSpeed) != wantSpeed)
            {
                cm->setDesiredSpeedOrders(RUN);
                cm->setDesiredSpeed(wantSpeed);
            }
            out->speed = KP_FIELD(m, AbstractMovementBase, currentSpeed);
            out->moving = KP_FIELD(m, AbstractMovementBase, currentlyMoving) ? 1 : 0;
            out->desired = KP_FIELD(m, AbstractMovementBase, desiredSpeed);
            out->maxSpeed = KP_FIELD(m, AbstractMovementBase, maxSpeed);
            out->walkSpeed = KP_FIELD(m, AbstractMovementBase, walkSpeed);
            out->speedOrders = (int)KP_FIELD(m, AbstractMovementBase, speedOrders);
            MedicalSystem* med = c->getMedical();
            out->down = c->isDead() ? 2 : ((med && med->isUnconcious()) ? 1 : 0);
            (void)slot;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void* AiOf(Character* c)
    {
        __try { return c ? (void*)KP_FIELD(c, Character, ai) : NULL; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return NULL; }
    }

    // 1 asked, 0 the hand did not resolve, -1 faulted.
    int DestroyCalls(Character* c, const char* reason, bool* ret)
    {
        __try
        {
            GameWorld* gw = kp::GameWorldGlobal();
            *ret = gw->destroy(static_cast<RootObject*>(c), false, reason);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    // Characters in the update list (POD copy).
    unsigned int CopyUpdateList(GameWorld* gw, Character** out, unsigned int max)
    {
        __try
        {
            unsigned int n = 0;
            const ogre_unordered_set<Character*>::type& set = gw->getCharacterUpdateList();
            for (ogre_unordered_set<Character*>::type::const_iterator it = set.begin(); it != set.end() && n < max; ++it) out[n++] = *it;
            return n;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    // 1: a "[KP] " body that is not a player character; 0 no; -1 fault.
    int IsStray(Character* c, char* name, size_t nameSize)
    {
        __try
        {
            if (!c || c->isPlayerCharacter()) return 0;
            CopyName(c, name, nameSize);
            return strncmp(name, kNamePrefix, sizeof(kNamePrefix) - 1) == 0 ? 1 : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    bool OwnBody(Character* c)
    {
        for (int i = 0; i < KP_SKIP_REMOTE; ++i)
            if (g_slots[i].used && Resolve(i) == c) return true;
        return false;
    }

    typedef void (*ProcessKillListFn)(GameWorld*, bool);   // GameWorld::processKillList is protected in KenshiLib's header
    bool ProcessKillListNow()
    {
        __try
        {
            ProcessKillListFn fn = reinterpret_cast<ProcessKillListFn>(kp::SiteAddress(KPS_GameWorld__processKillList));
            if (!fn) return false;
            fn(kp::GameWorldGlobal(), true);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // The save guard: SaveManager::saveGame is the one place that writes a save (its only caller is
    // SaveManager::execute 0x47c11b; save() and the autosave only ask for one, 1.0.65 0x47b0fe / 0x47b95f, M).
    typedef int (*SaveGameFn)(SaveManager*, const std::string&, const std::string&);
    SaveGameFn g_saveOrig = NULL;
    int SaveGameDetour(SaveManager* self, const std::string& location, const std::string& name)
    {
        if (g_saveGuard)
        {
            int n = 0, still = 0;
            for (int i = 0; i < KP_SKIP_REMOTE; ++i)
            {
                Slot& s = g_slots[i];
                if (!s.used || s.destroyed) continue;
                Character* c = Resolve(i);
                if (!c || !Identity(c, s)) continue;
                kp::AiSkipSet(1 + i, NULL);
                bool ret = false;
                if (DestroyCalls(c, "KenshiPure: a save starts", &ret) == 1) { s.destroyed = true; ++n; }
            }
            if (n > 0)
            {
                bool flushed = ProcessKillListNow();
                for (int i = 0; i < KP_SKIP_REMOTE; ++i)
                    if (g_slots[i].used && g_slots[i].destroyed && Resolve(i)) ++still;
                ++g_savesGuarded;
                kp::Log("remote: a save starts - removed %d bod%s first (kill list %s, %d still resolve)", n, n == 1 ? "y" : "ies",
                        flushed ? "processed" : "NOT processed (faulted)", still);
            }
        }
        return g_saveOrig(self, location, name);
    }
}

namespace kp
{
    bool RemoteGameInit(const RemoteConfig& cfg)
    {
        if (!cfg.on)
        {
            Log("remote: off (remote=off in KenshiPure.cfg, or the stand-in is on)");
            return false;
        }
        static const int need[] = {
            KPS_FactionManager__getFactionByStringID, KPS_GameWorld__factionMgr, KPS_Faction__createNewEmptyActivePlatoon,
            KPS_Platoon__getActivePlatoon, KPS_RootObjectFactory__createRandomCharacter, KPS_GameWorld__theFactory,
            KPS_GameWorld__destroy_RootObject_, KPS_Character__clearAllAIGoals, KPS_Character__getStats,
            KPS_Character__getAnimationClass, KPS_AnimationClass__setMaximumSpeed, KPS_CharStats__moveSpeed,
            KPS_vslot_Character__setName, KPS_vslot_Character__getFaction, KPS_Faction__getName, KPS_vslot_Character__getName,
            KPS_vslot_Character__getPosition, KPS_vslot_Character__getOrientation, KPS_hand__hand_RootObjectBase__,
            KPS_hand__getCharacter, KPS_Character__isPlayerCharacter, KPS_Character__isDead, KPS_Character__getMedical,
            KPS_MedicalSystem__isUnconcious, KPS_Character__getMovement, KPS_Character__ai,
            KPS_AbstractMovementBase__currentSpeed, KPS_AbstractMovementBase__currentlyMoving,
            KPS_AbstractMovementBase__desiredSpeed, KPS_AbstractMovementBase__maxSpeed, KPS_AbstractMovementBase__walkSpeed,
            KPS_AbstractMovementBase__speedOrders, KPS_Character__setDestination, KPS_CharMovement__setDestination,
            KPS_AbstractMovementBase__setDesiredSpeed, KPS_AbstractMovementBase__setDesiredSpeedOrders,
            KPS_CharMovement__setDirectMovement, KPS_CharMovement__setPositionDirectionAndTeleport,
            KPS_CharMovement__setPositionSimple, KPS_Character__teleportVisuallyOnly, KPS_CharMovement__halt,
            KPS_AI__periodicUpdate, KPS_GameWorld__getCharacterUpdateList,
        };
        for (size_t i = 0; i < sizeof(need) / sizeof(need[0]); ++i)
            if (!SiteOk(need[i]))
            {
                Log("remote: off (row %s is unresolved for this build)", g_sites[need[i]].name);
                return false;
            }
        if (cfg.drive != DM_ORDER && !AiSkipInstall())   // steer and auto skip the AI while steering
        {
            Log("remote: off (the AI-skip detour could not be installed)");
            return false;
        }
        memset(g_slots, 0, sizeof(g_slots));
        g_saveGuard = false;
        if (cfg.saveGuard)
        {
            if (SiteOk(KPS_SaveManager__saveGame) && SiteOk(KPS_GameWorld__processKillList) &&
                HookSite(KPS_SaveManager__saveGame, (void*)&SaveGameDetour, reinterpret_cast<void**>(&g_saveOrig)))
                g_saveGuard = true;
            else
            {
                Log("remote: off (the save guard could not be installed; remote_save_guard=0 runs without it)");
                return false;
            }
        }
        Log("remote: on (drive %s, faction %s, max speed %.0f, range %.0f, settle %u s, squads %s, save guard %s)",
            DriveModeName(cfg.drive), cfg.faction, cfg.maxSpeed, cfg.range, cfg.settleMs / 1000,
            cfg.squadPermanent ? "permanent" : "temporary", g_saveGuard ? "on" : "OFF");
        return true;
    }

    int RemoteGameSweep()
    {
        GameWorld* gw = GameWorldGlobal();
        static Character* all[2048];
        unsigned int n = gw ? CopyUpdateList(gw, all, 2048) : 0;
        int found = 0;
        for (unsigned int i = 0; i < n; ++i)
        {
            char name[64] = "";
            if (IsStray(all[i], name, sizeof(name)) != 1 || OwnBody(all[i])) continue;
            ++found;
            bool ret = false;
            int r = DestroyCalls(all[i], "KenshiPure: stray remote body", &ret);
            Log("remote: STRAY body \"%s\" in the world (not one of ours: a save kept it?) - destroy %s", name,
                r == 1 ? "asked" : "faulted");
        }
        return found;
    }

    bool RemoteGameSpawn(const RemoteConfig& cfg, const char* playerName, const float* pos, float yaw, RemoteSpawnInfo* out)
    {
        int slot = -1;
        for (int i = 0; i < KP_SKIP_REMOTE && slot < 0; ++i)
            if (!g_slots[i].used) slot = i;
        if (slot < 0) { Log("remote: no free body slot"); return false; }
        GameWorld* gw = GameWorldGlobal();
        if (!gw) return false;
        RemoteGameSweep();   // a body a save kept (G7) carries our prefix: remove it before building a new one
        Slot& s = g_slots[slot];
        memset(&s, 0, sizeof(s));
        _snprintf_s(s.name, sizeof(s.name), _TRUNCATE, "%s%s", kNamePrefix, playerName && playerName[0] ? playerName : "?");
        char why[128] = "";
        Faction* asked = NULL;
        Character* c = SpawnCalls(gw, cfg, pos, why, sizeof(why), &asked);
        if (!c)
        {
            Log("remote: spawn failed: %s", why);
            return false;
        }
        int asAsked = 0;
        if (!SetupBody(c, s, asked, pos, yaw, &asAsked))
        {
            // Built, but we cannot hold it: destroy it at once rather than leave a stray body.
            bool ret = false;
            int r = DestroyCalls(c, "KenshiPure: spawn setup failed", &ret);
            Log("remote: spawn failed: setting up the new body faulted or its hand did not resolve back (destroy %s)",
                r == 1 ? "asked" : "faulted");
            return false;
        }
        s.used = true;
        out->slot = slot;
        strcpy_s(out->faction, sizeof(out->faction), s.faction[0] ? s.faction : "(none)");
        out->factionAsAsked = asAsked;
        Log("remote: body \"%s\" built in slot %d", s.name, slot);
        return true;
    }

    bool RemoteGameUpdate(int slot, float maxSpeed, float wantSpeed, BodyState* out)
    {
        Character* c = Resolve(slot);
        if (!c || g_slots[slot].destroyed || !Identity(c, g_slots[slot]) || !UpdateBody(c, slot, maxSpeed, wantSpeed, out))
        {
            if (slot >= 0 && slot < KP_SKIP_REMOTE) AiSkipSet(1 + slot, NULL);
            return false;
        }
        return true;
    }

    bool RemoteGameApply(int slot, const DriveCmd& cmd, int mode)
    {
        Character* c = Resolve(slot);
        return c && !g_slots[slot].destroyed && DriveApplyTo(c, cmd, mode);
    }

    void RemoteGameSkipAi(int slot, bool on)
    {
        if (slot < 0 || slot >= KP_SKIP_REMOTE) return;
        AiSkipSet(1 + slot, on ? AiOf(Resolve(slot)) : NULL);
    }

    bool RemoteGameDestroy(int slot, const char* reason)
    {
        if (!SlotOk(slot)) return false;
        AiSkipSet(1 + slot, NULL);   // before destroy: a freed AI's address may be reused
        Slot& s = g_slots[slot];
        Character* c = Resolve(slot);
        if (!c || !Identity(c, s)) return false;
        bool ret = false;
        int r = DestroyCalls(c, reason, &ret);
        if (r != 1) return false;
        s.destroyed = true;
        Log("remote: destroy(\"%s\") returned %d", s.name, ret ? 1 : 0);
        return true;
    }

    bool RemoteGameExists(int slot)
    {
        Character* c = Resolve(slot);
        return c && Identity(c, g_slots[slot]);
    }

    bool RemoteGameVerify(int slot)
    {
        if (!SlotOk(slot)) return false;
        Character* c = Resolve(slot);
        return c && Identity(c, g_slots[slot]);
    }

    void RemoteGameFree(int slot)
    {
        if (slot < 0 || slot >= KP_SKIP_REMOTE) return;
        AiSkipSet(1 + slot, NULL);
        memset(&g_slots[slot], 0, sizeof(g_slots[slot]));
    }

    void RemoteGameForgetAll()
    {
        for (int i = 0; i < KP_SKIP_REMOTE; ++i) RemoteGameFree(i);
    }
}
