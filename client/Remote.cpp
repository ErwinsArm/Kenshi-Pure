// See Remote.h. VC++ 2010 and MSVC 14.44 both compile this file: keep to C++03 plus what both accept.
#include "Remote.h"
#include "NetFrame.h"   // IGameAccess

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    void Trim(const char* b, const char* e, char* out, size_t outSize)
    {
        while (b < e && (*b == ' ' || *b == '\t')) ++b;
        while (e > b && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) --e;
        size_t n = (size_t)(e - b);
        if (n >= outSize) n = outSize - 1;
        memcpy(out, b, n);
        out[n] = 0;
    }

    float LenXZ(const float v[3]) { return sqrtf(v[0] * v[0] + v[2] * v[2]); }
}

namespace kp
{
    void RemoteParseConfig(const char* text, RemoteConfig* cfg)
    {
        memset(cfg, 0, sizeof(*cfg));
        cfg->on = 1;
        cfg->drive = DM_ORDER;
        strcpy_s(cfg->faction, sizeof(cfg->faction), "nofac");
        cfg->maxSpeed = 150.0f;
        cfg->range = 3000.0f;
        cfg->settleMs = 3000;
        cfg->squadPermanent = 1;
        cfg->snapDist = 300.0f;
        cfg->saveGuard = 1;
        const char* p = text ? text : "";
        while (*p)
        {
            const char* end = strchr(p, '\n');
            if (!end) end = p + strlen(p);
            const char* eq = (const char*)memchr(p, '=', (size_t)(end - p));
            char key[32], val[64];
            if (eq && *p != '#')
            {
                Trim(p, eq, key, sizeof(key));
                Trim(eq + 1, end, val, sizeof(val));
                if (strcmp(key, "remote") == 0)
                {
                    if (strcmp(val, "off") == 0) cfg->on = 0;
                    else if (strcmp(val, "on") == 0) cfg->on = 1;
                }
                else if (strcmp(key, "remote_drive") == 0)
                {
                    if (strcmp(val, "order") == 0) cfg->drive = DM_ORDER;
                    else if (strcmp(val, "steer") == 0) cfg->drive = DM_STEER;
                }
                else if (strcmp(key, "remote_faction") == 0)
                {
                    if (val[0]) strcpy_s(cfg->faction, sizeof(cfg->faction), val);
                }
                else if (strcmp(key, "remote_maxspeed") == 0)
                {
                    float v = (float)atof(val);
                    if (v == 0.0f || (v >= 10.0f && v <= 1000.0f)) cfg->maxSpeed = v;
                }
                else if (strcmp(key, "remote_range") == 0)
                {
                    float v = (float)atof(val);
                    if (v >= 100.0f && v <= 100000.0f) cfg->range = v;
                }
                else if (strcmp(key, "remote_settle") == 0)
                {
                    long v = strtol(val, NULL, 10);
                    if (v >= 0 && v <= 600) cfg->settleMs = (unsigned int)v * 1000;
                }
                else if (strcmp(key, "remote_squad_permanent") == 0)
                {
                    if (strcmp(val, "0") == 0) cfg->squadPermanent = 0;
                    else if (strcmp(val, "1") == 0) cfg->squadPermanent = 1;
                }
                else if (strcmp(key, "remote_save_guard") == 0)
                {
                    if (strcmp(val, "0") == 0 || strcmp(val, "off") == 0) cfg->saveGuard = 0;
                    else if (strcmp(val, "1") == 0 || strcmp(val, "on") == 0) cfg->saveGuard = 1;
                }
                else if (strcmp(key, "remote_snap") == 0)
                {
                    float v = (float)atof(val);
                    if (v >= 10.0f && v <= 100000.0f) cfg->snapDist = v;
                }
            }
            p = *end ? end + 1 : end;
        }
    }

    RemoteBodies::RemoteBodies()
        : game_(0), worldLive_(false), haveEpoch_(false), epoch_(0), worldSince_(0), paused_(false), now_(0),
          spawns_(0), removes_(0), forgets_(0), kept_(0)
    {
        RemoteParseConfig("", &cfg_);
        cfg_.on = 0;
        for (int i = 0; i < kMaxPlayers; ++i) Free(&bodies_[i]);
        memset(localPos_, 0, sizeof(localPos_));
    }

