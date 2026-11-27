// The versioned C interface between KenshiPureLoader.dll (the Ogre plugin) and KenshiPure.dll.
// The loader fills a KpHost and calls KenshiPure's export `kpStart`. KenshiPure refuses a host whose
// version it does not know. Plain C types only, so both sides may be built by different compilers.
#pragma once

#define KP_HOST_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KpHost
{
    unsigned int size;          // sizeof(KpHost) as the loader knows it
    unsigned int version;       // KP_HOST_VERSION
    const char* loaderBuild;    // e.g. "KenshiPureLoader 0.0.2-m0b (built ...)"
    const char* exeSha256;      // SHA-256 of the running kenshi_x64.exe (64 hex chars), "" if unknown
    const wchar_t* exePath;     // full path of the running exe
    int displayHook;            // 1: the DXGI display fix is armed before the game window exists
} KpHost;

// Returns 1 when KenshiPure is active, 0 when it stays inactive (its log says why).
typedef int (*KpStartFn)(const KpHost* host);

#ifdef __cplusplus
}
#endif
