// See NetFrame.h. VC++ 2010 and MSVC 14.44 both compile this file: keep to C++03 plus what both accept.
#include "NetFrame.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    const char* LeaveText(unsigned int r)
    {
        switch (r)
        {
        case 0: return "left";
        case 1: return "timed out";
        case 2: return "removed by the server";
        default: return "unknown reason";
        }
    }

    bool Finite(const float* v, int n)
    {
        for (int i = 0; i < n; ++i)
            if (!(v[i] == v[i]) || v[i] > 3.0e38f || v[i] < -3.0e38f) return false;
        return true;
    }

    // Copies [b, e) without surrounding blanks into out (cut to outSize - 1).
    void CopyTrimmed(const char* b, const char* e, char* out, size_t outSize)
    {
        while (b < e && (*b == ' ' || *b == '\t')) ++b;
        while (e > b && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) --e;
        size_t n = (size_t)(e - b);
        if (n >= outSize) n = outSize - 1;
        memcpy(out, b, n);
        out[n] = 0;
    }
}

namespace kp
{
    bool NetParseConfig(const char* text, KpCoreConfig* cfg)
    {
        memset(cfg, 0, sizeof(*cfg));
        cfg->size = sizeof(*cfg);
        cfg->version = KP_CORE_VERSION;
        cfg->port = 27850;
        strcpy_s(cfg->name, sizeof(cfg->name), "player");
        bool haveHost = false;
        const char* p = text ? text : "";
        while (*p)
        {
            const char* end = strchr(p, '\n');
            if (!end) end = p + strlen(p);
            const char* eq = (const char*)memchr(p, '=', (size_t)(end - p));
            char key[32], val[KP_CORE_HOST_MAX];
            if (eq && *p != '#')
            {
                CopyTrimmed(p, eq, key, sizeof(key));
                CopyTrimmed(eq + 1, end, val, sizeof(val));
                if (strcmp(key, "host") == 0)
                {
                    strcpy_s(cfg->host, sizeof(cfg->host), val);
                    haveHost = val[0] != 0;
                }
                else if (strcmp(key, "port") == 0)
                {
                    long v = strtol(val, NULL, 10);
                    cfg->port = (v >= 1 && v <= 65535) ? (unsigned short)v : 0;   // 0: the core refuses it
                }
                else if (strcmp(key, "name") == 0)
                {
                    // Longer than the protocol allows: leave it empty so the core refuses it and says why.
                    if (strlen(val) >= sizeof(cfg->name)) cfg->name[0] = 0;
                    else strcpy_s(cfg->name, sizeof(cfg->name), val);
                }
            }
            p = *end ? end + 1 : end;
        }
        return haveHost;
    }

    NetFrame::NetFrame()
        : api_(0), core_(0), game_(0), lastTick_(0), haveEpoch_(false),
          epoch_(0), epochStart_(0), firstOfEpochDropped_(false), settled_(false)
    {
        memset(&stats_, 0, sizeof(stats_));
        memset(players_, 0, sizeof(players_));
    }

