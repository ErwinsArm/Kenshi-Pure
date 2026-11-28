// Performance line, own thread, no game calls (docs/client.md "Timer resolution").
// ENet's enet_initialize (win32.c) calls timeBeginPeriod(1). Since Windows 10 2004 the resolution a
// process asked for applies to that process's own waits, so Sleep(1) measured here shows what the
// game's Sleep/wait calls get. The system value from NtQueryTimerResolution is the finest any process
// asked for (other programs count too).
// Built with the VC++ 2010 (v100) toolset.
#include "Perf.h"
#include "Log.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

namespace
{
    const volatile unsigned long long* g_frames = NULL;

    typedef LONG (WINAPI* NtQueryTimerResolutionFn)(PULONG, PULONG, PULONG);

    double SystemTimerMs()
    {
        static NtQueryTimerResolutionFn q = (NtQueryTimerResolutionFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryTimerResolution");
        ULONG mn = 0, mx = 0, cur = 0;
        if (!q || q(&mn, &mx, &cur) != 0) return -1.0;
        return cur / 10000.0;   // 100 ns units
    }

    double Sleep1Ms()
    {
        LARGE_INTEGER f, a, b;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&a);
        for (int i = 0; i < 4; ++i) Sleep(1);
        QueryPerformanceCounter(&b);
        return (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart / 4.0;
    }

    unsigned long long CpuTime100ns()
    {
        FILETIME c, e, k, u;
        if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0;
        return (((unsigned long long)k.dwHighDateTime << 32) | k.dwLowDateTime) +
               (((unsigned long long)u.dwHighDateTime << 32) | u.dwLowDateTime);
    }

    DWORD WINAPI PerfThread(LPVOID)
    {
        ULONGLONG lastT = GetTickCount64();
        unsigned long long lastF = *g_frames, lastCpu = CpuTime100ns();
        for (;;)
        {
            Sleep(10000);
            unsigned long long f = *g_frames;
            ULONGLONG t = GetTickCount64();
            unsigned long long cpu = CpuTime100ns();
            double sec = (t - lastT) / 1000.0;
            if (sec <= 0.0) continue;
            if (f > 0)
                kp::Log("perf: fps %.1f, cpu %.0f%% of one core, Sleep(1) %.2f ms, system timer %.2f ms",
                        (f - lastF) / sec, (cpu - lastCpu) / 1e5 / sec, Sleep1Ms(), SystemTimerMs());
            lastT = t; lastF = f; lastCpu = cpu;
        }
    }

}

namespace kp
{
    void PerfStart(const volatile unsigned long long* frames)
    {
        g_frames = frames;
        HANDLE t = CreateThread(NULL, 0, PerfThread, NULL, 0, NULL);
        if (t) { SetThreadPriority(t, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(t); }
        Log("perf: a line every 10 s (fps, cpu, Sleep(1)); Sleep(1) now %.2f ms, system timer %.2f ms",
            Sleep1Ms(), SystemTimerMs());
    }
}
