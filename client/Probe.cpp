// Faction.h pulls in Boost.Thread headers, which would auto-link a Boost library we do not build.
// We use no Boost.Thread code, only header-only parts (unordered_set), so switch auto-linking off.
#define BOOST_ALL_NO_LIB
#define BOOST_SYSTEM_NO_DEPRECATED   // drops Boost.System's static error categories (need its .lib)

#include "Probe.h"
#include "Log.h"

#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <string>

#include <core/Functions.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/CharMovement.h>
#include <kenshi/Faction.h>
#include <kenshi/MedicalSystem.h>
#include <kenshi/Inventory.h>
#include <kenshi/Item.h>
#include <kenshi/GameData.h>
#include <kenshi/GameDataManager.h>
#include <kenshi/RootObjectFactory.h>
#include <kenshi/util/hand.h>

#define KP_PROBE_DEFAULT_PORT 47800
#define KP_PROBE_WAIT_MS 3000     // how long a request waits for a frame
#define KP_PROBE_MAX_LINE 4096
#define KP_PROBE_MAX_CHARS 64
#define KP_PROBE_MAX_NEARBY 512
#define KP_PROBE_MAX_ITEMS 400

namespace
{
    char g_buildName[160] = "";
    char g_kenshiVersion[64] = "";
    int g_port = KP_PROBE_DEFAULT_PORT;
    bool g_allowWrite = false;   // probe_write=1 in KenshiPure.cfg

    // One request slot: the server thread (one client at a time) fills it, the frame hook answers it.
    enum SlotState { SLOT_IDLE, SLOT_PENDING, SLOT_DONE };
    CRITICAL_SECTION g_slotLock;
    volatile LONG g_slotState = SLOT_IDLE;   // read without the lock only as a cheap "anything to do?"
    char g_slotLine[KP_PROBE_MAX_LINE + 1];
    std::string g_slotResult;

    // ---------------------------------------------------------------- JSON helpers

    void AppendEscaped(std::string& out, const char* s)
    {
        out += '"';
        for (; *s; ++s)
        {
            unsigned char c = (unsigned char)*s;
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else if (c < 0x20) { char b[8]; _snprintf_s(b, sizeof(b), _TRUNCATE, "\\u%04x", c); out += b; }
            else out += (char)c;   // UTF-8 bytes pass through
        }
        out += '"';
    }

