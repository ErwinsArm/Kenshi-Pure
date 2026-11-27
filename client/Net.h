// M3: networking in game (Net.cpp). Frame thread only, except NetStop (game exit).
// Built with the VC++ 2010 (v100) toolset; keep to C++ that compiler accepts.
#pragma once

namespace kp
{
    // Reads KenshiPure.cfg and starts the core's network thread; logs and stays off on any problem.
    void NetStart();
    // Per frame, after PoseOnFrame: hands over the local pose (10 Hz), drains and logs remote ones.
    void NetOnFrame();
    // Leaves the server and stops the network thread (called once at game exit).
    void NetStop();
}
