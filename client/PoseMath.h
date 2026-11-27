// Pure helpers for reading the local character's pose (M1): no game access, so they are unit-tested
// without Kenshi (client/test/PoseMathTest.cpp, run by scripts/build-plugin.ps1).
// Built with the VC++ 2010 (v100) toolset; keep to C++ that compiler accepts.
#pragma once

#include <float.h>
#include <math.h>
#include <limits>

namespace kp
{
    // Yaw in degrees from an orientation quaternion (Ogre order w, x, y, z; Kenshi's y is up): the
    // heading of the rotated +Z axis in the x/z plane, atan2(x, z). 0 = facing +z, 90 = facing +x.
    // A quaternion that turns +Z straight up or down has no heading: returns NaN.
    inline float YawDegFromQuat(float w, float x, float y, float z)
    {
        float fx = 2.0f * (x * z + w * y);
        float fz = 1.0f - 2.0f * (x * x + y * y);
        if (fx * fx + fz * fz < 1e-12f) return std::numeric_limits<float>::quiet_NaN();
        return (float)(atan2((double)fx, (double)fz) * 57.29577951308232);
    }

    // All n floats finite and within +-limit (Kenshi's world is a few hundred thousand units wide).
    inline bool AllFinite(const float* v, int n, float limit)
    {
        for (int i = 0; i < n; ++i)
            if (!_finite(v[i]) || v[i] > limit || v[i] < -limit) return false;
        return true;
    }

    // Squared distance in the x/z plane (ignores height).
    inline float DistXZ2(const float* a, const float* b)
    {
        float dx = a[0] - b[0], dz = a[2] - b[2];
        return dx * dx + dz * dz;
    }

    // Distance moved in the x/z plane per real second; 0 for a non-positive interval.
    inline float StepPerSecond(const float* from, const float* to, unsigned long long ms)
    {
        if (ms == 0) return 0.0f;
        return (float)(sqrt((double)DistXZ2(from, to)) * 1000.0 / (double)ms);
    }

    // Fires on the first call and then at most once per period (milliseconds, caller's clock).
    struct RateLimiter
    {
        unsigned long long last;
        bool started;
        RateLimiter() : last(0), started(false) {}
        bool Due(unsigned long long now, unsigned long long period)
        {
            if (started && now - last < period) return false;
            started = true;
            last = now;
            return true;
        }
    };
}