    void NetFrame::Logf(const char* fmt, ...)
    {
        if (!game_) return;
        char line[256];
        va_list a;
        va_start(a, fmt);
        _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, a);
        va_end(a);
        game_->Log(line);
    }

    bool NetFrame::Start(const KpCoreApi* api, const KpCoreConfig& cfg, IGameAccess* game)
    {
        Stop();
        api_ = api;
        game_ = game;
        if (!api_ || !game_) return false;
        core_ = api_->start(&cfg);
        if (!core_) return false;
        return true;
    }

    void NetFrame::Stop()
    {
        KpCore* c = core_;
        core_ = 0;   // nothing may use the core while (or after) it stops
        if (c && api_) api_->stop(c);
        memset(players_, 0, sizeof(players_));
    }

    NetFrame::Player* NetFrame::Find(unsigned short id)
    {
        for (int i = 0; i < kMaxPlayers; ++i)
            if (players_[i].id == id) return &players_[i];
        return 0;
    }

    const NetFrame::Player* NetFrame::Find(unsigned short id) const
    {
        for (int i = 0; i < kMaxPlayers; ++i)
            if (players_[i].id == id) return &players_[i];
        return 0;
    }

    NetFrame::Player* NetFrame::Add(unsigned short id, const char* name)
    {
        Player* p = Find(id);
        if (!p) p = Find(0);
        if (!p) return 0;
        memset(p, 0, sizeof(*p));
        p->id = id;
        strcpy_s(p->name, sizeof(p->name), name && name[0] ? name : "?");
        return p;
    }

    bool NetFrame::Remote(unsigned short playerId, KpRemotePose* out) const
    {
        const Player* p = playerId ? Find(playerId) : 0;
        if (!p || !p->havePose) return false;
        *out = p->last;
        return true;
    }

    void NetFrame::SendLocal(unsigned long long nowMs)
    {
        // Checked every frame; the game adapter samples at 10 Hz, so each new sample goes out once.
        NetLocalSample s;
        memset(&s, 0, sizeof(s));
        if (!game_->LocalPose(&s)) return;     // menu or loading: the server keeps our last pose (its age grows)
        if (s.tick == lastTick_) return;       // nothing new since the last send
        lastTick_ = s.tick;
        if (!haveEpoch_ || s.epoch != epoch_)
        {
            haveEpoch_ = true;
            epoch_ = s.epoch;
            epochStart_ = nowMs;
            firstOfEpochDropped_ = true;
            settled_ = false;
            ++stats_.posesUnsettled;           // the first sample of an epoch never goes out
            return;
        }
        if (!settled_)
        {
            bool destSet = s.dest[0] != 0.0f || s.dest[1] != 0.0f || s.dest[2] != 0.0f;
            if (!destSet && nowMs - epochStart_ < (unsigned long long)kSettleMaxMs)
            {
                ++stats_.posesUnsettled;
                return;
            }
            settled_ = true;
            Logf("net: sending own pose (%u sample%s after the load dropped%s)", stats_.posesUnsettled,
                 stats_.posesUnsettled == 1 ? "" : "s", destSet ? "" : ", destination still 0/0/0 after 2 s");
        }
        if (!Finite(s.pos, 3) || !Finite(s.vel, 3) || !Finite(&s.yawDeg, 1))
        {
            ++stats_.posesInvalid;   // e.g. no heading (yaw NaN): the protocol refuses non-finite values
            return;
        }
        KpPose p;
        memset(&p, 0, sizeof(p));
        for (int i = 0; i < 3; ++i) { p.pos[i] = s.pos[i]; p.vel[i] = s.vel[i]; }
        p.yaw = s.yawDeg;
        p.flags = (s.moving ? KP_POSE_MOVING : 0) | (s.paused ? KP_POSE_PAUSED : 0);
        if (api_->setLocalPose(core_, &p)) ++stats_.posesSent;
        else ++stats_.posesInvalid;   // out of the protocol's range
    }

    void NetFrame::Drain(unsigned long long nowMs)
    {
        KpEvent ev[32];
        int n;
        while ((n = api_->takeEvents(core_, ev, 32)) > 0)
        {
            for (int i = 0; i < n; ++i)
            {
                const KpEvent& e = ev[i];
                ++stats_.events;
                switch (e.kind)
                {
                case KP_EV_WELCOME:
                    memset(players_, 0, sizeof(players_));   // a new session: nobody is known yet
                    break;
                case KP_EV_JOINED:
                    Add((unsigned short)e.a, e.name);
                    Logf("net: player %u joined: %s", e.a, e.name);
                    break;
                case KP_EV_LEFT:
                {
                    Player* p = Find((unsigned short)e.a);
                    Logf("net: player %u (%s) left (%s)", e.a, p ? p->name : "?", LeaveText(e.b));
                    if (p) memset(p, 0, sizeof(*p));
                    break;
                }
                case KP_EV_LOST:
                case KP_EV_CONNECT_FAILED:
                    memset(players_, 0, sizeof(players_));   // the core logs why
                    break;
                default:
                    break;
                }
            }
            if (n < 32) break;
        }

        KpRemotePose rp[32];
        while ((n = api_->takeRemotePoses(core_, rp, 32)) > 0)
        {
            for (int i = 0; i < n; ++i)
            {
                ++stats_.remoteUpdates;
                Player* p = Find(rp[i].playerId);
                if (!p) p = Add(rp[i].playerId, "?");   // the core only reports announced players
                if (!p) continue;
                p->last = rp[i];
                p->havePose = true;
                if (!p->logged || nowMs - p->lastLog >= (unsigned long long)kRemoteLogMs)
                {
                    p->logged = true;
                    p->lastLog = nowMs;
                    const KpPose& q = rp[i].pose;
                    Logf("net: remote %s (%u) pos %.1f %.1f %.1f vel %.1f %.1f %.1f yaw %.1f moving %d%s age %u ms",
                         p->name, rp[i].playerId, q.pos[0], q.pos[1], q.pos[2], q.vel[0], q.vel[1], q.vel[2], q.yaw,
                         (q.flags & KP_POSE_MOVING) ? 1 : 0, (q.flags & KP_POSE_PAUSED) ? " paused" : "", rp[i].ageMs);
                }
            }
            if (n < 32) break;
        }
    }

    void NetFrame::OnFrame(unsigned long long nowMs)
    {
        if (!core_) return;
        SendLocal(nowMs);
        Drain(nowMs);
    }
}
