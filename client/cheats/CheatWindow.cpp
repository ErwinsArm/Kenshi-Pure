// KenshiPureCheats.dll: entry points (kpCheatStart, kpCheatFrame) and the cheat window, drawn with
// Kenshi's own GUI library MyGUI 3.2.3 (the game's MyGUIEngine_x64.dll; KenshiLib 0.5.0's headers and
// import library, declarations only) with skins from Kenshi's own layouts (data/gui/layout).
// Clicks only queue an action; the frame listener runs it on the game thread after the game's frame.
// Every MyGUI call runs inside __try (C++ exceptions included, /EHa): a failure closes the window
// for this run instead of reaching Kenshi. See docs/client.md "Cheat window".
// Built with the VC++ 2010 (v100) toolset; keep to C++ that compiler accepts.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include <MyGUI.h>

#include "CheatApi.h"
#include "CheatActions.h"
#include "KpHost.h"
#include "Log.h"
#include "Sites.h"

#define KP_CHEATS_VERSION "0.1.1-window"

namespace
{
    enum Action { A_NONE, A_HEAL, A_DAMAGE, A_KNOCKOUT, A_TELEPORT, A_SPAWN };

    bool g_on = false;
    enum WindowState { W_WAITING, W_SHOWN, W_FAILED };
    WindowState g_state = W_WAITING;
    ULONGLONG g_nextTry = 0, g_nextRefresh = 0;
    bool g_waitLogged = false;
    bool g_toggleDown = false;
    volatile int g_pending = A_NONE;   // set by a click (GUI callback), run by the frame listener

    MyGUI::Window* g_window = NULL;
    MyGUI::TextBox* g_charLine = NULL;
    MyGUI::ComboBox* g_item = NULL;
    MyGUI::EditBox* g_amount = NULL;
    MyGUI::EditBox* g_status = NULL;
    MyGUI::Button* g_buttons[6];
    char g_shownChar[96] = "?";
    char g_shownStatus[256] = "?";

    // Skins and layer from Kenshi's own layouts (Kenshi_EffectsPanel.layout and others).
    const char* const kSkinWindow = "Kenshi_WindowCX";
    const char* const kSkinButton = "Kenshi_Button1";
    const char* const kSkinText = "Kenshi_TextboxStandardText";
    const char* const kSkinCombo = "Kenshi_ComboBox";
    const char* const kSkinEdit = "Kenshi_EditBox";
    const char* const kSkinStatus = "Kenshi_WordWrap";
    const char* const kLayer = "Window";

    // ---------------------------------------------------------------- GUI callbacks (no game calls)

    void OnClick(MyGUI::Widget* w)
    {
        for (int i = 1; i <= A_SPAWN; ++i)
            if (g_buttons[i] == w) g_pending = i;
    }

    void OnWindowButton(MyGUI::Window* w, const std::string& name)
    {
        if (name == "close" && w) w->setVisible(false);
    }

    // ---------------------------------------------------------------- MyGUI work (wrapped below)

    // 1 ready, 0 not yet (GUI or Kenshi's skins not loaded), filled `missing` with the first absent name.
    __declspec(noinline) int GuiReady(char* missing, size_t missingSize)
    {
        if (!MyGUI::Gui::getInstancePtr() || !MyGUI::ResourceManager::getInstancePtr()) { _snprintf_s(missing, missingSize, _TRUNCATE, "MyGUI"); return 0; }
        MyGUI::LayerManager* layers = MyGUI::LayerManager::getInstancePtr();
        if (!layers || !layers->isExist(kLayer)) { _snprintf_s(missing, missingSize, _TRUNCATE, "layer %s", kLayer); return 0; }
        const char* const skins[] = { kSkinWindow, kSkinButton, kSkinText, kSkinCombo, kSkinEdit, kSkinStatus };
        for (size_t i = 0; i < sizeof(skins) / sizeof(skins[0]); ++i)
            if (!MyGUI::ResourceManager::getInstance().isExist(skins[i]))
            {
                _snprintf_s(missing, missingSize, _TRUNCATE, "skin %s", skins[i]);
                return 0;
            }
        return 1;
    }

    MyGUI::Button* AddButton(int action, const char* caption, int x, int y, int w, int h)
    {
        MyGUI::Button* b = g_window->createWidget<MyGUI::Button>(kSkinButton, MyGUI::IntCoord(x, y, w, h), MyGUI::Align::Default);
        b->setCaption(MyGUI::UString(caption));
        b->eventMouseButtonClick += MyGUI::newDelegate(OnClick);
        g_buttons[action] = b;
        return b;
    }

