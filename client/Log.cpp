#include "Log.h"

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include <share.h>
#include <string>

namespace
{
    CRITICAL_SECTION g_lock;
    bool g_ready = false;
    FILE* g_file = NULL;
    wchar_t g_dir[MAX_PATH] = L"";
    ULONGLONG g_start = 0;

    void FindPluginDir()
    {
        HMODULE self = NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&FindPluginDir), &self);
        DWORD n = GetModuleFileNameW(self, g_dir, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) { g_dir[0] = 0; return; }
        wchar_t* slash = wcsrchr(g_dir, L'\\');
        if (slash) slash[1] = 0;
    }

    // Last lines kept in memory for the probe's log_tail command (guarded by g_lock).
    const int kRing = 500;
    char g_ring[kRing][256];
    unsigned long long g_ringCount = 0;

    void WriteLine(const char* fmt, va_list args)
    {
        SYSTEMTIME t;
        GetLocalTime(&t);
        double up = (GetTickCount64() - g_start) / 1000.0;
        char line[256];
        int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "%02u:%02u:%02u.%03u +%9.3f [%5lu] ", t.wHour, t.wMinute,
                            t.wSecond, t.wMilliseconds, up, GetCurrentThreadId());
        if (n > 0) _vsnprintf_s(line + n, sizeof(line) - n, _TRUNCATE, fmt, args);
        _snprintf_s(g_ring[g_ringCount % kRing], sizeof(g_ring[0]), _TRUNCATE, "%s", line);
        ++g_ringCount;
        if (!g_file) return;
        fputs(line, g_file);
        fputc('\n', g_file);
        fflush(g_file);
    }
}

namespace kp
{
    void LogOpen(const wchar_t* name)
    {
        if (g_ready) return;
        InitializeCriticalSection(&g_lock);
        g_start = GetTickCount64();
        FindPluginDir();
        wchar_t path[MAX_PATH], prev[MAX_PATH];
        _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s%s.log", g_dir, name);
        _snwprintf_s(prev, MAX_PATH, _TRUNCATE, L"%s%s.prev.log", g_dir, name);
        MoveFileExW(path, prev, MOVEFILE_REPLACE_EXISTING);
        g_file = _wfsopen(path, L"w", _SH_DENYWR);
        g_ready = true;
    }

    void Log(const char* fmt, ...)
    {
        if (!g_ready) return;
        EnterCriticalSection(&g_lock);
        va_list args;
        va_start(args, fmt);
        WriteLine(fmt, args);
        va_end(args);
        LeaveCriticalSection(&g_lock);
    }

    void LogTry(const char* fmt, ...)
    {
        if (!g_ready || !TryEnterCriticalSection(&g_lock)) return;
        va_list args;
        va_start(args, fmt);
        WriteLine(fmt, args);
        va_end(args);
        LeaveCriticalSection(&g_lock);
    }

    std::string LogTail(int lines)
    {
        std::string out;
        if (!g_ready) return out;
        EnterCriticalSection(&g_lock);
        unsigned long long have = g_ringCount < kRing ? g_ringCount : kRing;
        unsigned long long take = (unsigned long long)lines < have ? lines : have;
        for (unsigned long long i = g_ringCount - take; i < g_ringCount; ++i)
        {
            out += g_ring[i % kRing];
            out += '\n';
        }
        LeaveCriticalSection(&g_lock);
        return out;
    }

    const wchar_t* PluginDir() { return g_dir; }
}
