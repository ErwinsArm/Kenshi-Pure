// M5 game adapter for remote bodies (Remote.h has the logic): spawns a body with the G3 recipe
// (docs/findings/remote-bodies.md) into its own temporary squad, keeps one hand per slot (never a pointer across
// frames), keeps its max speed, carries out drive commands (StandIn.cpp's calls), destroys it, tells whether a
// place lies in a loaded zone, and keeps our squads out of every save (Platoon::serialiseEverything detour: the
// body stays in the world). Frame thread only (saves run on the game's main thread too); every game call inside
// __try. Built with the VC++ 2010 (v100) toolset.
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
#include <kenshi/ZoneManager.h>
#include <kenshi/FactionRelations.h>
#include <kenshi/Animation/AnimationClass.h>

namespace
{
    // The name every body gets: a fixed prefix nobody's NPC carries, so a hand that resolves to anything
    // else after a load is refused, and a save that kept a body can be told from the game's own people.
    const char kNamePrefix[] = "[KP] ";

    struct Slot
    {
        bool used;
        bool destroyed;                       // destroy was asked
        __declspec(align(16)) char hand[sizeof(hand)];
        char name[64];                        // the body's name as set at spawn
        char faction[64];                     // the faction's name as read back at spawn
    };
    Slot g_slots[KP_SKIP_REMOTE];

