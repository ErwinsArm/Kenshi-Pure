// KenshiPureLoader: early display fix. Kenshi (Ogre D3D11) creates its swap chain with
// DXGI_MODE_SCALING_STRETCHED, and DXGI then picks a smaller, stretched full-screen mode until
// Alt+Enter (cause measured in AnarchyKenshi's log, docs/findings/AnarchyKenshi.dll.md section 5).
// We set Scaling = UNSPECIFIED in IDXGIFactory::CreateSwapChain and IDXGISwapChain::ResizeTarget.
//
// When: Kenshi calls Ogre::Root::initialise (OgreMain export, imported by both measured exes), which
// creates the window and the swap chain. By then Ogre's D3D11 render system has loaded `dxgi.dll`
// (ReShade's wrapper if present, else Windows'); we never load dxgi.dll ourselves before that, so a
// wrapper in the game folder still loads first. Inside our Root::initialise detour, before the
// original runs, we create a factory through each loaded dxgi.dll (the one the game got by name and
// Windows' own) and detour the function in vtable slot 10 (CreateSwapChain) of each; inside the
// first CreateSwapChain we detour the new swap chain's slot 14 (ResizeTarget) once per function.

#include <windows.h>
#include <dxgi.h>
#include <MinHook.h>

#include "DxgiFix.h"
#include "Log.h"

namespace
{
    const int kMax = 2;   // the game's dxgi.dll (maybe a wrapper) and Windows' dxgi.dll

    typedef HRESULT (STDMETHODCALLTYPE* CreateSwapChainFn)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
    typedef HRESULT (STDMETHODCALLTYPE* ResizeTargetFn)(IDXGISwapChain*, const DXGI_MODE_DESC*);
    typedef void* (*RootInitialiseFn)(void* root, bool autoCreateWindow, const void* title, const void* caps);

    CreateSwapChainFn g_createOrig[kMax];
    void* g_createTarget[kMax];
    int g_createCount = 0;
    ResizeTargetFn g_resizeOrig[kMax];
    void* g_resizeTarget[kMax];
    volatile LONG g_resizeCount = 0;
    CRITICAL_SECTION g_lock;
    RootInitialiseFn g_rootInitOrig = NULL;
    volatile LONG g_installed = 0;
    volatile LONG g_resizeLogged = 0;

    const char* ScalingName(DXGI_MODE_SCALING s)
    {
        return s == DXGI_MODE_SCALING_UNSPECIFIED ? "UNSPECIFIED" : s == DXGI_MODE_SCALING_CENTERED ? "CENTERED"
             : s == DXGI_MODE_SCALING_STRETCHED ? "STRETCHED" : "?";
    }

    HRESULT STDMETHODCALLTYPE ResizeCommon(int n, IDXGISwapChain* sc, const DXGI_MODE_DESC* mode)
    {
        if (mode && mode->Scaling != DXGI_MODE_SCALING_UNSPECIFIED)
        {
            DXGI_MODE_DESC m = *mode;
            if (InterlockedExchange(&g_resizeLogged, 1) == 0)
                kp::Log("dxgi: ResizeTarget %ux%u scaling %s -> UNSPECIFIED", m.Width, m.Height, ScalingName(m.Scaling));
            m.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
            return g_resizeOrig[n](sc, &m);
        }
        return g_resizeOrig[n](sc, mode);
    }
    template <int N> HRESULT STDMETHODCALLTYPE ResizeDetour(IDXGISwapChain* sc, const DXGI_MODE_DESC* mode)
    {
        return ResizeCommon(N, sc, mode);
    }

    // Detours slot 14 (ResizeTarget) of a swap chain's vtable, once per distinct function.
    void HookResizeTarget(IDXGISwapChain* sc)
    {
        void* target = (*reinterpret_cast<void***>(sc))[14];
        EnterCriticalSection(&g_lock);
        bool known = false;
        for (LONG i = 0; i < g_resizeCount; ++i) known = known || g_resizeTarget[i] == target;
        if (!known && g_resizeCount < kMax)
        {
            int n = g_resizeCount;
            ResizeTargetFn fn = n == 0 ? &ResizeDetour<0> : &ResizeDetour<1>;
            void* detour = reinterpret_cast<void*>(fn);
            MH_STATUS s = MH_CreateHook(target, detour, reinterpret_cast<void**>(&g_resizeOrig[n]));
            if (s == MH_OK) s = MH_EnableHook(target);
            if (s == MH_OK) { g_resizeTarget[n] = target; InterlockedIncrement(&g_resizeCount); }
            kp::Log("dxgi: ResizeTarget at %p %s", target, s == MH_OK ? "hooked" : MH_StatusToString(s));
        }
        LeaveCriticalSection(&g_lock);
    }