    MyGUI::TextBox* AddText(const char* caption, int x, int y, int w, int h)
    {
        MyGUI::TextBox* t = g_window->createWidget<MyGUI::TextBox>(kSkinText, MyGUI::IntCoord(x, y, w, h), MyGUI::Align::Default);
        t->setCaption(MyGUI::UString(caption));
        return t;
    }

    __declspec(noinline) void BuildWindow()
    {
        MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
        g_window = gui->createWidget<MyGUI::Window>(kSkinWindow, MyGUI::IntCoord(40, 140, 340, 380), MyGUI::Align::Default,
                                                    kLayer, "KenshiPureCheatWindow");
        g_window->setCaption(MyGUI::UString("KenshiPure cheats  (Ctrl+Shift+C: show/hide)"));
        g_window->eventWindowButtonPressed += MyGUI::newDelegate(OnWindowButton);

        g_charLine = AddText("Character: none selected", 10, 6, 300, 24);
        AddButton(A_HEAL, "Heal", 10, 36, 96, 30);
        AddButton(A_DAMAGE, "Damage", 112, 36, 96, 30);
        AddButton(A_KNOCKOUT, "Knock out", 214, 36, 96, 30);
        AddButton(A_TELEPORT, "Teleport to camera", 10, 72, 300, 30);
        AddText("Item (name or ID) and amount:", 10, 112, 300, 24);
        g_item = g_window->createWidget<MyGUI::ComboBox>(kSkinCombo, MyGUI::IntCoord(10, 138, 222, 28), MyGUI::Align::Default);
        for (int i = 0; i < kp::cheat::ItemCount(); ++i) g_item->addItem(MyGUI::UString(kp::cheat::ItemName(i)));
        g_item->setComboModeDrop(false);   // typing a name is allowed too
        if (kp::cheat::ItemCount() > 0) g_item->setIndexSelected(0);
        g_amount = g_window->createWidget<MyGUI::EditBox>(kSkinEdit, MyGUI::IntCoord(240, 138, 70, 28), MyGUI::Align::Default);
        g_amount->setOnlyText(MyGUI::UString("1"));
        AddButton(A_SPAWN, "Create item", 10, 172, 300, 30);
        AddText("Last result:", 10, 210, 300, 24);
        g_status = g_window->createWidget<MyGUI::EditBox>(kSkinStatus, MyGUI::IntCoord(10, 234, 300, 96), MyGUI::Align::Default);
        g_status->setEditStatic(true);
        g_status->setEditMultiLine(true);
        g_status->setEditWordWrap(true);
        g_status->setOnlyText(MyGUI::UString("Select one of your characters, then press a button."));
    }

    __declspec(noinline) void ToggleWindow()
    {
        g_window->setVisible(!g_window->getVisible());
    }

    __declspec(noinline) void Refresh(const char* charName, const char* status)
    {
        if (strcmp(charName, g_shownChar) != 0)
        {
            _snprintf_s(g_shownChar, sizeof(g_shownChar), _TRUNCATE, "%s", charName);
            std::string line = std::string("Character: ") + (charName[0] ? charName : "none selected");
            g_charLine->setCaption(MyGUI::UString(line));
        }
        if (strcmp(status, g_shownStatus) != 0)
        {
            _snprintf_s(g_shownStatus, sizeof(g_shownStatus), _TRUNCATE, "%s", status);
            if (status[0]) g_status->setOnlyText(MyGUI::UString(status));
        }
    }

    __declspec(noinline) void ReadItem(char* key, size_t keySize, int* count)
    {
        _snprintf_s(key, keySize, _TRUNCATE, "%s", g_item->getOnlyText().asUTF8_c_str());
        *count = atoi(g_amount->getOnlyText().asUTF8_c_str());
    }

    // ---------------------------------------------------------------- guarded wrappers

    void Failed(const char* what)
    {
        g_state = W_FAILED;
        kp::Log("window: %s failed (exception) - cheat window off for this run", what);
    }

