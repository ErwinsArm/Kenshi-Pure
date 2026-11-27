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
          epoch_(0), epochStart_(0), firstOfEpochDropped_(false), settled_(false),
          localOk_(false), localEpoch_(0), localPaused_(false), trackId_(0), siOn_(false), siEpoch_(0),
          seenEpoch_(0), epochSince_(0), nextPick_(0), lastStatus_(0), lastNoPickLog_(0), phaseStart_(0), phase_(0), phaseAiSkip_(false), waking_(false),
          applyFails_(0), lastGap_(0.0f)
    {
        memset(&stats_, 0, sizeof(stats_));
        memset(players_, 0, sizeof(players_));
        memset(localPos_, 0, sizeof(localPos_));
        memset(&si_, 0, sizeof(si_));
        memset(&lastBody_, 0, sizeof(lastBody_));
        memset(&lastCmd_, 0, sizeof(lastCmd_));
        DriveParseConfig("", &drive_);   // off
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
        // Game exit (or a restart): forget the stand-in without calling into a world that may be going away.
        if (siOn_) Release(0, true, "networking stops");
        track_.Clear();
        trackId_ = 0;
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
        if (!game_->LocalPose(&s))
        {
            localOk_ = false;
            return;                            // menu or loading: the server keeps our last pose (its age grows)
        }
        localOk_ = true;
        for (int i = 0; i < 3; ++i) localPos_[i] = s.pos[i];
        localEpoch_ = s.worldEpoch;   // the stand-in is released on a world change, not on a click on another own character
        localPaused_ = s.paused != 0;
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
                    ForgetTrack(0);
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
                    ForgetTrack((unsigned short)e.a);
                    break;
                }
                case KP_EV_LOST:
                case KP_EV_CONNECT_FAILED:
                    memset(players_, 0, sizeof(players_));   // the core logs why
                    ForgetTrack(0);
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
                if (drive_.mode != DM_OFF)
                {
                    if (trackId_ == 0) trackId_ = rp[i].playerId;   // the first remote player drives the stand-in
                    if (rp[i].playerId == trackId_) track_.Add(nowMs, rp[i]);
                }
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
        DriveFrame(nowMs);
    }

    // ------------------------------------------------------------------ stand-in (M4)

    namespace
    {
        // The cycle: place (proves coordinates); path = CharMovement::setDestination with the AI skipped
        // (candidate A, movement half); order = Character::setDestination with the AI on (candidate A, order
        // half: its TASK_MOVE job is ticked by the AI, so skipping the AI would stall it); steer =
        // setDirectMovement with the AI skipped (candidate B).
        struct Phase { int mode; bool aiSkip; unsigned int ms; };
        const Phase kCycle[] = {
            { DM_PLACE, true, 20000 }, { DM_PATH, true, 45000 }, { DM_ORDER, false, 45000 }, { DM_STEER, true, 45000 },
        };
        const int kCycleCount = (int)(sizeof(kCycle) / sizeof(kCycle[0]));

        const char* CmdName(int k)
        {
            switch (k)
            {
            case DC_PLACE: return "place";
            case DC_SNAP: return "snap";
            case DC_WALK: return "walk";
            case DC_STEER: return "steer";
            case DC_HALT: return "halt";
            default: return "-";
            }
        }
    }

    void NetFrame::ForgetTrack(unsigned short playerId)
    {
        if (playerId != 0 && playerId != trackId_) return;
        track_.Clear();
        trackId_ = 0;   // DriveFrame releases the stand-in when its track is empty
    }

    void NetFrame::StartPhase(unsigned long long nowMs, int phase)
    {
        phase_ = phase;
        phaseStart_ = nowMs;
        int mode = drive_.mode == DM_CYCLE ? kCycle[phase].mode : drive_.mode;
        phaseAiSkip_ = drive_.aiSkip != DA_NONE && (drive_.mode != DM_CYCLE || kCycle[phase].aiSkip);
        planner_.Reset(mode, drive_.snapDist);
        game_->StandInSkipAi(phaseAiSkip_);
        Logf("standin: phase %s, own AI %s", DriveModeName(mode), phaseAiSkip_ ? "skipped" : "on");
    }

    void NetFrame::LogPhaseSummary(const char* why)
    {
        const DriveStats& s = planner_.Stats();
        if (s.samples == 0) return;
        Logf("standin: phase %s (AI %s) %s: %u frames, gap mean %.1f max %.1f, body moving %.0f%%, snaps %u, walk orders %u",
             DriveModeName(planner_.Mode()), phaseAiSkip_ ? "skipped" : "on", why, s.samples, s.gapSum / (float)s.samples,
             s.gapMax, 100.0f * (float)s.moving / (float)s.samples, s.snaps, s.orders);   // kp-not-game: DriveStats
    }

    void NetFrame::Release(unsigned long long nowMs, bool worldGone, const char* why)
    {
        if (!siOn_) return;
        LogPhaseSummary("ended");
        siOn_ = false;
        game_->StandInRelease(worldGone);
        Logf("standin: released %s (%s)%s", si_.name, why, worldGone ? "" : ", put back where it stood");
        nextPick_ = nowMs + kPickEveryMs;
    }

    void NetFrame::DriveFrame(unsigned long long nowMs)
    {
        if (drive_.mode == DM_OFF || !game_) return;
        if (localOk_ && (epochSince_ == 0 || localEpoch_ != seenEpoch_))
        {
            seenEpoch_ = localEpoch_;
            epochSince_ = nowMs;
        }
        bool fresh = !track_.Empty() && nowMs - track_.LastAddMs() <= (unsigned long long)kRemoteStaleMs;
        if (siOn_)
        {
            // World unload/load or another own character: the old body may be gone; never touch it.
            if (!localOk_) { Release(nowMs, true, "no own character: menu or loading"); return; }
            if (localEpoch_ != siEpoch_) { Release(nowMs, true, "the world changed (load)"); return; }
            if (!fresh) { Release(nowMs, false, "no fresh remote pose for 3 s"); return; }
            BodyState b;
            memset(&b, 0, sizeof(b));
            if (!game_->StandInRead(&b)) { Release(nowMs, true, "its handle no longer resolves"); return; }
            lastBody_ = b;
            // Wake-up: a picked NPC may be busy (sitting, working: it stood 33 units above the ground) and
            // ignored teleports and walk orders while its AI was skipped, also 60 s after the load; with
            // the AI on, an order moved it at once (M4 runs, M). So: AI on and orders until it moves.
            if (waking_ && (b.moving || nowMs - phaseStart_ >= (unsigned long long)kWakeMaxMs))
            {
                waking_ = false;
                Logf("standin: %s after %.1f s; the drive starts", b.moving ? "it moves" : "it did not move", (float)(nowMs - phaseStart_) / 1000.0f);
                StartPhase(nowMs, 0);
            }
            else if (!waking_ && drive_.mode == DM_CYCLE && nowMs - phaseStart_ >= kCycle[phase_].ms)
            {
                LogPhaseSummary("done");
                StartPhase(nowMs, (phase_ + 1) % kCycleCount);
            }
            if (localPaused_) return;   // the game drops teleports while paused (probe); nothing moves anyway
            TrackSample target;
            if (!track_.At(nowMs - DrivePlanner::kRenderDelayMs, &target)) return;
            DriveCmd c = planner_.Step(nowMs, b, target, &lastGap_);
            if (c.kind != DC_NONE)
            {
                lastCmd_ = c;
                if (game_->StandInApply(c, planner_.Mode())) applyFails_ = 0;
                else if (++applyFails_ >= kMaxApplyFails)
                {
                    Release(nowMs, false, "game calls faulted 3 times in a row");
                    return;
                }
            }
            if (nowMs - lastStatus_ >= (unsigned long long)kStatusMs)
            {
                lastStatus_ = nowMs;
                Logf("standin: %s %s body %.1f %.1f %.1f target %.1f %.1f %.1f gap %.1f speed %.1f (asked %.1f, desired %.1f, "
                     "max %.1f, walk %.1f, orders %d) moving %d last %s",
                     DriveModeName(planner_.Mode()), si_.name, b.pos[0], b.pos[1], b.pos[2], target.pos[0], target.pos[1],
                     target.pos[2], lastGap_, b.speed, lastCmd_.speed, b.desired, b.maxSpeed, b.walkSpeed, b.speedOrders, b.moving,
                     CmdName(lastCmd_.kind));
            }
            return;
        }
        if (!localOk_ || !fresh || nowMs < nextPick_) return;
        if (nowMs - epochSince_ < (unsigned long long)drive_.settleMs) return;   // the world is still settling after a load
        nextPick_ = nowMs + kPickEveryMs;
        StandInInfo info;
        memset(&info, 0, sizeof(info));
        if (!game_->StandInPick(localPos_, drive_, &info))
        {
            if (lastNoPickLog_ == 0 || nowMs - lastNoPickLog_ >= 10000)
            {
                lastNoPickLog_ = nowMs;
                Logf("standin: no NPC to drive within %.0f units%s%s", drive_.pickDist, drive_.name[0] ? " named " : "",
                     drive_.name);
            }
            return;
        }
        si_ = info;
        siOn_ = true;
        siEpoch_ = localEpoch_;
        applyFails_ = 0;
        memset(&lastCmd_, 0, sizeof(lastCmd_));
        Logf("standin: driving %s (%.0f units away, at %.1f %.1f %.1f) from remote player %u, mode %s", si_.name,
             si_.distance, si_.home[0], si_.home[1], si_.home[2], trackId_, DriveModeName(drive_.mode));
        // Wake it up first (see DriveFrame): the order mode with its own AI on.
        waking_ = true;
        phase_ = 0;
        phaseStart_ = nowMs;
        phaseAiSkip_ = false;
        planner_.Reset(DM_ORDER, drive_.snapDist);
        game_->StandInSkipAi(false);
        Logf("standin: waking it up (own AI on, walk orders until it moves)");
    }
}