    HRESULT CreateCommon(int n, IDXGIFactory* f, IUnknown* dev, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** out)
    {
        DXGI_SWAP_CHAIN_DESC copy;
        DXGI_SWAP_CHAIN_DESC* use = desc;
        if (desc)
        {
            const DXGI_MODE_DESC& b = desc->BufferDesc;
            kp::Log("dxgi: CreateSwapChain [%d] %ux%u @ %u/%u format %d scaling %s, %s", n, b.Width, b.Height,
                    b.RefreshRate.Numerator, b.RefreshRate.Denominator, (int)b.Format, ScalingName(b.Scaling),
                    desc->Windowed ? "windowed" : "full screen");
            if (b.Scaling != DXGI_MODE_SCALING_UNSPECIFIED)
            {
                copy = *desc;
                copy.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
                use = &copy;
                kp::Log("dxgi: scaling set to UNSPECIFIED (with STRETCHED, DXGI picks a smaller scaled mode)");
            }
        }
        HRESULT hr = g_createOrig[n](f, dev, use, out);
        kp::Log("dxgi: CreateSwapChain [%d] -> 0x%08lx", n, (unsigned long)hr);
        if (SUCCEEDED(hr) && out && *out) HookResizeTarget(*out);
        return hr;
    }
    template <int N> HRESULT STDMETHODCALLTYPE CreateDetour(IDXGIFactory* f, IUnknown* dev, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** out)
    {
        return CreateCommon(N, f, dev, desc, out);
    }

    // Factory through one dxgi.dll module; detours its CreateSwapChain (slot 10) unless already done.
    void HookFactoryOf(HMODULE dxgi, const char* which)
    {
        wchar_t path[MAX_PATH] = L"?";
        GetModuleFileNameW(dxgi, path, MAX_PATH);
        typedef HRESULT (WINAPI* CreateFactoryFn)(REFIID, void**);
        CreateFactoryFn create = (CreateFactoryFn)GetProcAddress(dxgi, "CreateDXGIFactory1");
        REFIID iid = create ? __uuidof(IDXGIFactory1) : __uuidof(IDXGIFactory);
        if (!create) create = (CreateFactoryFn)GetProcAddress(dxgi, "CreateDXGIFactory");
        IDXGIFactory* f = NULL;
        if (!create || FAILED(create(iid, reinterpret_cast<void**>(&f))) || !f)
        {
            kp::Log("dxgi: no factory from %s %ls", which, path);
            return;
        }
        void* target = (*reinterpret_cast<void***>(f))[10];
        f->Release();
        for (int i = 0; i < g_createCount; ++i)
            if (g_createTarget[i] == target) { kp::Log("dxgi: %s %ls: CreateSwapChain %p already hooked", which, path, target); return; }
        if (g_createCount >= kMax) return;
        int n = g_createCount;
        CreateSwapChainFn fn = n == 0 ? &CreateDetour<0> : &CreateDetour<1>;
        void* detour = reinterpret_cast<void*>(fn);
        MH_STATUS s = MH_CreateHook(target, detour, reinterpret_cast<void**>(&g_createOrig[n]));
        if (s == MH_OK) s = MH_EnableHook(target);
        if (s == MH_OK) { g_createTarget[n] = target; ++g_createCount; }
        kp::Log("dxgi: %s %ls: CreateSwapChain at %p %s", which, path, target, s == MH_OK ? "hooked" : MH_StatusToString(s));
    }

    void InstallDxgiHooks()
    {
        if (InterlockedExchange(&g_installed, 1) != 0) return;
        HMODULE byName = GetModuleHandleW(L"dxgi.dll");   // what the render system imported
        wchar_t sys[MAX_PATH];
        GetSystemDirectoryW(sys, MAX_PATH);
        wcscat_s(sys, MAX_PATH, L"\\dxgi.dll");
        HMODULE windows = GetModuleHandleW(sys);           // loaded already if a wrapper is in use
        if (!byName && !windows) { kp::Log("dxgi: no dxgi.dll loaded at Root::initialise - display fix not armed"); return; }
        if (byName) HookFactoryOf(byName, "game's dxgi.dll");
        if (windows && windows != byName) HookFactoryOf(windows, "Windows' dxgi.dll");
    }

    void* RootInitialiseDetour(void* root, bool autoCreateWindow, const void* title, const void* caps)
    {
        kp::Log("dxgi: Ogre::Root::initialise(autoCreateWindow=%d) - arming the display fix", autoCreateWindow ? 1 : 0);
        InstallDxgiHooks();
        return g_rootInitOrig(root, autoCreateWindow, title, caps);
    }
}

namespace kp
{
    bool DxgiFixArm()
    {
        InitializeCriticalSection(&g_lock);
        HMODULE ogre = GetModuleHandleW(L"OgreMain_x64.dll");
        void* target = ogre ? (void*)GetProcAddress(ogre,
            "?initialise@Root@Ogre@@QEAAPEAVRenderWindow@2@_NAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@1@Z") : NULL;
        if (!target) { Log("dxgi: OgreMain_x64.dll has no Root::initialise export - display fix not armed"); return false; }
        MH_STATUS s = MH_CreateHook(target, (void*)&RootInitialiseDetour, reinterpret_cast<void**>(&g_rootInitOrig));
        if (s == MH_OK) s = MH_EnableHook(target);
        Log("dxgi: Ogre::Root::initialise at %p %s", target, s == MH_OK ? "hooked; DXGI hooks follow there" : MH_StatusToString(s));
        return s == MH_OK;
    }
}
