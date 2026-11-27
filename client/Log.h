// KenshiPure client: log file next to the plugin DLL (mods/KenshiPure/KenshiPure.log).
// Built with the VC++ 2010 (v100) toolset; keep to C++ that compiler accepts.
#pragma once

namespace kp
{
    // Opens the log (the previous one is kept as KenshiPure.prev.log). Safe to call once.
    void LogOpen();
    // printf-style line with time and thread id; flushed at once so a crash loses nothing.
    void Log(const char* fmt, ...);
    // Same, but gives up instead of waiting if another thread holds the log (for the
    // exception handler, which must never block).
    void LogTry(const char* fmt, ...);
    // Folder of the plugin DLL, with trailing backslash (wide).
    const wchar_t* PluginDir();
}
