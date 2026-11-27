#include "Log.h"

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include <share.h>

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

    void WriteLine(const char* fmt, va_list args)
    {
        if (!g_file) return;
        SYSTEMTIME t;
        GetLocalTime(&t);
        double up = (GetTickCount64() - g_start) / 1000.0;
        fprintf(g_file, "%02u:%02u:%02u.%03u +%9.3f [%5lu] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
                up, GetCurrentThreadId());
        vfprintf(g_file, fmt, args);
        fputc('\n', g_file);
        fflush(g_file);
    }
}

namespace kp
{
    void LogOpen()
    {
        if (g_ready) return;
        InitializeCriticalSection(&g_lock);
        g_start = GetTickCount64();
        FindPluginDir();
        wchar_t path[MAX_PATH], prev[MAX_PATH];
        _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%sKenshiPure.log", g_dir);
        _snwprintf_s(prev, MAX_PATH, _TRUNCATE, L"%sKenshiPure.prev.log", g_dir);
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

    const wchar_t* PluginDir() { return g_dir; }
}
