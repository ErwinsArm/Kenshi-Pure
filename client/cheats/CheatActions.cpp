// KenshiPure cheats: the actions behind the cheat window (heal, damage, knock out, create item,
// teleport). Part of KenshiPureCheats.dll (docs/client.md "Cheat window").
// Every game access runs on the game thread inside __try and copies plain data (as Probe.cpp does).
// Built with the VC++ 2010 (v100) toolset; keep to C++ that compiler accepts.
#include "CheatActions.h"
#include "Log.h"

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>

#include "Game.h"

#define KP_CHEAT_MAX_ITEMS 32
#define KP_CHEAT_MAX_OWNED 256

namespace
{
    char g_items[KP_CHEAT_MAX_ITEMS][96];
    int g_itemCount = 0;
    // The weapon maker passed to createItem for weapons (the game's own fallback maker by default).
    char g_weaponMaker[96] = "1057-gamedata.base";
    // Result of the last action, for the window's status line.
    char g_msg[256] = "";

    // Logs "cheat: ..." and keeps the text as the last result.
    void Say(const char* fmt, ...)
    {
        va_list a;
        va_start(a, fmt);
        _vsnprintf_s(g_msg, sizeof(g_msg), _TRUNCATE, fmt, a);
        va_end(a);
        kp::Log("cheat: %s", g_msg);
    }

    // A teleport is checked again once the game has run for a second (see CheckTeleport).
    struct PendingTeleport
    {
        bool active;
        Character* who;
        float to[3];
        ULONGLONG madeAt;
        ULONGLONG runningSince;   // 0 until the game runs
    };
    PendingTeleport g_tp;

