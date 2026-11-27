// See Drive.h. VC++ 2010 and MSVC 14.44 both compile this file: keep to C++03 plus what both accept.
#include "Drive.h"

#include <math.h>
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

    float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    float LenXZ(const float v[3]) { return sqrtf(v[0] * v[0] + v[2] * v[2]); }

    // Shortest-arc blend of two angles in degrees, result in (-180, 180].
    float LerpYaw(float a, float b, float f)
    {
        float d = fmodf(b - a, 360.0f);
        if (d > 180.0f) d -= 360.0f;
        if (d < -180.0f) d += 360.0f;
        float r = fmodf(a + d * f, 360.0f);
        if (r > 180.0f) r -= 360.0f;
        if (r <= -180.0f) r += 360.0f;
        return r;
    }

    void ZeroCmd(kp::DriveCmd* c) { memset(c, 0, sizeof(*c)); }
}

namespace kp
{
    float DistXZ(const float a[3], const float b[3])
    {
        float dx = a[0] - b[0], dz = a[2] - b[2];
        return sqrtf(dx * dx + dz * dz);
    }

    const char* DriveModeName(int mode)
    {
        switch (mode)
        {
        case DM_OFF: return "off";
        case DM_PLACE: return "place";
        case DM_PATH: return "path";
        case DM_ORDER: return "order";
        case DM_STEER: return "steer";
        case DM_CYCLE: return "cycle";
        default: return "?";
        }
    }

