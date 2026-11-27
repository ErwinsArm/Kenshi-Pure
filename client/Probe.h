// KenshiPure client: live game probe. A read-only TCP server on 127.0.0.1 (own thread) that answers
// line-based JSON requests. Anything that touches the game is queued and answered by the frame hook
// on the game thread; no game call ever runs on the server thread.
// Built with the VC++ 2010 (v100) toolset; keep to C++ that compiler accepts.
#pragma once

class GameWorld;

namespace kp
{
    // Remembers what BuildCheck found (shown by "status").
    void ProbeSetBuild(const char* buildName, const char* kenshiVersion);
    // Reads KenshiPure.cfg ("probe_port=N", 0 = off), starts the server thread. Call once.
    void ProbeStart();
    // Call from the frame hook (game thread) after the original frame ran: answers a queued request.
    void ProbeOnFrame(GameWorld* gw, unsigned long long frames, unsigned long frameThread);
}