    void RemoteBodies::Logf(const char* fmt, ...)
    {
        if (!game_) return;
        char line[320];
        va_list a;
        va_start(a, fmt);
        _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, a);
        va_end(a);
        game_->Log(line);
    }

    void RemoteBodies::Free(Body* b)
    {
        b->id = 0;
        b->name[0] = 0;
        b->state = RB_FREE;
        b->left = false;
        b->slot = -1;
        b->track.Clear();
        b->planner.Reset(DM_OFF, 300.0f);
        b->stateAt = b->lastRecv = b->nextTry = b->lastStatus = 0;
        b->lastAge = 0;
        b->spawnFails = b->applyFails = 0;
        b->aiSkipped = false;
        b->verify = false;
        b->destroyTries = 0;
        b->downSince = 0;
        memset(&b->body, 0, sizeof(b->body));
        memset(&b->lastCmd, 0, sizeof(b->lastCmd));
        b->lastGap = b->lastLag = 0.0f;
        memset(&b->stats, 0, sizeof(b->stats));
    }

    RemoteBodies::Body* RemoteBodies::FindMutable(unsigned short id)
    {
        if (id == 0) return 0;
        for (int i = 0; i < kMaxPlayers; ++i)
            if (bodies_[i].id == id) return &bodies_[i];
        return 0;
    }

    const RemoteBodies::Body* RemoteBodies::Find(unsigned short id) const
    {
        if (id == 0) return 0;
        for (int i = 0; i < kMaxPlayers; ++i)
            if (bodies_[i].id == id) return &bodies_[i];
        return 0;
    }

    int RemoteBodies::CountState(int state) const
    {
        int n = 0;
        for (int i = 0; i < kMaxPlayers; ++i)
            if (bodies_[i].id != 0 && bodies_[i].state == state) ++n;
        return n;
    }

    RemoteBodies::Body* RemoteBodies::Add(unsigned short id, const char* name)
    {
        Body* b = FindMutable(id);
        if (b)
        {
            if (name && name[0] && strcmp(name, "?") != 0) strcpy_s(b->name, sizeof(b->name), name);
            return b;
        }
        for (int i = 0; i < kMaxPlayers && !b; ++i)
            if (bodies_[i].id == 0) b = &bodies_[i];
        if (!b) return 0;
        Free(b);
        b->id = id;
        strcpy_s(b->name, sizeof(b->name), name && name[0] ? name : "?");
        b->state = RB_WAIT;
        return b;
    }

    void RemoteBodies::LogSummary(const Body* b, const char* why)
    {
        const RemoteBodyStats& s = b->stats;
        if (s.samples == 0)
        {
            Logf("remote: body of %s (%u) removed (%s), never driven", b->name, b->id, why);
            return;
        }
        float n = (float)s.samples;   // kp-not-game: RemoteBodyStats
        Logf("remote: body of %s (%u) removed (%s): %u frames, gap to target mean %.1f max %.1f, lag to newest mean %.1f "
             "max %.1f, body moving %.0f%%, speed while the remote moves %.1f, snaps %u, orders %u, down %u frames",
             b->name, b->id, why, s.samples, s.gapSum / n, s.gapMax, s.lagSum / n, s.lagMax, 100.0f * (float)s.moving / n,
             s.speedSamples ? s.speedSum / (float)s.speedSamples : 0.0f, s.snaps, s.orders, s.down);
    }

    void RemoteBodies::Remove(Body* b, unsigned long long nowMs, const char* why)
    {
        if (b->state != RB_LIVE) return;
        LogSummary(b, why);
        ++removes_;
        if (b->aiSkipped) game_->RemoteSkipAi(b->slot, false);
        b->aiSkipped = false;
        bool asked = game_->RemoteDestroy(b->slot, "KenshiPure: remote player gone");
        if (!asked)
        {
            // Faulted or the hand no longer resolves: nothing more to wait for.
            Logf("remote: destroying the body of %s (%u) failed - forgetting it", b->name, b->id);
            game_->RemoteFree(b->slot);
            b->slot = -1;
            b->state = b->left ? RB_FREE : RB_WAIT;
            b->nextTry = nowMs + kRetryMs;
            if (b->left) Free(b);
            return;
        }
        b->state = RB_REMOVING;
        b->stateAt = nowMs;
        b->destroyTries = 1;
    }

    void RemoteBodies::ForgetAll(const char* why)
    {
        int n = 0;
        for (int i = 0; i < kMaxPlayers; ++i)
        {
            Body* b = &bodies_[i];
            if (b->id == 0) continue;
            if (b->state == RB_LIVE || b->state == RB_REMOVING)
            {
                if (b->state == RB_LIVE) LogSummary(b, why);
                ++n;
                b->slot = -1;
                b->aiSkipped = false;
                b->state = RB_WAIT;
                b->nextTry = 0;
                b->applyFails = 0;
                b->track.Clear();   // respawn from the next snapshot
            }
            if (b->left) Free(b);
        }
        if (game_) game_->RemoteForgetAll();
        if (n > 0)
        {
            forgets_ += (unsigned int)n;
            Logf("remote: forgot %d bod%s without touching the game (%s)", n, n == 1 ? "y" : "ies", why);
        }
    }

    void RemoteBodies::Forget(Body* b)
    {
        if (game_ && b->slot >= 0) game_->RemoteFree(b->slot);
        b->slot = -1;
        b->aiSkipped = false;
        b->verify = false;
        b->state = RB_WAIT;
        b->nextTry = 0;
        b->applyFails = 0;
        b->destroyTries = 0;
        b->downSince = 0;
        b->track.Clear();   // respawn from the next snapshot
        ++forgets_;
        if (b->left) Free(b);
    }

    void RemoteBodies::MarkVerify()
    {
        for (int i = 0; i < kMaxPlayers; ++i)
            if (bodies_[i].id != 0 && (bodies_[i].state == RB_LIVE || bodies_[i].state == RB_REMOVING)) bodies_[i].verify = true;
    }

    // The world is back after a menu, a load or a stall: keep each body whose hand still resolves to what was
    // spawned there, forget the others (no destroy: a body that does not verify is not ours to touch).
    void RemoteBodies::VerifyAll()
    {
        int kept = 0, forgot = 0;
        for (int i = 0; i < kMaxPlayers; ++i)
        {
            Body* b = &bodies_[i];
            if (b->id == 0 || !b->verify) continue;
            b->verify = false;
            if (game_->RemoteVerify(b->slot))
            {
                ++kept;
                ++kept_;
                b->track.Clear();   // poses from before the gap would drive it into the past
                b->planner.Reset(cfg_.drive, cfg_.snapDist);
                continue;
            }
            if (b->state == RB_LIVE) LogSummary(b, "the world changed and its hand no longer resolves to it");
            ++forgot;
            Forget(b);
        }
        if (kept + forgot > 0)
            Logf("remote: the world is back: kept %d bod%s, forgot %d without touching the game", kept, kept == 1 ? "y" : "ies",
                 forgot);
    }

    void RemoteBodies::World(unsigned long long nowMs, bool localOk, unsigned int worldEpoch, const float* localPos, bool paused)
    {
        now_ = nowMs;
        if (!cfg_.on) return;
        if (!localOk)
        {
            if (worldLive_) MarkVerify();   // no game call while there is no world
            worldLive_ = false;
            return;
        }
        if (!haveEpoch_ || worldEpoch != epoch_)
        {
            if (haveEpoch_) MarkVerify();
            haveEpoch_ = true;
            epoch_ = worldEpoch;
            worldSince_ = nowMs;
        }
        else if (!worldLive_)
            worldSince_ = nowMs;   // back from the menu in the same epoch: settle again
        worldLive_ = true;
        for (int i = 0; i < 3; ++i) localPos_[i] = localPos[i];
        paused_ = paused;
    }

    void RemoteBodies::OnJoined(unsigned short id, const char* name)
    {
        if (!cfg_.on) return;
        Body* b = Add(id, name);
        if (b) b->left = false;
    }

    void RemoteBodies::OnLeft(unsigned short id, const char* why)
    {
        if (!cfg_.on) return;
        Body* b = FindMutable(id);
        if (!b) return;
        b->left = true;
        if (b->state == RB_LIVE) Remove(b, now_, why);
        else if (b->state != RB_REMOVING) Free(b);
    }

    void RemoteBodies::OnSessionEnd(const char* why)
    {
        if (!cfg_.on) return;
        for (int i = 0; i < kMaxPlayers; ++i)
            if (bodies_[i].id != 0) OnLeft(bodies_[i].id, why);
    }

    void RemoteBodies::OnPose(unsigned long long nowMs, const KpRemotePose& p, const char* name)
    {
        if (!cfg_.on) return;
        Body* b = Add(p.playerId, name);
        if (!b || b->left) return;
        b->track.Add(nowMs, p);
        b->lastRecv = nowMs;
        b->lastAge = p.ageMs;
    }

    void RemoteBodies::Stop()
    {
        ForgetAll("networking stops");
        for (int i = 0; i < kMaxPlayers; ++i) Free(&bodies_[i]);
        worldLive_ = false;
        haveEpoch_ = false;
    }

    bool RemoteBodies::TrySpawn(Body* b, unsigned long long nowMs)
    {
        TrackSample s;
        if (!b->track.At(nowMs, &s)) return false;
        RemoteSpawnInfo info;
        memset(&info, 0, sizeof(info));
        info.slot = -1;
        if (!game_->RemoteSpawn(cfg_, b->name, s.pos, s.yaw, &info) || info.slot < 0)
        {
            ++b->spawnFails;
            b->nextTry = nowMs + kRetryMs;
            if (b->spawnFails >= kMaxSpawnFails)
            {
                b->state = RB_GIVEUP;
                Logf("remote: spawning a body for %s (%u) failed %d times - giving up on this player", b->name, b->id,
                     b->spawnFails);
            }
            else
                Logf("remote: spawning a body for %s (%u) failed (try %d of %d)", b->name, b->id, b->spawnFails,
                     (int)kMaxSpawnFails);
            return false;
        }
        ++spawns_;
        b->slot = info.slot;
        b->state = RB_LIVE;
        b->verify = false;
        b->downSince = 0;
        b->destroyTries = 0;
        b->stateAt = nowMs;
        b->applyFails = 0;
        b->lastStatus = 0;
        memset(&b->stats, 0, sizeof(b->stats));
        memset(&b->lastCmd, 0, sizeof(b->lastCmd));
        b->planner.Reset(cfg_.drive, cfg_.snapDist);
        // Steer drives the body directly (AK's way): its AI is skipped. Order hands the walk to its AI's
        // move job, so the AI stays on (M4: skipping it stalled the order).
        b->aiSkipped = cfg_.drive == DM_STEER;
        game_->RemoteSkipAi(b->slot, b->aiSkipped);
        Logf("remote: spawned a body for %s (%u) at %.1f %.1f %.1f, faction %s%s, drive %s, AI %s", b->name, b->id, s.pos[0],
             s.pos[1], s.pos[2], info.faction, info.factionAsAsked ? "" : " (NOT the one asked for)", DriveModeName(cfg_.drive),
             b->aiSkipped ? "skipped" : "on");
        return true;
    }

    void RemoteBodies::Drive(Body* b, unsigned long long nowMs)
    {
        BodyState st;
        memset(&st, 0, sizeof(st));
        if (!game_->RemoteUpdate(b->slot, cfg_.maxSpeed, &st))
        {
            // Gone without us (the game removed it: zone unload, ...) or the hand resolves to someone else.
            LogSummary(b, "its hand no longer resolves to it");
            game_->RemoteFree(b->slot);
            b->slot = -1;
            b->aiSkipped = false;
            b->state = RB_WAIT;
            b->nextTry = nowMs + kRetryMs;
            return;
        }
        b->body = st;
        if (st.down == 2) { Remove(b, nowMs, "it died"); b->nextTry = nowMs + kRetryMs; return; }
        if (st.down == 1)
        {
            if (b->downSince == 0) b->downSince = nowMs;
            else if (nowMs - b->downSince >= (unsigned long long)kDownMaxMs)
            {
                Remove(b, nowMs, "knocked out for 10 s");
                b->nextTry = nowMs + kRetryMs;
                return;
            }
        }
        else b->downSince = 0;
        if (paused_) return;   // nothing moves; the game drops teleports while paused (M4)
        TrackSample target, newest;
        if (!b->track.At(nowMs - DrivePlanner::kRenderDelayMs, &target) || !b->track.At(nowMs, &newest)) return;
        DriveCmd c = b->planner.Step(nowMs, st, target, &b->lastGap);
        b->lastLag = DistXZ(st.pos, newest.pos);
        RemoteBodyStats& s = b->stats;
        ++s.samples;
        s.gapSum += b->lastGap;
        if (b->lastGap > s.gapMax) s.gapMax = b->lastGap;
        s.lagSum += b->lastLag;
        if (b->lastLag > s.lagMax) s.lagMax = b->lastLag;
        if (st.moving) ++s.moving;
        if (st.down) ++s.down;
        if (target.moving && !target.paused && LenXZ(target.vel) > 1.0f)
        {
            s.speedSum += st.speed;
            ++s.speedSamples;
        }
        if (c.kind == DC_SNAP) ++s.snaps;
        if (c.kind == DC_WALK) ++s.orders;
        if (c.kind != DC_NONE)
        {
            b->lastCmd = c;
            if (game_->RemoteApply(b->slot, c, cfg_.drive)) b->applyFails = 0;
            else if (++b->applyFails >= kMaxApplyFails)
            {
                Remove(b, nowMs, "game calls faulted 3 times in a row");
                b->nextTry = nowMs + kRetryMs;
                return;
            }
        }
        if (nowMs - b->lastStatus >= (unsigned long long)kStatusMs)
        {
            b->lastStatus = nowMs;
            Logf("remote: %s (%u) %s body %.1f %.1f %.1f target %.1f %.1f %.1f gap %.1f lag %.1f speed %.1f (asked %.1f, "
                 "desired %.1f, max %.1f, orders %d) moving %d%s last %s",
                 b->name, b->id, DriveModeName(cfg_.drive), st.pos[0], st.pos[1], st.pos[2], target.pos[0], target.pos[1],
                 target.pos[2], b->lastGap, b->lastLag, st.speed, b->lastCmd.speed, st.desired, st.maxSpeed, st.speedOrders,
                 st.moving, st.down ? " DOWN" : "",
                 b->lastCmd.kind == DC_WALK ? "walk" : b->lastCmd.kind == DC_STEER ? "steer" : b->lastCmd.kind == DC_SNAP
                     ? "snap" : b->lastCmd.kind == DC_HALT ? "halt" : b->lastCmd.kind == DC_PLACE ? "place" : "-");
        }
    }

    void RemoteBodies::Frame(unsigned long long nowMs)
    {
        now_ = nowMs;
        if (!cfg_.on || !game_) return;
        if (worldLive_) VerifyAll();
        bool settled = worldLive_ && nowMs - worldSince_ >= (unsigned long long)cfg_.settleMs;
        int live = CountState(RB_LIVE) + CountState(RB_REMOVING);
        bool spawnedThisFrame = false;
        for (int i = 0; i < kMaxPlayers; ++i)
        {
            Body* b = &bodies_[i];
            if (b->id == 0) continue;
            bool recvFresh = b->lastRecv != 0 && nowMs - b->lastRecv <= (unsigned long long)kStaleRecvMs;
            bool poseFresh = recvFresh && b->lastAge <= (unsigned int)kStaleAgeMs;
            TrackSample newest;
            bool haveNewest = b->track.At(nowMs, &newest);
            float dist = haveNewest ? DistXZ(localPos_, newest.pos) : 1.0e30f;
            switch (b->state)
            {
            case RB_LIVE:
                if (!worldLive_) break;   // cannot happen: World() forgot it
                if (!recvFresh) { Remove(b, nowMs, "no snapshot for 3 s"); break; }
                if (!poseFresh) { Remove(b, nowMs, "its newest pose is older than 15 s"); break; }
                if (!haveNewest) break;   // the track starts over after a world change: wait for the next pose
                if (dist > cfg_.range + (float)kRangeSlack) { Remove(b, nowMs, "out of range"); break; }
                Drive(b, nowMs);
                break;
            case RB_REMOVING:
            {
                if (!worldLive_) break;   // no game call without a world; checked again once it is back
                bool exists = game_->RemoteExists(b->slot);
                if (exists && nowMs - b->stateAt >= (unsigned long long)kRemoveWaitMs)
                {
                    // destroy can take the immediate or the deferred path (1.0.65 0x798fc3, critic): never respawn while
                    // the old body may still stand there; ask again, then give up on it (logged, it stays in the world).
                    if (b->destroyTries < kMaxDestroyTries)
                    {
                        ++b->destroyTries;
                        b->stateAt = nowMs;
                        Logf("remote: body of %s (%u) still there 10 s after destroy - asking again (%d of %d)", b->name, b->id,
                             b->destroyTries, (int)kMaxDestroyTries);
                        if (game_->RemoteDestroy(b->slot, "KenshiPure: remote player gone")) break;
                    }
                    Logf("remote: body of %s (%u) did not go away after %d destroy calls - forgetting it (it may stay in the world)",
                         b->name, b->id, b->destroyTries);
                    exists = false;
                }
                if (!exists)
                {
                    Logf("remote: body of %s (%u) is gone", b->name, b->id);
                    game_->RemoteFree(b->slot);
                    b->slot = -1;
                    if (b->left) { Free(b); break; }
                    b->state = RB_WAIT;
                    if (b->nextTry < nowMs) b->nextTry = nowMs;
                    --live;
                }
                break;
            }
            case RB_WAIT:
                if (b->left) { Free(b); break; }
                if (!settled || spawnedThisFrame || live >= kMaxBodies || !poseFresh || nowMs < b->nextTry) break;
                if (dist > cfg_.range) break;
                spawnedThisFrame = true;   // one spawn per frame (each is a heavy game call)
                if (TrySpawn(b, nowMs)) ++live;
                break;
            default:
                break;
            }
        }
    }
}