    void Trim(char* s)
    {
        size_t n = strlen(s);
        while (n && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
        size_t i = 0;
        while (s[i] == ' ' || s[i] == '\t') ++i;
        if (i) memmove(s, s + i, n - i + 1);
    }

    void AddItem(const char* name)
    {
        if (g_itemCount >= KP_CHEAT_MAX_ITEMS) return;
        char* e = g_items[g_itemCount];
        _snprintf_s(e, sizeof(g_items[0]), _TRUNCATE, "%s", name);
        Trim(e);
        if (e[0]) ++g_itemCount;
    }

    // Optional KenshiPure-cheats.cfg next to the DLL: cheat_item=<name or string ID> (repeatable, the
    // item list of the window) and cheat_weapon_maker=<name or string ID>.
    void ReadCfg()
    {
        wchar_t path[MAX_PATH];
        _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%sKenshiPure-cheats.cfg", kp::PluginDir());
        FILE* f = NULL;
        if (_wfopen_s(&f, path, L"r") == 0 && f)
        {
            char line[256];
            while (fgets(line, sizeof(line), f))
            {
                char* eq = strchr(line, '=');
                if (!eq) continue;
                *eq = 0;
                char* value = eq + 1;
                Trim(line);
                Trim(value);
                if (strcmp(line, "cheat_item") == 0) AddItem(value);
                else if (strcmp(line, "cheat_weapon_maker") == 0 && value[0])
                    _snprintf_s(g_weaponMaker, sizeof(g_weaponMaker), _TRUNCATE, "%s", value);
            }
            fclose(f);
        }
        if (g_itemCount == 0)
        {
            // Found by name in the probe run (docs/client.md "Live game probe").
            AddItem("Bread");
            AddItem("Katana");
            AddItem("Samurai Armour");
        }
    }

    // ---------------------------------------------------------------- target character

    // The player's selected character if it is one of theirs, else NULL.
    Character* Selected(GameWorld* gw)
    {
        __try
        {
            PlayerInterface* pi = KP_FIELD(gw, GameWorld, player);
            if (!pi) return NULL;
            Character* sel = KP_FIELD(pi, PlayerInterface, selectedCharacter).getCharacter();
            if (!sel) return NULL;
            lektor<Character*>& chars = KP_FIELD(pi, PlayerInterface, playerCharacters);
            unsigned int n = chars.count;
            if (!chars.stuff || n > KP_CHEAT_MAX_OWNED) return NULL;
            for (unsigned int i = 0; i < n; ++i)
                if (chars.stuff[i] == sel) return sel;
            return NULL;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return NULL;
        }
    }

    // Is c still one of the player's characters (a remembered pointer may be stale)?
    bool StillOwned(GameWorld* gw, Character* c)
    {
        __try
        {
            PlayerInterface* pi = KP_FIELD(gw, GameWorld, player);
            if (!pi) return false;
            lektor<Character*>& chars = KP_FIELD(pi, PlayerInterface, playerCharacters);
            unsigned int n = chars.count;
            if (!chars.stuff || n > KP_CHEAT_MAX_OWNED) return false;
            for (unsigned int i = 0; i < n; ++i)
                if (chars.stuff[i] == c) return true;
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    __declspec(noinline) void CopyName(Character* c, char* out, size_t outSize)
    {
        std::string n = c->getName();
        _snprintf_s(out, outSize, _TRUNCATE, "%s", n.c_str());
    }

    bool SafeName(Character* c, char* out, size_t outSize)
    {
        __try
        {
            out[0] = 0;
            CopyName(c, out, outSize);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            _snprintf_s(out, outSize, _TRUNCATE, "?");
            return false;
        }
    }

    __declspec(noinline) void CopyPosition(Character* c, float* xyz)
    {
        Ogre::Vector3 v = c->getPosition();
        xyz[0] = v.x; xyz[1] = v.y; xyz[2] = v.z;
    }

    bool SafePosition(Character* c, float* xyz)
    {
        __try
        {
            CopyPosition(c, xyz);
            return _finite(xyz[0]) && _finite(xyz[1]) && _finite(xyz[2]);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SafePaused(GameWorld* gw, bool* paused)
    {
        __try
        {
            *paused = gw->isPaused();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // ---------------------------------------------------------------- medical

    enum MedicalMode { M_HEAL, M_DAMAGE, M_KNOCKOUT };

    struct MedicalResult { int parts; float bloodBefore, bloodAfter, koTimer; };

    // MedicalSystem::reassessCollapseMode (1.0.65 0x648890) sets `unconcious` (+0x161) when
    // knockoutTimer (+0xA0) is above a threshold (measured; it runs from the periodic medical update,
    // not every frame). The unit of the timer (game seconds?) is not measured.
    const float kKnockoutTimer = 20.0f;

    // fleshStun is stun DAMAGE: shown health = (flesh - fleshStun) / max (measured, docs/client.md).
    // Death needs a torso or the head far below zero; damage stops at -50 % of max per part
    // (a safety margin, inferred); knock out adds stun damage (recovers on its own) and the KO timer.
    bool MedicalOne(Character* c, MedicalMode mode, MedicalResult* r)
    {
        __try
        {
            r->parts = 0;
            MedicalSystem* m = c->getMedical();
            if (!m) return false;
            int count = m->getPartCount();
            for (int i = 0; i < count; ++i)
            {
                MedicalSystem::HealthPartStatus* p = m->getPart((unsigned __int64)i);
                if (!p) continue;
                float maxHealth = KP_FIELD(p, MedicalSystem::HealthPartStatus, _maxHealth);
                if (!(maxHealth > 0.0f)) continue;
                float& flesh = KP_FIELD(p, MedicalSystem::HealthPartStatus, flesh);
                float& stun = KP_FIELD(p, MedicalSystem::HealthPartStatus, fleshStun);
                MedicalSystem::HealthPartStatus::PartType type = KP_FIELD(p, MedicalSystem::HealthPartStatus, whatAmI);
                bool robotic = KP_FIELD(p, MedicalSystem::HealthPartStatus, robotLimb) != NULL;
                if (mode == M_HEAL)
                {
                    flesh = maxHealth;
                    stun = 0.0f;
                }
                else if (mode == M_DAMAGE)
                {
                    if (robotic) continue;
                    float f = flesh - 0.3f * maxHealth;
                    float floor = -0.5f * maxHealth;
                    if (f < floor) f = floor;
                    if (f < flesh) flesh = f;
                }
                else
                {
                    if (robotic) continue;
                    if (type != MedicalSystem::HealthPartStatus::PART_TORSO &&
                        type != MedicalSystem::HealthPartStatus::PART_HEAD) continue;
                    float s = flesh + 0.2f * maxHealth;   // (flesh - s) / max = -0.2
                    if (s < 0.0f) s = 0.0f;
                    if (s > stun) stun = s;
                }
                p->updateDerivedHealths();
                ++r->parts;
            }
            r->bloodBefore = KP_FIELD(m, MedicalSystem, blood);
            float& koTimer = KP_FIELD(m, MedicalSystem, knockoutTimer);
            if (mode == M_HEAL)
            {
                KP_FIELD(m, MedicalSystem, blood) = m->getMaxBlood();
                KP_FIELD(m, MedicalSystem, currentBleedRate) = 0.0f;
                koTimer = 0.0f;   // else a knocked-out character stays down after the heal
            }
            else if (mode == M_KNOCKOUT && koTimer < kKnockoutTimer)
                koTimer = kKnockoutTimer;
            r->koTimer = koTimer;
            r->bloodAfter = KP_FIELD(m, MedicalSystem, blood);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool DoMedical(GameWorld* gw, MedicalMode mode, const char* what)
    {
        Character* c = Selected(gw);
        if (!c) { Say("%s - no own character selected", what); return false; }
        char nm[96];
        SafeName(c, nm, sizeof(nm));
        MedicalResult r;
        if (!MedicalOne(c, mode, &r)) { Say("%s %s - faulted or no medical system", what, nm); return false; }
        Say("%s %s - %d parts changed, blood %.1f -> %.1f, knockout timer %.1f", what, nm, r.parts, r.bloodBefore,
                r.bloodAfter, r.koTimer);
        return r.parts > 0;
    }

    // ---------------------------------------------------------------- items

    __declspec(noinline) GameData* FindData(GameWorld* gw, const char* key, itemType type)
    {
        std::string k(key);
        GameData* d = KP_FIELD(gw, GameWorld, gamedata).getDataByName(k, type);
        if (!d) d = KP_FIELD(gw, GameWorld, gamedata).getData(k, type);   // string ID
        return d;
    }

    // Categories createItem builds from a GameData of that type (WEAPON through the maker, below).
    const itemType kSpawnTypes[] = { ITEM, WEAPON, ARMOUR, CROSSBOW };

    bool SafeFind(GameWorld* gw, const char* key, GameData** out, itemType* type)
    {
        __try
        {
            *out = NULL;
            for (size_t i = 0; i < sizeof(kSpawnTypes) / sizeof(kSpawnTypes[0]); ++i)
            {
                GameData* d = FindData(gw, key, kSpawnTypes[i]);
                if (d) { *out = d; *type = kSpawnTypes[i]; return true; }
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SafeFindMaker(GameWorld* gw, GameData** out)
    {
        __try
        {
            *out = FindData(gw, g_weaponMaker, WEAPON_MANUFACTURER);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // createItem(gd, ...) returns NULL at once for a GameData of type WEAPON (measured, 1.0.65
    // 0x57fcc0: `cmp eax,2; je` to `xor eax,eax`). Weapons are built from a WEAPON_MANUFACTURER gd,
    // with the weapon's own GameData in the third argument (null there = a random one of the maker's
    // "weapon types") and the material picked from the maker's list when the fourth is null.
    __declspec(noinline) Item* MakeItem(GameWorld* gw, GameData* gd, GameData* maker)
    {
        hand none;
        RootObjectFactory* f = KP_FIELD(gw, GameWorld, theFactory);
        if (maker) return f->createItem(maker, none, gd, NULL, -1, NULL);
        return f->createItem(gd, none, NULL, NULL, -1, NULL);
    }

    __declspec(noinline) void CopyItemName(Item* it, char* out, size_t outSize)
    {
        std::string n = it->getName();
        _snprintf_s(out, outSize, _TRUNCATE, "%s", n.c_str());
    }

    // 1 added to the inventory, 2 dropped next to the character (inventory full: addItem drops on
    // fail), 0 the factory gave nothing, -1 faulted.
    int SpawnUnit(GameWorld* gw, Character* c, GameData* gd, GameData* maker, char* name, size_t nameSize, int* firstQty)
    {
        __try
        {
            Item* it = MakeItem(gw, gd, maker);
            if (!it) return 0;
            if (!name[0])
            {
                CopyItemName(it, name, nameSize);
                if (kp::SiteOk(KPS_InventoryItemBase__quantity))
                    *firstQty = KP_FIELD(static_cast<InventoryItemBase*>(it), InventoryItemBase, quantity);
            }
            Inventory* inv = c->getInventory();
            if (!inv) return -1;
            // One unit per call: with a quantity > 1 a section splits copies off the item and does not
            // take them back when a later placement fails (docs/client.md "Cheat hotkeys").
            return inv->_NV_addItem(it, 1, true, false) ? 1 : 2;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    bool DoSpawn(GameWorld* gw, const char* key, int count)
    {
        Character* c = Selected(gw);
        if (!c) { Say("spawn item - no own character selected"); return false; }
        GameData* gd = NULL;
        itemType type = ITEM;
        if (!SafeFind(gw, key, &gd, &type)) { Say("spawn \"%s\" - a game call faulted", key); return false; }
        if (!gd) { Say("spawn \"%s\" - no item, weapon, armour or crossbow with that name or ID", key); return false; }
        GameData* maker = NULL;
        if (type == WEAPON)
        {
            if (!SafeFindMaker(gw, &maker) || !maker)
            {
                Say("spawn \"%s\" - weapon maker \"%s\" not found (cheat_weapon_maker)", key, g_weaponMaker);
                return false;
            }
        }
        char made[96] = "";
        int added = 0, dropped = 0, result = 1, firstQty = -1;
        for (int i = 0; i < count; ++i)
        {
            result = SpawnUnit(gw, c, gd, maker, made, sizeof(made), &firstQty);
            if (result == 1) ++added;
            else if (result == 2) ++dropped;
            else break;
        }
        Say("spawn \"%s\" x%d (category %d%s) -> made \"%s\" (quantity %d when made), %d in the inventory, "
                "%d dropped on the ground%s",
                key, count, (int)type, maker ? ", through the weapon maker" : "", made, firstQty, added, dropped,
                result == 0 ? "; the item factory returned nothing" : result < 0 ? "; a game call faulted" : "");
        return added + dropped > 0;
    }

    // ---------------------------------------------------------------- teleport

    __declspec(noinline) void CopyCamera(GameWorld* gw, float* xyz)
    {
        Ogre::Vector3 v = gw->getCameraCenter();
        xyz[0] = v.x; xyz[1] = v.y; xyz[2] = v.z;
    }

    // The game's own Character::teleport (row Character::teleport, 1.0.65 0x5c9bf0): writes the
    // character's own position, calls the movement's _setPositionAndTeleport(pos, -1) and moves the
    // animation. The probe called only the movement part (with floor 0) and lost the move while paused.
    __declspec(noinline) void CallTeleport(Character* c, const float* xyz)
    {
        Ogre::Vector3 p(xyz[0], xyz[1], xyz[2]);
        c->teleport(p);
    }

    bool SafeTeleport(Character* c, const float* xyz)
    {
        __try
        {
            CallTeleport(c, xyz);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SafeCamera(GameWorld* gw, float* xyz)
    {
        __try
        {
            CopyCamera(gw, xyz);
            return _finite(xyz[0]) && _finite(xyz[1]) && _finite(xyz[2]) && fabs(xyz[0]) < 1e7f && fabs(xyz[2]) < 1e7f;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    float DistXZ(const float* a, const float* b)
    {
        float dx = a[0] - b[0], dz = a[2] - b[2];
        return sqrtf(dx * dx + dz * dz);
    }

    bool DoTeleport(GameWorld* gw)
    {
        Character* c = Selected(gw);
        if (!c) { Say("teleport - no own character selected"); return false; }
        char nm[96];
        SafeName(c, nm, sizeof(nm));
        float from[3] = { 0, 0, 0 }, to[3], after[3] = { 0, 0, 0 };
        bool paused = false;
        SafePaused(gw, &paused);
        SafePosition(c, from);
        if (!SafeCamera(gw, to)) { Say("teleport %s - camera centre unreadable", nm); return false; }
        if (!SafeTeleport(c, to)) { Say("teleport %s - faulted", nm); return false; }
        SafePosition(c, after);
        Say("teleport %s%s from %.1f %.1f %.1f to camera centre %.1f %.1f %.1f; position right after %.1f %.1f %.1f",
                nm, paused ? " (paused)" : "", from[0], from[1], from[2], to[0], to[1], to[2], after[0], after[1], after[2]);
        g_tp.active = true;
        g_tp.who = c;
        memcpy(g_tp.to, to, sizeof(to));
        g_tp.madeAt = GetTickCount64();
        g_tp.runningSince = 0;
        return true;
    }

    // One second after the game runs (again), log where the character is and how far from the target
    // (only a log: a character still under a move order walks on, so distance alone proves nothing).
    void CheckTeleport(GameWorld* gw)
    {
        if (!g_tp.active) return;
        ULONGLONG now = GetTickCount64();
        bool paused = true;
        if (!SafePaused(gw, &paused) || now - g_tp.madeAt > 10 * 60 * 1000 || !StillOwned(gw, g_tp.who))
        {
            g_tp.active = false;
            return;
        }
        if (paused) { g_tp.runningSince = 0; return; }
        if (!g_tp.runningSince) { g_tp.runningSince = now; return; }
        if (now - g_tp.runningSince < 1000) return;
        float pos[3];
        if (!SafePosition(g_tp.who, pos)) { g_tp.active = false; return; }
        Say("teleport check after 1 s of running game: at %.1f %.1f %.1f, %.1f from the target (x/z)",
                pos[0], pos[1], pos[2], DistXZ(pos, g_tp.to));
        g_tp.active = false;
    }
}

namespace kp
{
    namespace cheat
    {
        bool Init()
        {
            ReadCfg();
            const char* features[] = { "optional:probe", "optional:probe-write", "optional:cheat-write" };
            for (size_t i = 0; i < sizeof(features) / sizeof(features[0]); ++i)
                if (!FeatureOn(features[i]))
                {
                    Log("cheat: off (feature %s has unresolved addresses for this build)", features[i]);
                    return false;
                }
            for (int i = 0; i < g_itemCount; ++i) Log("cheat: item %d/%d: \"%s\"", i + 1, g_itemCount, g_items[i]);
            Log("cheat: weapon maker \"%s\"", g_weaponMaker);
            return true;
        }

        int ItemCount() { return g_itemCount; }
        const char* ItemName(int i) { return i >= 0 && i < g_itemCount ? g_items[i] : ""; }
        const char* LastResult() { return g_msg; }

        void SelectedName(GameWorld* gw, char* out, size_t outSize)
        {
            Character* c = Selected(gw);
            if (c) SafeName(c, out, outSize);
            else _snprintf_s(out, outSize, _TRUNCATE, "");
        }

        bool Heal(GameWorld* gw) { return DoMedical(gw, M_HEAL, "heal"); }
        bool Damage(GameWorld* gw) { return DoMedical(gw, M_DAMAGE, "damage"); }
        bool KnockOut(GameWorld* gw) { return DoMedical(gw, M_KNOCKOUT, "knock out"); }
        bool Teleport(GameWorld* gw) { return DoTeleport(gw); }
        bool Spawn(GameWorld* gw, const char* key, int count)
        {
            if (count < 1) count = 1;
            if (count > 100) count = 100;
            if (!key || !key[0]) { Say("create item - no item name given"); return false; }
            return DoSpawn(gw, key, count);
        }
        void OnFrame(GameWorld* gw) { CheckTeleport(gw); }
    }
}
