// Unit tests for client/PoseMath.h (no game needed). Built and run by scripts/build-plugin.ps1.
#include <stdio.h>
#include <math.h>

#include "PoseMath.h"

static int g_failed = 0;

static void Check(bool ok, const char* what)
{
    if (!ok) { printf("FAIL: %s\n", what); ++g_failed; }
}

static bool Near(float a, float b, float eps) { return fabs(a - b) <= eps; }

int main()
{
    const float s45 = (float)sqrt(0.5);
    // Rotations about the up axis (y): q = (cos(a/2), 0, sin(a/2), 0).
    Check(Near(kp::YawDegFromQuat(1, 0, 0, 0), 0.0f, 1e-4f), "identity faces +z (yaw 0)");
    Check(Near(kp::YawDegFromQuat(s45, 0, s45, 0), 90.0f, 1e-3f), "+90 about y faces +x");
    Check(Near(kp::YawDegFromQuat(s45, 0, -s45, 0), -90.0f, 1e-3f), "-90 about y faces -x");
    Check(Near((float)fabs(kp::YawDegFromQuat(0, 0, 1, 0)), 180.0f, 1e-3f), "180 about y faces -z");
    // A tilt about x does not change the heading while +z stays out of the vertical.
    float c = (float)cos(0.2), s = (float)sin(0.2);
    Check(Near(kp::YawDegFromQuat(c, s, 0, 0), 0.0f, 1e-3f), "pitch keeps yaw 0");
    // +Z turned straight up (90 about x): no heading.
    Check(_isnan(kp::YawDegFromQuat(s45, s45, 0, 0)) != 0, "vertical forward gives NaN");

    float good[3] = { 1.0f, -2.0f, 3.0f };
    float big[3] = { 1.0f, 2e7f, 3.0f };
    float nan[3] = { 1.0f, 0.0f, 0.0f };
    nan[1] = std::numeric_limits<float>::quiet_NaN();
    float inf[3] = { 0.0f, 0.0f, 0.0f };
    inf[2] = std::numeric_limits<float>::infinity();
    Check(kp::AllFinite(good, 3, 1e7f), "finite values pass");
    Check(!kp::AllFinite(big, 3, 1e7f), "value beyond limit fails");
    Check(!kp::AllFinite(nan, 3, 1e7f), "NaN fails");
    Check(!kp::AllFinite(inf, 3, 1e7f), "infinity fails");

    float a[3] = { 0, 100, 0 }, b[3] = { 3, -50, 4 };
    Check(Near(kp::DistXZ2(a, b), 25.0f, 1e-5f), "xz distance ignores height");
    Check(Near(kp::StepPerSecond(a, b, 1000), 5.0f, 1e-5f), "5 units in 1 s");
    Check(Near(kp::StepPerSecond(a, b, 500), 10.0f, 1e-5f), "5 units in 0.5 s");
    Check(kp::StepPerSecond(a, b, 0) == 0.0f, "zero interval gives 0");

    kp::RateLimiter r;
    Check(r.Due(5000, 1000), "first call fires");
    Check(!r.Due(5500, 1000), "not again within the period");
    Check(!r.Due(5999, 1000), "not again just before the period");
    Check(r.Due(6000, 1000), "fires after the period");
    Check(!r.Due(6001, 1000), "period restarts");

    if (g_failed) { printf("pose-math-test: %d failed\n", g_failed); return 1; }
    printf("pose-math-test: ok\n");
    return 0;
}