    // Our squads, kept past their body (an empty squad stays until the game destroys it) so no save writes one. An
    // entry is only compared by address with the live platoon being saved, then confirmed by that platoon's string
    // id: our stored pointer is never dereferenced.
    struct SquadEntry
    {
        const void* platoon;
        char id[64];
    };
    enum { kSquads = 64 };
    SquadEntry g_squads[kSquads];
    unsigned int g_squadNext = 0;
    bool g_saveSkip = false;
    unsigned int g_squadsSkipped = 0;
    char g_faction[64];                       // cfg faction string id (the stray check)

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
    __declspec(noinline) void CopyPlatoonId(Platoon* p, char* out, size_t outSize)
    {
        std::string id = p->getPlatoonStringID();
        _snprintf_s(out, outSize, _TRUNCATE, "%s", id.c_str());
    }
    bool PlatoonId(Platoon* p, char* out, size_t outSize)
    {
        __try
        {
            CopyPlatoonId(p, out, outSize);
            return out[0] != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            out[0] = 0;
            return false;
        }
    }
    // 1 permanent, 0 temporary, -1 fault.
    int PersistentOf(Platoon* p)
    {
        __try { return p->isPersistentSquad() ? 1 : 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }
    bool RememberSquad(Platoon* p, char* id, size_t idSize)
    {
        if (!p || !PlatoonId(p, id, idSize)) return false;
        SquadEntry& e = g_squads[g_squadNext++ % kSquads];
        e.platoon = p;
        strcpy_s(e.id, sizeof(e.id), id);
        return true;
    }

    // As ActivePlatoon::unloadCheck decides whether a squad stays (1.0.65 0x4fe140, M): no zone map there -> stays;
    // a loaded (or loading) zone whose bounds minus 170 units of unloaded edges hold the place -> stays; else the
    // squad unloads after 4.5 s.
    __declspec(noinline) ZoneMap* ZoneAt(ZoneManager* zm, const float* p)
    {
        Ogre::Vector3 v(p[0], p[1], p[2]);
        return zm->getZoneMap(v);
    }
    __declspec(noinline) bool InZoneBounds(ZoneMap* z, const float* p)
    {
        // (min x, min z, max x, max z), compared as at 0x4fe259..0x4fe28b.
        Ogre::Vector4 b = z->getBoundsMinusUnloadedEdges(170.0f);
        return p[0] >= b.x && b.z >= p[0] && p[2] >= b.y && b.w >= p[2];
    }
    int LoadedCalls(GameWorld* gw, const float* p)
    {
        __try
        {
            ZoneManager* zm = KP_FIELD(gw, GameWorld, zoneMgr);
            if (!zm) return -1;
            ZoneMap* z = ZoneAt(zm, p);
            if (!z) return 1;
            if (!z->isLoadedMT() && !z->isBeingLoadedMT()) return 0;
            return InZoneBounds(z, p) ? 1 : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    // A faction's relation towards another (FactionRelations::getFactionRelation; an NPC counts a faction as an enemy
    // at <= -30, FactionRelations::isEnemy 1.0.65 0x6b23b3, M). false: no relations object or a fault.
    bool RelationOf(Faction* from, Faction* to, float* out)
    {
        __try
        {
            FactionRelations* r = KP_FIELD(from, Faction, relations);
            if (!r) return false;
            *out = r->getFactionRelation(to);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
    Faction* FactionOf(Character* c)
    {
        __try { return c ? c->getFaction() : NULL; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return NULL; }
    }
    bool FactionName(Faction* f, char* out, size_t outSize)
    {
        __try
        {
            CopyFactionName(f, out, outSize);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            out[0] = 0;
            return false;
        }
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
                          Faction** factionOut, Platoon** squadOut)
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
            *squadOut = squad;
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

    // 1: a "[KP] " body in our faction that is not a player character; 0 no; -1 fault.
    int IsStray(Character* c, Faction* ours, char* name, size_t nameSize)
    {
        __try
        {
            if (!c || !ours || c->isPlayerCharacter() || c->getFaction() != ours) return 0;
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

    Faction* OurFaction()
    {
        GameWorld* gw = kp::GameWorldGlobal();
        __try
        {
            FactionManager* fm = gw ? KP_FIELD(gw, GameWorld, factionMgr) : NULL;
            return fm ? FactionById(fm, g_faction) : NULL;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return NULL;
        }
    }

    // How the factions around a new body stand towards ours (and ours towards them): whether local NPCs would attack it.
    void LogRelations(Faction* ours)
    {
        GameWorld* gw = kp::GameWorldGlobal();
        static Character* all[2048];
        unsigned int n = gw && ours ? CopyUpdateList(gw, all, 2048) : 0;
        Faction* seen[24];
        int counts[24];
        int ns = 0;
        for (unsigned int i = 0; i < n; ++i)
        {
            Faction* f = FactionOf(all[i]);
            if (!f || f == ours) continue;
            int k = 0;
            while (k < ns && seen[k] != f) ++k;
            if (k < ns) { ++counts[k]; continue; }
            if (ns == 24) continue;
            seen[ns] = f;
            counts[ns++] = 1;
        }
        for (int k = 0; k < ns; ++k)
        {
            char name[64];
            float theirs = 0.0f, back = 0.0f;
            bool okT = RelationOf(seen[k], ours, &theirs), okB = RelationOf(ours, seen[k], &back);
            FactionName(seen[k], name, sizeof(name));
            if (okT && okB)
                kp::Log("remote: relation of \"%s\" (%d nearby) towards our faction %.0f%s, ours towards it %.0f%s", name,
                        counts[k], theirs, theirs <= -30.0f ? " ENEMY" : "", back, back <= -30.0f ? " ENEMY" : "");
            else
                kp::Log("remote: relation of \"%s\" (%d nearby) unreadable", name, counts[k]);
        }
    }

    // Saves skip our squads: Faction::saveState calls Platoon::serialiseEverything for each of its squads and ignores
    // the result (1.0.65 0x372d05 / 0x372d54, M); serialiseEverything is the one writer of a squad, its characters and its
    // platoon file (ActivePlatoon::serialiseEverythingToDisk -> saveToDisk "platoon/<id>.platoon", its only callers,
    // 0x373839 / 0x4feb3c, M) and returns NULL itself for squads it does not save (dead ones far from the player,
    // 0x372dfb..0x372ef9, M). So for ours we take that same path; the body stays in the world.
    typedef GameData* (*SerialiseFn)(Platoon*, GameDataContainer*, bool);
    SerialiseFn g_serialiseOrig = NULL;
    GameData* SerialiseDetour(Platoon* self, GameDataContainer* container, bool levelEditor)
    {
        if (g_saveSkip && self)
            for (int i = 0; i < kSquads; ++i)
            {
                if (g_squads[i].platoon != self) continue;
                char id[64];
                if (PlatoonId(self, id, sizeof(id)) && strcmp(id, g_squads[i].id) == 0)
                {
                    ++g_squadsSkipped;
                    kp::Log("remote: the save skips our squad %s", id);
                    return NULL;
                }
            }
        return g_serialiseOrig(self, container, levelEditor);
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
            KPS_AI__periodicUpdate, KPS_GameWorld__getCharacterUpdateList, KPS_Platoon__getPlatoonStringID,
            KPS_Platoon__isPersistentSquad, KPS_GameWorld__zoneMgr, KPS_ZoneManager__getZoneMap_Vector3_,
            KPS_ZoneMap__isLoadedMT, KPS_ZoneMap__isBeingLoadedMT, KPS_ZoneMap__getBoundsMinusUnloadedEdges,
            KPS_Faction__relations, KPS_FactionRelations__getFactionRelation,
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
        memset(g_squads, 0, sizeof(g_squads));
        strcpy_s(g_faction, sizeof(g_faction), cfg.faction);
        g_saveSkip = false;
        if (cfg.saveGuard)
        {
            if (SiteOk(KPS_Platoon__serialiseEverything) &&
                HookSite(KPS_Platoon__serialiseEverything, (void*)&SerialiseDetour, reinterpret_cast<void**>(&g_serialiseOrig)))
                g_saveSkip = true;
            else
            {
                Log("remote: off (the save skip could not be installed; remote_save_guard=0 runs without it)");
                return false;
            }
        }
        Log("remote: on (drive %s, faction %s, max speed %.0f, range %.0f, settle %u s, squads %s, saves skip our squads: %s)",
            DriveModeName(cfg.drive), cfg.faction, cfg.maxSpeed, cfg.range, cfg.settleMs / 1000,
            cfg.squadPermanent ? "permanent" : "temporary", g_saveSkip ? "yes" : "NO");
        return true;
    }

    int RemoteGameSweep()
    {
        GameWorld* gw = GameWorldGlobal();
        static Character* all[2048];
        Faction* ours = OurFaction();
        unsigned int n = gw && ours ? CopyUpdateList(gw, all, 2048) : 0;
        int found = 0;
        for (unsigned int i = 0; i < n; ++i)
        {
            char name[64] = "";
            if (IsStray(all[i], ours, name, sizeof(name)) != 1 || OwnBody(all[i])) continue;
            ++found;
            bool ret = false;
            int r = DestroyCalls(all[i], "KenshiPure: stray remote body", &ret);
            Log("remote: STRAY body \"%s\" in the world (not one of ours: a save from an older build kept it?) - destroy %s",
                name, r == 1 ? "asked" : "faulted");
        }
        return found;
    }

    int RemoteGameLoadedAt(const float* pos)
    {
        GameWorld* gw = GameWorldGlobal();
        return gw && pos ? LoadedCalls(gw, pos) : -1;
    }

    bool RemoteGameSpawn(const RemoteConfig& cfg, const char* playerName, const float* pos, float yaw, RemoteSpawnInfo* out)
    {
        int slot = -1;
        for (int i = 0; i < KP_SKIP_REMOTE && slot < 0; ++i)
            if (!g_slots[i].used) slot = i;
        if (slot < 0) { Log("remote: no free body slot"); return false; }
        GameWorld* gw = GameWorldGlobal();
        if (!gw) return false;
        RemoteGameSweep();   // a body an older build's save kept (G7) carries our prefix: remove it before building a new one
        Slot& s = g_slots[slot];
        memset(&s, 0, sizeof(s));
        _snprintf_s(s.name, sizeof(s.name), _TRUNCATE, "%s%s", kNamePrefix, playerName && playerName[0] ? playerName : "?");
        char why[128] = "";
        Faction* asked = NULL;
        Platoon* squad = NULL;
        Character* c = SpawnCalls(gw, cfg, pos, why, sizeof(why), &asked, &squad);
        // Remember the squad before anything else: from now on no save may write it, with or without its body.
        char squadId[64] = "";
        bool remembered = RememberSquad(squad, squadId, sizeof(squadId));
        if (squad && !remembered)
        {
            if (c)
            {
                bool ret = false;
                DestroyCalls(c, "KenshiPure: squad without an id", &ret);
            }
            Log("remote: spawn failed: the new squad has no string id, so a save could not skip it (body destroyed)");
            return false;
        }
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
        int persistent = PersistentOf(squad);
        Log("remote: body \"%s\" built in slot %d, squad %s (%s)", s.name, slot, squadId,
            persistent == 1 ? "permanent" : persistent == 0 ? "temporary" : "persistence unreadable");
        LogRelations(asked);
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