    void DriveParseConfig(const char* text, DriveConfig* cfg)
    {
        memset(cfg, 0, sizeof(*cfg));
        cfg->mode = DM_OFF;
        cfg->aiSkip = DA_AI;
        cfg->snapDist = 300.0f;
        cfg->pickDist = 600.0f;
        cfg->settleMs = 0;
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
                if (strcmp(key, "standin") == 0)
                {
                    for (int m = DM_OFF; m <= DM_CYCLE; ++m)
                        if (strcmp(val, DriveModeName(m)) == 0) cfg->mode = m;
                }
                else if (strcmp(key, "standin_ai") == 0)
                {
                    if (strcmp(val, "none") == 0) cfg->aiSkip = DA_NONE;
                    else if (strcmp(val, "ai") == 0) cfg->aiSkip = DA_AI;
                    else if (strcmp(val, "character") == 0) cfg->aiSkip = DA_CHARACTER;
                }
                else if (strcmp(key, "standin_snap") == 0)
                {
                    float v = (float)atof(val);
                    if (v >= 10.0f && v <= 100000.0f) cfg->snapDist = v;
                }
                else if (strcmp(key, "standin_range") == 0)
                {
                    float v = (float)atof(val);
                    if (v >= 10.0f && v <= 100000.0f) cfg->pickDist = v;
                }
                else if (strcmp(key, "standin_settle") == 0)
                {
                    long v = strtol(val, NULL, 10);
                    if (v >= 0 && v <= 600) cfg->settleMs = (unsigned int)v * 1000;
                }
                else if (strcmp(key, "standin_name") == 0)
                {
                    strcpy_s(cfg->name, sizeof(cfg->name), val);
                }
            }
            p = *end ? end + 1 : end;
        }
    }

    // ------------------------------------------------------------------ RemoteTrack

    void RemoteTrack::Clear()
    {
        memset(s_, 0, sizeof(s_));
        n_ = 0;
        lastAdd_ = 0;
    }

    void RemoteTrack::Add(unsigned long long nowMs, const KpRemotePose& p)
    {
        TrackSample s;
        memset(&s, 0, sizeof(s));
        s.t = nowMs > p.ageMs ? nowMs - p.ageMs : 0;
        for (int i = 0; i < 3; ++i) { s.pos[i] = p.pose.pos[i]; s.vel[i] = p.pose.vel[i]; }
        s.yaw = p.pose.yaw;
        s.moving = (p.pose.flags & KP_POSE_MOVING) ? 1 : 0;
        s.paused = (p.pose.flags & KP_POSE_PAUSED) ? 1 : 0;
        lastAdd_ = nowMs;
        if (n_ > 0)
        {
            const TrackSample& last = s_[n_ - 1];
            // The server repeats the latest pose each snapshot with a growing age: same pose, same t.
            if (memcmp(last.pos, s.pos, sizeof(s.pos)) == 0 && last.yaw == s.yaw && last.moving == s.moving &&
                last.paused == s.paused)
                return;
            if (s.t <= last.t) s.t = last.t + 1;   // age jitter must not reorder the track
        }
        if (n_ == kMax)
        {
            memmove(s_, s_ + 1, sizeof(s_[0]) * (kMax - 1));
            --n_;
        }
        s_[n_++] = s;
    }

    bool RemoteTrack::At(unsigned long long t, TrackSample* out) const
    {
        if (n_ == 0) return false;
        if (t <= s_[0].t)
        {
            *out = s_[0];
            return true;
        }
        for (int i = 0; i + 1 < n_; ++i)
        {
            const TrackSample& a = s_[i];
            const TrackSample& b = s_[i + 1];
            if (t >= a.t && t < b.t)
            {
                float f = (float)(t - a.t) / (float)(b.t - a.t);
                *out = b;
                for (int k = 0; k < 3; ++k)
                {
                    out->pos[k] = a.pos[k] + (b.pos[k] - a.pos[k]) * f;
                    out->vel[k] = a.vel[k] + (b.vel[k] - a.vel[k]) * f;
                }
                out->yaw = LerpYaw(a.yaw, b.yaw, f);
                out->t = t;
                return true;
            }
        }
        const TrackSample& l = s_[n_ - 1];
        *out = l;
        unsigned long long ahead = t - l.t;
        if (ahead > (unsigned long long)kExtrapolateMs) ahead = kExtrapolateMs;
        if (l.moving && !l.paused)
            for (int k = 0; k < 3; ++k) out->pos[k] = l.pos[k] + l.vel[k] * ((float)ahead / 1000.0f);
        out->t = t;
        return true;
    }

    // ------------------------------------------------------------------ DrivePlanner

    DrivePlanner::DrivePlanner() { Reset(DM_OFF, 300.0f); }

    void DrivePlanner::Reset(int mode, float snapDist)
    {
        mode_ = mode;
        snapDist_ = snapDist;
        haltPending_ = true;
        halted_ = false;
        haveOrder_ = false;
        memset(orderPos_, 0, sizeof(orderPos_));
        orderAt_ = lastPlace_ = lastSnap_ = 0;
        snapped_ = false;
        memset(&stats_, 0, sizeof(stats_));
    }

    DriveCmd DrivePlanner::Walk(unsigned long long nowMs, const float dest[3], float speed)
    {
        DriveCmd c;
        ZeroCmd(&c);
        c.kind = DC_WALK;
        for (int i = 0; i < 3; ++i) { c.pos[i] = dest[i]; orderPos_[i] = dest[i]; }
        c.speed = speed;
        haveOrder_ = true;
        orderAt_ = nowMs;
        halted_ = false;
        ++stats_.orders;
        return c;
    }

    DriveCmd DrivePlanner::Step(unsigned long long nowMs, const BodyState& body, const TrackSample& target, float* gapOut)
    {
        DriveCmd c;
        ZeroCmd(&c);
        float gap = DistXZ(body.pos, target.pos);
        if (gapOut) *gapOut = gap;
        ++stats_.samples;
        stats_.gapSum += gap;
        if (gap > stats_.gapMax) stats_.gapMax = gap;
        if (body.moving) ++stats_.moving;

        if (haltPending_)
        {
            haltPending_ = false;
            halted_ = true;
            haveOrder_ = false;
            c.kind = DC_HALT;
            return c;
        }
        if (mode_ == DM_PLACE)
        {
            if (lastPlace_ != 0 && nowMs - lastPlace_ < (unsigned long long)kPlaceEveryMs) return c;
            lastPlace_ = nowMs;
            c.kind = DC_PLACE;
            for (int i = 0; i < 3; ++i) c.pos[i] = target.pos[i];
            c.yaw = target.yaw;
            return c;
        }
        if (mode_ != DM_PATH && mode_ != DM_ORDER && mode_ != DM_STEER) return c;

        if (gap > snapDist_ && (!snapped_ || nowMs - lastSnap_ >= (unsigned long long)kSnapCooldownMs))
        {
            snapped_ = true;
            lastSnap_ = nowMs;
            haveOrder_ = false;   // walk again from the new place
            ++stats_.snaps;
            c.kind = DC_SNAP;
            for (int i = 0; i < 3; ++i) c.pos[i] = target.pos[i];
            c.yaw = target.yaw;
            return c;
        }

        float remoteSpeed = LenXZ(target.vel);
        bool remoteMoving = target.moving && !target.paused;
        if (mode_ == DM_PATH || mode_ == DM_ORDER)
        {
            if (remoteMoving)
            {
                // KenshiCoop's lead point: 0.6 s ahead along the sender's velocity; re-issued only
                // when it moved, since a new order every frame restarts the path (remote-bodies.md G5).
                float lead[3] = { target.pos[0] + target.vel[0] * 0.6f, target.pos[1], target.pos[2] + target.vel[2] * 0.6f };
                if (!haveOrder_ || DistXZ(lead, orderPos_) > 15.0f || nowMs - orderAt_ >= (unsigned long long)kReissueMs)
                {
                    float speed = remoteSpeed * Clamp(1.0f + gap / 100.0f, 1.0f, 2.5f);
                    return Walk(nowMs, lead, speed < 10.0f ? 10.0f : speed);
                }
                return c;
            }
            if (gap > 20.0f)
            {
                if (!haveOrder_ || DistXZ(target.pos, orderPos_) > 15.0f || nowMs - orderAt_ >= (unsigned long long)kReissueMs)
                    return Walk(nowMs, target.pos, Clamp(gap, 10.0f, 100.0f));
                return c;
            }
            if (!halted_)
            {
                halted_ = true;
                haveOrder_ = false;
                c.kind = DC_HALT;
            }
            return c;
        }

        // DM_STEER (AK's way, values from AK 0x50bd0a..0x50c699, M): wanted velocity = the sender's velocity plus a
        // correction toward the target; its length capped at min(1.5 x the sender's speed, 200) (200 when the sender
        // stands). The game multiplies the direction by the speed (HavokCharacter::updateVelocity, 1.0.65 0x147c60, M),
        // so the command carries a unit direction and the length as the speed; M4 sent the whole velocity.
        float v[3] = { target.vel[0] + (target.pos[0] - body.pos[0]) * 2.0f, 0.0f,
                       target.vel[2] + (target.pos[2] - body.pos[2]) * 2.0f };
        if (!remoteMoving) { v[0] = (target.pos[0] - body.pos[0]) * 2.0f; v[2] = (target.pos[2] - body.pos[2]) * 2.0f; }
        float len = LenXZ(v);
        if ((!remoteMoving && gap < 5.0f) || len < 0.01f)
        {
            if (!halted_)
            {
                halted_ = true;
                c.kind = DC_HALT;
            }
            return c;
        }
        halted_ = false;
        float cap = remoteSpeed > 0.1f ? 1.5f * remoteSpeed : 200.0f;
        if (cap > 200.0f) cap = 200.0f;
        if (cap < 20.0f) cap = 20.0f;   // a slow sender must still let the body close a gap
        float speed = len > cap ? cap : len;
        c.kind = DC_STEER;
        c.vel[0] = v[0] / len;
        c.vel[2] = v[2] / len;
        c.speed = speed;
        // The game keeps sqrt(limit) as the most one step may move (CharMovement::setDirectMovement 1.0.65 0x3332c0,
        // update 0x65f6b3, M); speed^2 makes that one second of motion, so it never cuts a frame's step.
        c.limit = speed * speed;
        return c;
    }
}