    bool SafeGuiReady(char* missing, size_t n, int* ready)
    {
        __try { *ready = GuiReady(missing, n); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool SafeBuild()
    {
        __try { BuildWindow(); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool SafeToggle()
    {
        __try { ToggleWindow(); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    __declspec(noinline) void HideWindow()
    {
        g_window->setVisible(false);
    }

    bool SafeHide()
    {
        __try { HideWindow(); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool SafeRefresh(const char* charName, const char* status)
    {
        __try { Refresh(charName, status); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool SafeReadItem(char* key, size_t n, int* count)
    {
        __try { ReadItem(key, n, count); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void TryCreate()
    {
        ULONGLONG now = GetTickCount64();
        if (now < g_nextTry) return;
        g_nextTry = now + 1000;
        char missing[96] = "";
        int ready = 0;
        if (!SafeGuiReady(missing, sizeof(missing), &ready)) { Failed("checking the GUI"); return; }
        if (!ready)
        {
            if (!g_waitLogged) kp::Log("window: waiting for Kenshi's GUI (%s not there yet)", missing);
            g_waitLogged = true;
            return;
        }
        kp::Log("window: creating the cheat window (skin %s, layer %s)", kSkinWindow, kLayer);
        if (!SafeBuild()) { Failed("creating the window"); return; }
        g_state = W_SHOWN;
        kp::Log("window: shown");
    }

    bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

    bool GameInFront()
    {
        HWND w = GetForegroundWindow();
        DWORD pid = 0;
        if (w) GetWindowThreadProcessId(w, &pid);
        return pid == GetCurrentProcessId();
    }

    void RunPending(GameWorld* gw)
    {
        int a = g_pending;
        if (a == A_NONE) return;
        g_pending = A_NONE;
        switch (a)
        {
        case A_HEAL: kp::cheat::Heal(gw); break;
        case A_DAMAGE: kp::cheat::Damage(gw); break;
        case A_KNOCKOUT: kp::cheat::KnockOut(gw); break;
        case A_TELEPORT: kp::cheat::Teleport(gw); break;
        case A_SPAWN:
        {
            char key[96] = "";
            int count = 1;
            if (!SafeReadItem(key, sizeof(key), &count)) { Failed("reading the item fields"); return; }
            kp::cheat::Spawn(gw, key, count);
            break;
        }
        }
        g_nextRefresh = 0;   // show the result at once
    }
}

extern "C" __declspec(dllexport) int kpCheatStart(const KpCheatHost* host)
{
    kp::LogOpen(L"KenshiPureCheats");
    kp::Log("KenshiPureCheats " KP_CHEATS_VERSION " loaded (built " __DATE__ " " __TIME__ ")");
    if (!host || host->version != KP_CHEAT_API_VERSION || host->size < sizeof(KpCheatHost))
    {
        kp::Log("host: interface version %u - this DLL needs version %d; staying off", host ? host->version : 0, KP_CHEAT_API_VERSION);
        return 0;
    }
    if (!kp::SitesResolve(host->exePath, host->exeSha256) || !kp::SitesKnownBuild())
    {
        kp::Log("build: unknown build or a required address is unresolved - cheats off (they change the game)");
        return 0;
    }
    if (!kp::cheat::Init()) return 0;
    g_on = true;
    kp::Log("cheats: on; the window appears once Kenshi's GUI is loaded");
    return 1;
}

extern "C" __declspec(dllexport) void kpCheatFrame(GameWorld* gw)
{
    if (!g_on) return;
    kp::cheat::OnFrame(gw);
    if (g_state == W_WAITING) TryCreate();
    if (g_state != W_SHOWN) return;

    bool toggle = GameInFront() && Down(VK_CONTROL) && Down(VK_SHIFT) && !Down(VK_MENU) && Down('C');
    if (toggle && !g_toggleDown && !SafeToggle()) { Failed("show/hide"); return; }
    g_toggleDown = toggle;

    RunPending(gw);
    if (g_state != W_SHOWN) return;
    ULONGLONG now = GetTickCount64();
    if (now >= g_nextRefresh)
    {
        g_nextRefresh = now + 500;
        char name[96] = "";
        kp::cheat::SelectedName(gw, name, sizeof(name));
        if (!SafeRefresh(name, kp::cheat::LastResult())) Failed("updating the window");
    }
}

// Multiplayer (KenshiPure is connected to a server): no cheat runs for the rest of this game run. Drops a queued
// click, hides the window and turns kpCheatFrame into a no-op. Game thread.
extern "C" __declspec(dllexport) void kpCheatStop()
{
    if (!g_on) return;
    g_on = false;
    g_pending = A_NONE;
    if (g_state == W_SHOWN && !SafeHide()) g_state = W_FAILED;
    kp::Log("cheats: off for the rest of this game run (connected to a multiplayer server)");
}
