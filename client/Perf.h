// Performance line (Perf.cpp): every 10 s frames per second, Kenshi's CPU time and how long a
// Sleep(1) really takes in this process (the timer resolution the game's own waits get).
#pragma once

namespace kp
{
    // frames: the frame counter of the frame hook (read without a lock; a torn read only skews one line).
    void PerfStart(const volatile unsigned long long* frames);
}