    std::string Fmt(const char* fmt, ...)
    {
        char buf[512];
        va_list a;
        va_start(a, fmt);
        _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, a);
        va_end(a);
        return buf;
    }

    std::string Num(double v)
    {
        if (!_finite(v)) return "null";
        return Fmt("%.4f", v);
    }

    const char* Bool(int v) { return v ? "true" : "false"; }

    std::string ErrorReply(const char* msg)
    {
        std::string r = "{\"ok\":false,\"error\":";
        AppendEscaped(r, msg);
        r += "}";
        return r;
    }

    // Finds "key":"value" (string) in a flat JSON line. No nesting, no escapes: enough for our requests.
    bool GetStr(const char* line, const char* key, char* out, size_t outSize)
    {
        std::string pat = std::string("\"") + key + "\"";
        const char* p = strstr(line, pat.c_str());
        if (!p) return false;
        p += pat.size();
        while (*p == ' ' || *p == ':') ++p;
        if (*p != '"') return false;
        ++p;
        size_t n = 0;
        while (*p && *p != '"' && n + 1 < outSize) out[n++] = *p++;
        out[n] = 0;
        return *p == '"';
    }

    bool GetNum(const char* line, const char* key, double* out)
    {
        std::string pat = std::string("\"") + key + "\"";
        const char* p = strstr(line, pat.c_str());
        if (!p) return false;
        p += pat.size();
        while (*p == ' ' || *p == ':') ++p;
        char* end = NULL;
        double v = strtod(p, &end);
        if (end == p || !_finite(v)) return false;
        *out = v;
        return true;
    }

    int GetInt(const char* line, const char* key, int def)
    {
        double v;
        return GetNum(line, key, &v) ? (int)v : def;
    }

    bool ContainsNoCase(const char* hay, const char* needle)
    {
        if (!*needle) return true;
        for (; *hay; ++hay)
        {
            const char* h = hay;
            const char* n = needle;
            while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) { ++h; ++n; }
            if (!*n) return true;
        }
        return false;
    }

    // ---------------------------------------------------------------- game reads (frame thread only)
    // Rule: functions with __try hold only plain data. Anything that makes a std::string or another
    // C++ object lives in a small noinline helper called from inside the __try.

    __declspec(noinline) void CopyStr(const std::string& s, char* out, size_t outSize)
    {
        _snprintf_s(out, outSize, _TRUNCATE, "%s", s.c_str());
    }

    __declspec(noinline) void CopyName(RootObjectBase* o, char* out, size_t outSize)
    {
        std::string n = o->getName();
        _snprintf_s(out, outSize, _TRUNCATE, "%s", n.c_str());
    }

    __declspec(noinline) void CopyFaction(Character* c, char* out, size_t outSize)
    {
        out[0] = 0;
        Faction* f = c->getFaction();
        if (f) CopyStr(f->getName(), out, outSize);
    }

    __declspec(noinline) void CopyPos(Character* c, float* xyz)
    {
        Ogre::Vector3 v = c->getPosition();
        xyz[0] = v.x; xyz[1] = v.y; xyz[2] = v.z;
    }

    __declspec(noinline) void CopyCamera(GameWorld* gw, float* xyz)
    {
        Ogre::Vector3 v = gw->getCameraCenter();
        xyz[0] = v.x; xyz[1] = v.y; xyz[2] = v.z;
    }

    struct StatusSample
    {
        int sameWorld, paused, hasPlayer;
        unsigned int players;
        float speed, hours;
        double stamp;
        float cam[3];
    };

    bool SampleStatus(GameWorld* gw, StatusSample* s)
    {
        __try
        {
            s->sameWorld = (ou == gw) ? 1 : 0;
            s->speed = gw->getFrameSpeedMultiplier();
            s->paused = gw->isPaused() ? 1 : 0;
            s->stamp = gw->getTimeStamp();
            s->hours = gw->getTimeFromStamp_inGameHours(s->stamp);
            CopyCamera(gw, s->cam);
            PlayerInterface* pi = gw->player;
            s->hasPlayer = pi ? 1 : 0;
            s->players = pi ? pi->playerCharacters.count : 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    Character* GetSelected(GameWorld* gw)
    {
        __try
        {
            PlayerInterface* pi = gw->player;
            return pi ? pi->selectedCharacter.getCharacter() : NULL;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return NULL;
        }
    }

    struct CharSample
    {
        char name[128];
        char faction[96];
        float x, y, z;
        int player, dead, unconscious, money, hasMedical;
        float blood, maxBlood, hunger, knockoutTimer;
    };

    bool SampleChar(Character* c, CharSample* s)
    {
        __try
        {
            s->name[0] = 0;
            s->faction[0] = 0;
            float p[3];
            CopyName(c, s->name, sizeof(s->name));
            CopyPos(c, p);
            s->x = p[0]; s->y = p[1]; s->z = p[2];
            CopyFaction(c, s->faction, sizeof(s->faction));
            s->player = c->isPlayerCharacter() ? 1 : 0;
            s->dead = c->isDead() ? 1 : 0;
            s->money = c->getMoney();
            MedicalSystem* m = c->getMedical();
            s->hasMedical = m ? 1 : 0;
            s->unconscious = (m && m->isUnconcious()) ? 1 : 0;
            s->blood = m ? m->blood : 0.0f;
            s->maxBlood = m ? m->getMaxBlood() : 0.0f;
            s->hunger = m ? m->hunger : 0.0f;
            s->knockoutTimer = m ? m->knockoutTimer : 0.0f;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
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
            return false;
        }
    }

    // POD copy of the character pointers, so no fault can happen while we hold C++ objects.
    bool CopyPlayerList(GameWorld* gw, Character** list, unsigned int* n)
    {
        __try
        {
            *n = 0;
            PlayerInterface* pi = gw->player;
            if (!pi || !pi->playerCharacters.stuff) return true;
            unsigned int count = pi->playerCharacters.count;
            if (count > KP_PROBE_MAX_CHARS) count = KP_PROBE_MAX_CHARS;
            for (unsigned int i = 0; i < count; ++i) list[i] = pi->playerCharacters.stuff[i];
            *n = count;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // POD copy of every character the game updates (all loaded characters, not only ours).
    bool CopyUpdateList(GameWorld* gw, Character** list, unsigned int max, unsigned int* n, unsigned int* total)
    {
        __try
        {
            *n = 0;
            *total = 0;
            const ogre_unordered_set<Character*>::type& all = gw->getCharacterUpdateList();
            for (ogre_unordered_set<Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
            {
                if (*n < max) list[(*n)++] = *it;
                ++*total;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Own characters picked by "who": "all" (default given by the caller), "selected", "first", or a
    // name (case-insensitive, exact). Returns false with *err set if nothing matched.
    bool ResolveWho(GameWorld* gw, const char* line, const char* def, Character** out, unsigned int* n, const char** err)
    {
        char who[96];
        if (!GetStr(line, "who", who, sizeof(who))) _snprintf_s(who, sizeof(who), _TRUNCATE, "%s", def);
        *n = 0;
        Character* own[KP_PROBE_MAX_CHARS];
        unsigned int ownN = 0;
        if (!CopyPlayerList(gw, own, &ownN)) { *err = "a game call faulted while reading the player list"; return false; }
        if (_stricmp(who, "selected") == 0)
        {
            Character* s = GetSelected(gw);
            if (!s) { *err = "no character selected"; return false; }
            out[(*n)++] = s;
            return true;
        }
        if (_stricmp(who, "all") == 0)
        {
            for (unsigned int i = 0; i < ownN; ++i) out[(*n)++] = own[i];
        }
        else if (_stricmp(who, "first") == 0)
        {
            if (ownN) out[(*n)++] = own[0];
        }
        else
        {
            for (unsigned int i = 0; i < ownN; ++i)
            {
                char nm[128];
                if (own[i] && SafeName(own[i], nm, sizeof(nm)) && _stricmp(nm, who) == 0) out[(*n)++] = own[i];
            }
        }
        if (*n == 0) { *err = "no own character matches \"who\" (all, selected, first or a name)"; return false; }
        return true;
    }

    void AppendChar(std::string& r, const CharSample& c, double dist, int selected)
    {
        r += "{\"name\":";
        AppendEscaped(r, c.name);
        r += ",\"faction\":";
        AppendEscaped(r, c.faction);
        r += ",\"x\":" + Num(c.x) + ",\"y\":" + Num(c.y) + ",\"z\":" + Num(c.z);
        if (dist >= 0.0) r += ",\"distance\":" + Num(dist);
        if (selected >= 0) r += Fmt(",\"selected\":%s", Bool(selected));
        r += Fmt(",\"player\":%s,\"dead\":%s,\"unconscious\":%s,\"money\":%d", Bool(c.player), Bool(c.dead),
                 Bool(c.unconscious), c.money);
        if (c.hasMedical)
            r += ",\"blood\":" + Num(c.blood) + ",\"maxBlood\":" + Num(c.maxBlood) + ",\"hunger\":" + Num(c.hunger) +
                 ",\"knockoutTimer\":" + Num(c.knockoutTimer);
        r += "}";
    }

    std::string DoStatus(GameWorld* gw, unsigned long long frames, unsigned long frameThread)
    {
        StatusSample s;
        std::string r = "{\"ok\":true,\"plugin\":\"KenshiPure\",\"build\":";
        AppendEscaped(r, g_buildName);
        r += ",\"kenshiVersion\":";
        AppendEscaped(r, g_kenshiVersion);
        r += Fmt(",\"frames\":%llu,\"frameThread\":%lu,\"writeEnabled\":%s", frames, frameThread, Bool(g_allowWrite));
        if (!SampleStatus(gw, &s)) return ErrorReply("a game call faulted while reading the status");
        r += Fmt(",\"sameWorld\":%s,\"paused\":%s,\"frameSpeed\":%s,\"timeStamp\":%s,\"inGameHours\":%s",
                 Bool(s.sameWorld), Bool(s.paused), Num(s.speed).c_str(), Num(s.stamp).c_str(), Num(s.hours).c_str());
        r += ",\"camera\":{\"x\":" + Num(s.cam[0]) + ",\"y\":" + Num(s.cam[1]) + ",\"z\":" + Num(s.cam[2]) + "}";
        r += Fmt(",\"playerInterface\":%s,\"playerCharacters\":%u", Bool(s.hasPlayer), s.players);
        Character* sel = GetSelected(gw);
        char nm[128] = "";
        r += ",\"selected\":";
        if (sel && SafeName(sel, nm, sizeof(nm))) AppendEscaped(r, nm); else r += "null";
        r += "}";
        return r;
    }

    std::string DoCharacters(GameWorld* gw)
    {
        Character* list[KP_PROBE_MAX_CHARS];
        unsigned int n = 0;
        if (!CopyPlayerList(gw, list, &n)) return ErrorReply("a game call faulted while reading the player list");
        Character* sel = GetSelected(gw);
        std::string r = "{\"ok\":true,\"characters\":[";
        for (unsigned int i = 0; i < n; ++i)
        {
            CharSample c;
            if (i) r += ",";
            if (!list[i] || !SampleChar(list[i], &c)) { r += "{\"error\":\"unreadable\"}"; continue; }
            AppendChar(r, c, -1.0, list[i] == sel ? 1 : 0);
        }
        r += "]}";
        return r;
    }

    struct Near { CharSample s; double d; };
    Near g_near[KP_PROBE_MAX_NEARBY];   // frame thread only; too big for the stack
    Character* g_all[KP_PROBE_MAX_NEARBY];

    // Characters near our first character, nearest first.
    std::string DoNearby(GameWorld* gw, const char* line)
    {
        int radius = GetInt(line, "radius", 3000);
        int limit = GetInt(line, "limit", 30);
        if (radius < 1) radius = 1;
        if (limit < 1) limit = 1;
        if (limit > 200) limit = 200;
        unsigned int n = 0, total = 0;
        if (!CopyUpdateList(gw, g_all, KP_PROBE_MAX_NEARBY, &n, &total))
            return ErrorReply("a game call faulted while reading the character list");
        Character* mine[KP_PROBE_MAX_CHARS];
        unsigned int mineN = 0;
        CharSample ref;
        if (!CopyPlayerList(gw, mine, &mineN) || mineN == 0 || !SampleChar(mine[0], &ref))
            return ErrorReply("no own character to measure from");
        unsigned int kept = 0, unreadable = 0;
        for (unsigned int i = 0; i < n; ++i)
        {
            CharSample s;
            if (!g_all[i] || !SampleChar(g_all[i], &s)) { ++unreadable; continue; }
            double dx = s.x - ref.x, dy = s.y - ref.y, dz = s.z - ref.z;
            double d = sqrt(dx * dx + dy * dy + dz * dz);
            if (d > radius) continue;
            g_near[kept].s = s;
            g_near[kept].d = d;
            ++kept;
        }
        for (unsigned int i = 1; i < kept; ++i)   // insertion sort by distance
            for (unsigned int j = i; j > 0 && g_near[j].d < g_near[j - 1].d; --j)
            {
                Near t = g_near[j]; g_near[j] = g_near[j - 1]; g_near[j - 1] = t;
            }
        std::string r = "{\"ok\":true,\"from\":";
        AppendEscaped(r, ref.name);
        r += Fmt(",\"radius\":%d,\"loaded\":%u,\"inRadius\":%u,\"unreadable\":%u,\"characters\":[", radius, total, kept,
                 unreadable);
        for (unsigned int i = 0; i < kept && (int)i < limit; ++i)
        {
            if (i) r += ",";
            AppendChar(r, g_near[i].s, g_near[i].d, -1);
        }
        r += "]}";
        return r;
    }

    // ---------------------------------------------------------------- health per body part

    struct PartSample
    {
        char name[64];
        int type, side, robotic;
        float flesh, stun, maxHealth, hpMult, percent;
    };

    __declspec(noinline) void CopyDataName(GameData* d, char* out, size_t outSize)
    {
        out[0] = 0;
        if (d) CopyStr(d->name, out, outSize);
    }

    bool SampleParts(Character* c, PartSample* parts, int max, int* n)
    {
        __try
        {
            *n = 0;
            MedicalSystem* m = c->getMedical();
            if (!m) return true;
            int count = m->getPartCount();
            for (int i = 0; i < count && *n < max; ++i)
            {
                MedicalSystem::HealthPartStatus* p = m->getPart((unsigned __int64)i);
                if (!p) continue;
                PartSample& s = parts[(*n)++];
                CopyDataName(p->data, s.name, sizeof(s.name));
                s.type = (int)p->whatAmI;
                s.side = (int)p->side;
                s.robotic = p->robotLimb ? 1 : 0;
                s.flesh = p->flesh;
                s.stun = p->fleshStun;
                s.maxHealth = p->_maxHealth;
                s.hpMult = p->HPMult;
                s.percent = p->derivedFleshHealthPercent;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void AppendParts(std::string& r, Character* c)
    {
        PartSample parts[16];
        int n = 0;
        r += "\"parts\":";
        if (!SampleParts(c, parts, 16, &n)) { r += "\"unreadable\""; return; }
        r += "[";
        for (int i = 0; i < n; ++i)
        {
            if (i) r += ",";
            r += "{\"part\":";
            AppendEscaped(r, parts[i].name);
            r += Fmt(",\"type\":%d,\"side\":%d,\"robotic\":%s", parts[i].type, parts[i].side, Bool(parts[i].robotic));
            r += ",\"flesh\":" + Num(parts[i].flesh) + ",\"stun\":" + Num(parts[i].stun) + ",\"maxHealth\":" +
                 Num(parts[i].maxHealth) + ",\"hpMult\":" + Num(parts[i].hpMult) + ",\"percent\":" + Num(parts[i].percent) + "}";
        }
        r += "]";
    }

    std::string DoHealth(GameWorld* gw, const char* line)
    {
        Character* list[KP_PROBE_MAX_CHARS];
        unsigned int n = 0;
        const char* err = "";
        if (!ResolveWho(gw, line, "all", list, &n, &err)) return ErrorReply(err);
        std::string r = "{\"ok\":true,\"characters\":[";
        for (unsigned int i = 0; i < n; ++i)
        {
            CharSample c;
            if (i) r += ",";
            if (!SampleChar(list[i], &c)) { r += "{\"error\":\"unreadable\"}"; continue; }
            r += "{\"name\":";
            AppendEscaped(r, c.name);
            r += Fmt(",\"unconscious\":%s,\"dead\":%s,", Bool(c.unconscious), Bool(c.dead));
            r += "\"blood\":" + Num(c.blood) + ",\"maxBlood\":" + Num(c.maxBlood) + ",\"hunger\":" + Num(c.hunger) + ",";
            AppendParts(r, list[i]);
            r += "}";
        }
        r += "]}";
        return r;
    }

    // ---------------------------------------------------------------- inventory

    struct ItemSample { char section[48]; char name[96]; int quantity; };
    ItemSample g_items[KP_PROBE_MAX_ITEMS];

    __declspec(noinline) void CopySectionName(InventorySection* s, char* out, size_t outSize)
    {
        CopyStr(s->name, out, outSize);
    }

    bool SampleInventory(Character* c, ItemSample* out, int max, int* n)
    {
        __try
        {
            *n = 0;
            Inventory* inv = c->getInventory();
            if (!inv) return true;
            lektor<InventorySection*>& secs = inv->getAllSections();
            for (unsigned int s = 0; s < secs.count && *n < max; ++s)
            {
                InventorySection* sec = secs.stuff[s];
                if (!sec) continue;
                char secName[48];
                CopySectionName(sec, secName, sizeof(secName));
                size_t cnt = sec->items.size();
                for (size_t i = 0; i < cnt && *n < max; ++i)
                {
                    Item* it = sec->items[i].item;
                    if (!it) continue;
                    ItemSample& o = out[(*n)++];
                    memcpy(o.section, secName, sizeof(secName));
                    CopyName(it, o.name, sizeof(o.name));
                    o.quantity = it->quantity;
                }
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::string DoInventory(GameWorld* gw, const char* line)
    {
        Character* list[KP_PROBE_MAX_CHARS];
        unsigned int n = 0;
        const char* err = "";
        if (!ResolveWho(gw, line, "first", list, &n, &err)) return ErrorReply(err);
        std::string r = "{\"ok\":true,\"characters\":[";
        for (unsigned int i = 0; i < n; ++i)
        {
            char nm[128] = "";
            SafeName(list[i], nm, sizeof(nm));
            if (i) r += ",";
            r += "{\"name\":";
            AppendEscaped(r, nm);
            int k = 0;
            if (!SampleInventory(list[i], g_items, KP_PROBE_MAX_ITEMS, &k)) { r += ",\"error\":\"unreadable\"}"; continue; }
            r += ",\"items\":[";
            for (int j = 0; j < k; ++j)
            {
                if (j) r += ",";
                r += "{\"section\":";
                AppendEscaped(r, g_items[j].section);
                r += ",\"item\":";
                AppendEscaped(r, g_items[j].name);
                r += Fmt(",\"quantity\":%d}", g_items[j].quantity);
            }
            r += "]}";
        }
        r += "]}";
        return r;
    }

    // ---------------------------------------------------------------- game data (item templates)

    // Category names accepted by find_items / spawn_item; numbers are passed through as itemType.
    int ParseCategory(const char* line, int def)
    {
        char c[32];
        if (!GetStr(line, "category", c, sizeof(c)))
        {
            double v;
            return GetNum(line, "category", &v) ? (int)v : def;
        }
        if (_stricmp(c, "item") == 0) return ITEM;
        if (_stricmp(c, "weapon") == 0) return WEAPON;
        if (_stricmp(c, "armour") == 0 || _stricmp(c, "armor") == 0) return ARMOUR;
        if (_stricmp(c, "character") == 0) return CHARACTER;
        if (_stricmp(c, "faction") == 0) return FACTION;
        if (_stricmp(c, "race") == 0) return RACE;
        return atoi(c);
    }

    GameData* g_data[8192];   // frame thread only

    __declspec(noinline) void CollectDataInner(GameWorld* gw, int type, unsigned int* n, unsigned int* total)
    {
        lektor<GameData*> l;   // its array stays allocated (Kenshi's allocator): a small leak per query
        gw->gamedata.getDataOfType(l, (itemType)type);
        *total = l.count;
        *n = 0;
        for (unsigned int i = 0; i < l.count && *n < sizeof(g_data) / sizeof(g_data[0]); ++i) g_data[(*n)++] = l.stuff[i];
    }

    bool CollectData(GameWorld* gw, int type, GameData*** arr, unsigned int* n)
    {
        __try
        {
            unsigned int total = 0;
            CollectDataInner(gw, type, n, &total);
            *arr = g_data;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SafeDataName(GameData* d, char* name, size_t nameSize, char* sid, size_t sidSize)
    {
        __try
        {
            name[0] = 0;
            sid[0] = 0;
            CopyStr(d->name, name, nameSize);
            CopyStr(d->stringID, sid, sidSize);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::string DoFindItems(GameWorld* gw, const char* line)
    {
        char query[96] = "";
        GetStr(line, "query", query, sizeof(query));
        int type = ParseCategory(line, ITEM);
        int limit = GetInt(line, "limit", 40);
        if (limit < 1) limit = 1;
        if (limit > 300) limit = 300;
        GameData** arr = NULL;
        unsigned int n = 0;
        if (!CollectData(gw, type, &arr, &n)) return ErrorReply("a game call faulted while reading the game data");
        std::string r = Fmt("{\"ok\":true,\"category\":%d,\"total\":%u,\"matches\":[", type, n);
        int found = 0;
        for (unsigned int i = 0; i < n && found < limit; ++i)
        {
            char name[128], sid[96];
            if (!arr[i] || !SafeDataName(arr[i], name, sizeof(name), sid, sizeof(sid))) continue;
            if (!ContainsNoCase(name, query)) continue;
            if (found++) r += ",";
            r += "{\"name\":";
            AppendEscaped(r, name);
            r += ",\"id\":";
            AppendEscaped(r, sid);
            r += "}";
        }
        r += "]}";
        return r;
    }

    // ---------------------------------------------------------------- writes (only with probe_write=1)

    struct HealResult { int parts; float bloodBefore, bloodAfter; };

    // stunMode: 0 keep, 1 set to the part's max, 2 set to 0.
    bool HealOne(Character* c, int stunMode, HealResult* r)
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
                p->flesh = p->_maxHealth;
                if (stunMode == 1) p->fleshStun = p->_maxHealth;
                else if (stunMode == 2) p->fleshStun = 0.0f;
                p->updateDerivedHealths();
                ++r->parts;
            }
            r->bloodBefore = m->blood;
            m->blood = m->getMaxBlood();
            m->currentBleedRate = 0.0f;
            r->bloodAfter = m->blood;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::string DoHeal(GameWorld* gw, const char* line)
    {
        char stun[16] = "keep";
        GetStr(line, "stun", stun, sizeof(stun));
        int stunMode = _stricmp(stun, "max") == 0 ? 1 : _stricmp(stun, "zero") == 0 ? 2 : 0;
        Character* list[KP_PROBE_MAX_CHARS];
        unsigned int n = 0;
        const char* err = "";
        if (!ResolveWho(gw, line, "all", list, &n, &err)) return ErrorReply(err);
        std::string r = "{\"ok\":true,\"healed\":[";
        for (unsigned int i = 0; i < n; ++i)
        {
            char nm[128] = "";
            SafeName(list[i], nm, sizeof(nm));
            HealResult h;
            if (i) r += ",";
            r += "{\"name\":";
            AppendEscaped(r, nm);
            if (!HealOne(list[i], stunMode, &h)) { r += ",\"error\":\"faulted or no medical system\"}"; continue; }
            r += Fmt(",\"parts\":%d,\"bloodBefore\":%s,\"bloodAfter\":%s}", h.parts, Num(h.bloodBefore).c_str(),
                     Num(h.bloodAfter).c_str());
        }
        r += "]}";
        kp::Log("probe: heal (%u characters, stun %s)", n, stun);
        return r;
    }

    __declspec(noinline) void CallSetPosition(Character* c, float x, float y, float z)
    {
        Ogre::Vector3 p(x, y, z);
        c->getMovement()->_NV__setPositionAndTeleport(p, 0);
    }

    bool TeleportOne(Character* c, float x, float y, float z)
    {
        __try
        {
            if (!c->getMovement()) return false;
            CallSetPosition(c, x, y, z);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // teleport: absolute x/y/z, or dx/dy/dz relative to where each character stands.
    std::string DoTeleport(GameWorld* gw, const char* line)
    {
        double x, y, z, dx = 0, dy = 0, dz = 0;
        bool absolute = GetNum(line, "x", &x) && GetNum(line, "y", &y) && GetNum(line, "z", &z);
        bool relative = GetNum(line, "dx", &dx) | GetNum(line, "dy", &dy) | GetNum(line, "dz", &dz);
        if (absolute == relative) return ErrorReply("give either x, y, z (absolute) or dx/dy/dz (relative)");
        Character* list[KP_PROBE_MAX_CHARS];
        unsigned int n = 0;
        const char* err = "";
        if (!ResolveWho(gw, line, "selected", list, &n, &err)) return ErrorReply(err);
        std::string r = "{\"ok\":true,\"teleported\":[";
        for (unsigned int i = 0; i < n; ++i)
        {
            CharSample before;
            if (i) r += ",";
            if (!SampleChar(list[i], &before)) { r += "{\"error\":\"unreadable\"}"; continue; }
            float tx = absolute ? (float)x : before.x + (float)dx;
            float ty = absolute ? (float)y : before.y + (float)dy;
            float tz = absolute ? (float)z : before.z + (float)dz;
            r += "{\"name\":";
            AppendEscaped(r, before.name);
            r += ",\"from\":[" + Num(before.x) + "," + Num(before.y) + "," + Num(before.z) + "],\"to\":[" + Num(tx) + "," +
                 Num(ty) + "," + Num(tz) + "]";
            if (!TeleportOne(list[i], tx, ty, tz)) { r += ",\"error\":\"faulted\"}"; continue; }
            r += "}";
        }
        r += "],\"note\":\"read the position again next frame to see where it landed\"}";
        kp::Log("probe: teleport (%u characters)", n);
        return r;
    }

    __declspec(noinline) GameData* FindData(GameWorld* gw, const char* key, int type)
    {
        std::string k(key);
        GameData* d = gw->gamedata.getDataByName(k, (itemType)type);
        if (!d) d = gw->gamedata.getData(k, (itemType)type);   // string ID
        return d;
    }

    __declspec(noinline) Item* MakeItem(GameWorld* gw, GameData* gd)
    {
        hand none;
        return gw->theFactory->createItem(gd, none, NULL, NULL, -1, NULL);
    }

    // 0 ok, 1 no such data, 2 factory gave nothing, 3 inventory refused (dropped), -1 faulted
    int SpawnOne(GameWorld* gw, Character* c, const char* key, int type, int qty)
    {
        __try
        {
            GameData* gd = FindData(gw, key, type);
            if (!gd) return 1;
            Item* it = MakeItem(gw, gd);
            if (!it) return 2;
            Inventory* inv = c->getInventory();
            if (!inv) return 3;
            return inv->_NV_addItem(it, qty, true, false) ? 0 : 3;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    std::string DoSpawnItem(GameWorld* gw, const char* line)
    {
        char key[128];
        if (!GetStr(line, "name", key, sizeof(key))) return ErrorReply("give \"name\" (exact item name or string ID; see find_items)");
        int type = ParseCategory(line, ITEM);
        int qty = GetInt(line, "quantity", 1);
        if (qty < 1) qty = 1;
        if (qty > 100) qty = 100;
        Character* list[KP_PROBE_MAX_CHARS];
        unsigned int n = 0;
        const char* err = "";
        if (!ResolveWho(gw, line, "selected", list, &n, &err)) return ErrorReply(err);
        int res = SpawnOne(gw, list[0], key, type, qty);
        kp::Log("probe: spawn_item \"%s\" x%d category %d -> %d", key, qty, type, res);
        switch (res)
        {
        case 0: return Fmt("{\"ok\":true,\"spawned\":%d,\"category\":%d}", qty, type);
        case 1: return ErrorReply("no item with that name or ID in this category (try find_items)");
        case 2: return ErrorReply("the game's item factory returned nothing");
        case 3: return ErrorReply("the inventory refused the item (full?); it may lie on the ground");
        default: return ErrorReply("a game call faulted while spawning");
        }
    }

    // ---------------------------------------------------------------- dispatch

    bool IsWrite(const char* cmd)
    {
        return strcmp(cmd, "heal") == 0 || strcmp(cmd, "teleport") == 0 || strcmp(cmd, "spawn_item") == 0;
    }

    bool IsGameCmd(const char* cmd)
    {
        return strcmp(cmd, "status") == 0 || strcmp(cmd, "characters") == 0 || strcmp(cmd, "nearby") == 0 ||
               strcmp(cmd, "health") == 0 || strcmp(cmd, "inventory") == 0 || strcmp(cmd, "find_items") == 0 ||
               IsWrite(cmd);
    }

    std::string RunGameCmd(GameWorld* gw, const char* line, unsigned long long frames, unsigned long frameThread)
    {
        char cmd[32] = "";
        GetStr(line, "cmd", cmd, sizeof(cmd));
        if (strcmp(cmd, "status") == 0) return DoStatus(gw, frames, frameThread);
        if (strcmp(cmd, "characters") == 0) return DoCharacters(gw);
        if (strcmp(cmd, "nearby") == 0) return DoNearby(gw, line);
        if (strcmp(cmd, "health") == 0) return DoHealth(gw, line);
        if (strcmp(cmd, "inventory") == 0) return DoInventory(gw, line);
        if (strcmp(cmd, "find_items") == 0) return DoFindItems(gw, line);
        if (strcmp(cmd, "heal") == 0) return DoHeal(gw, line);
        if (strcmp(cmd, "teleport") == 0) return DoTeleport(gw, line);
        if (strcmp(cmd, "spawn_item") == 0) return DoSpawnItem(gw, line);
        return ErrorReply("unknown game command");
    }

    // ---------------------------------------------------------------- server thread

    // Queues a game command and waits for the frame hook. Returns the reply line.
    std::string RunOnGameThread(const char* line)
    {
        EnterCriticalSection(&g_slotLock);
        _snprintf_s(g_slotLine, sizeof(g_slotLine), _TRUNCATE, "%s", line);
        g_slotResult.clear();
        g_slotState = SLOT_PENDING;
        LeaveCriticalSection(&g_slotLock);

        ULONGLONG start = GetTickCount64();
        for (;;)
        {
            Sleep(5);
            EnterCriticalSection(&g_slotLock);
            if (g_slotState == SLOT_DONE)
            {
                std::string r = g_slotResult;
                g_slotState = SLOT_IDLE;
                LeaveCriticalSection(&g_slotLock);
                return r;
            }
            if (GetTickCount64() - start > KP_PROBE_WAIT_MS)
            {
                g_slotState = SLOT_IDLE;   // cancel; the hook only answers PENDING under this lock
                LeaveCriticalSection(&g_slotLock);
                return ErrorReply("no game frame within 3 seconds (menu or loading screen?)");
            }
            LeaveCriticalSection(&g_slotLock);
        }
    }

    std::string Handle(const char* line)
    {
        char cmd[32];
        if (line[0] != '{' || !GetStr(line, "cmd", cmd, sizeof(cmd)))
            return ErrorReply("expected a JSON line like {\"cmd\":\"status\"}");
        if (strcmp(cmd, "ping") == 0) return "{\"ok\":true,\"pong\":true}";
        if (strcmp(cmd, "log_tail") == 0)
        {
            int n = GetInt(line, "lines", 50);
            if (n < 1) n = 1;
            if (n > 500) n = 500;
            std::string tail = kp::LogTail(n);
            std::string r = "{\"ok\":true,\"text\":";
            AppendEscaped(r, tail.c_str());
            r += "}";
            return r;
        }
        if (IsWrite(cmd) && !g_allowWrite)
            return ErrorReply("write commands are off; set probe_write=1 in mods\\KenshiPure\\KenshiPure.cfg and restart the game");
        if (IsGameCmd(cmd)) return RunOnGameThread(line);
        return ErrorReply("unknown command (status, characters, nearby, health, inventory, find_items, log_tail, ping; "
                          "with probe_write=1 also heal, teleport, spawn_item)");
    }

    void ServeClient(SOCKET c)
    {
        DWORD to = 10000;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
        setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, (const char*)&to, sizeof(to));
        static char buf[KP_PROBE_MAX_LINE + 1];   // server thread only
        size_t len = 0;
        for (;;)
        {
            int got = recv(c, buf + len, (int)(KP_PROBE_MAX_LINE - len), 0);
            if (got <= 0) return;
            len += got;
            for (;;)
            {
                char* nl = (char*)memchr(buf, '\n', len);
                if (!nl)
                {
                    if (len >= KP_PROBE_MAX_LINE) return;   // line too long: drop the client
                    break;
                }
                *nl = 0;
                if (nl > buf && nl[-1] == '\r') nl[-1] = 0;
                std::string reply = Handle(buf) + "\n";
                size_t rest = len - (size_t)(nl + 1 - buf);
                memmove(buf, nl + 1, rest);
                len = rest;
                const char* p = reply.c_str();
                int left = (int)reply.size();
                while (left > 0)
                {
                    int sent = send(c, p, left, 0);
                    if (sent <= 0) return;
                    p += sent; left -= sent;
                }
            }
        }
    }

    unsigned __stdcall ServerThread(void*)
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { kp::Log("probe: WSAStartup failed"); return 0; }
        SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // 127.0.0.1 only, never 0.0.0.0
        addr.sin_port = htons((u_short)g_port);
        if (ls == INVALID_SOCKET || bind(ls, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(ls, 2) != 0)
        {
            kp::Log("probe: cannot listen on 127.0.0.1:%d (error %d) - probe off", g_port, WSAGetLastError());
            return 0;
        }
        kp::Log("probe: listening on 127.0.0.1:%d (%s)", g_port, g_allowWrite ? "read + write commands" : "read-only");
        for (;;)
        {
            SOCKET c = accept(ls, NULL, NULL);
            if (c == INVALID_SOCKET) { Sleep(100); continue; }
            ServeClient(c);
            closesocket(c);
        }
    }

    void ReadCfg()
    {
        wchar_t path[MAX_PATH];
        _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%sKenshiPure.cfg", kp::PluginDir());
        FILE* f = NULL;
        if (_wfopen_s(&f, path, L"r") != 0 || !f) return;
        char line[256];
        while (fgets(line, sizeof(line), f))
        {
            const char* eq = strchr(line, '=');
            if (!eq) continue;
            if (strncmp(line, "probe_port", 10) == 0) g_port = atoi(eq + 1);
            else if (strncmp(line, "probe_write", 11) == 0) g_allowWrite = atoi(eq + 1) != 0;
        }
        fclose(f);
    }
}

namespace kp
{
    void ProbeSetBuild(const char* buildName, const char* kenshiVersion)
    {
        _snprintf_s(g_buildName, sizeof(g_buildName), _TRUNCATE, "%s", buildName);
        _snprintf_s(g_kenshiVersion, sizeof(g_kenshiVersion), _TRUNCATE, "%s", kenshiVersion);
    }

    void ProbeStart()
    {
        ReadCfg();
        if (g_port <= 0 || g_port > 65535) { Log("probe: off (probe_port=%d)", g_port); return; }
        InitializeCriticalSection(&g_slotLock);
        uintptr_t h = _beginthreadex(NULL, 0, ServerThread, NULL, 0, NULL);
        if (h) CloseHandle((HANDLE)h);
        else Log("probe: could not start the server thread");
    }

    void ProbeOnFrame(GameWorld* gw, unsigned long long frames, unsigned long frameThread)
    {
        if (g_slotState != SLOT_PENDING) return;   // cheap check each frame
        EnterCriticalSection(&g_slotLock);
        if (g_slotState == SLOT_PENDING)
        {
            g_slotResult = RunGameCmd(gw, g_slotLine, frames, frameThread);
            g_slotState = SLOT_DONE;
        }
        LeaveCriticalSection(&g_slotLock);
    }
}
