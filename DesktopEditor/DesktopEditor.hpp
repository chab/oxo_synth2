#pragma once

#include "public.sdk/source/common/pluginview.h"
#include "EditorCore/OxoSynth2Bridge.h"
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <optional>
#include <filesystem>
#include <functional>
#include "EditorCore/Themes.hpp"
#if defined(_WIN32)
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "WindowsModule.hpp"
#include "WindowsHttp.hpp"
#else
// GDI+ on cairo, and the Win32 names this editor calls (no X11 here).
#include "LinuxPlatform.hpp"
struct _XDisplay; // Xlib's Display
#endif

namespace oxo {
class OxoSynth2Controller;
class ThemeEditorWindow;
class TextCache;
#if !defined(_WIN32)
class LinuxRunLoopHandler;
namespace lx { class MenuTracker; } // LinuxX11.hpp
#endif

// The VST3 editor of the Windows and Linux builds (the mac's is SwiftUI):
// the original 770x560 SwiftUI design canvas redrawn with GDI+ (on Linux,
// GDI+ on cairo: Linux_Build/Source/LinuxGdiplus.hpp). It uses the existing
// SynthEditorCore bridge; the DSP and parameter layout belong exclusively
// to the original Synth2 project.
//   DesktopEditor.cpp  the panel: layout, drawing, browser, menus, input
//   Windows_Build/Source/DesktopEditor.cpp  its Win32 window
//   Linux_Build/Source/LinuxEditor.cpp      its X11 window
// Each platform file defines the "platform" members below.
class DesktopEditor final : public Steinberg::CPluginView {
public:
    explicit DesktopEditor(OxoSynth2Controller* controller);
    ~DesktopEditor() override;
    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override;
    Steinberg::tresult PLUGIN_API canResize() override { return Steinberg::kResultTrue; }
    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* r) override;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* r) override;
    void attachedToParent() override;
    void removedFromParent() override;
    // Keys from hosts that deliver them to the view (else the window's own).
    Steinberg::tresult PLUGIN_API onKeyDown(Steinberg::char16 key, Steinberg::int16 keyCode,
                                            Steinberg::int16 modifiers) override;
#if !defined(_WIN32)
    // The host's Linux::IRunLoop: X events, the 50 ms timer.
    void onEvents();
    void onTimer();
#endif

private:
    enum class Kind { Knob, Toggle, Cycle, Display, Wave, Radio, Badge, Label };
    struct Control {
        Kind kind;
        int id, value;
        float x, y, w, h;
        std::wstring label;
        int symbol = 0;
    };
    struct Bank {
        std::string name;
        std::string source;
        bool readOnly = false;
        std::vector<std::string> slots;
        std::wstring wideName; // for the natural sort
    };
    void constructLayout();
    void knob(int id, const wchar_t* label, float sx, float sy, float x, float y, float size = 24);
    void button(int id, const wchar_t* label, Kind kind, float sx, float sy, float x, float y, float width = 40);
    void display(int id, float sx, float sy, float x, float y, float width = 30);
    void radio(int id, float sx, float sy, float x, float y, std::vector<std::wstring> choices,
               int first = 0, bool horizontal = false, bool trailing = false);
    void waves(int id, float sx, float sy, float x, float y, const std::vector<int>& values,
               const std::vector<int>& shapes);
    void label(float x, float y, const wchar_t* text);
    void badge(float x, float y, const wchar_t* text, int id = -1);
    void text(Gdiplus::Graphics& g, const std::wstring& s, float x, float y, float w, float h, float size,
              Gdiplus::Color c, bool bold = false, bool centred = false, bool mono = false);
    bool visible(const Gdiplus::RectF& design) const;
    static Gdiplus::RectF bounds(const Control& c);
    static Gdiplus::RectF chromeRect(int id);
    void invalidate(const Gdiplus::RectF& design) const;
    void invalidateParam(int id) const;
    void valueChanged(int id);
    void hoverChanged(int control, int chrome);
    void drawStatic(Gdiplus::Graphics& g);
    void drawDynamic(Gdiplus::Graphics& g);
    void drawControlStatic(Gdiplus::Graphics& g, const Control& c);
    void drawControlDynamic(Gdiplus::Graphics& g, const Control& c, bool hovered);
    void drawWave(Gdiplus::Graphics& g, const Control& c);
    // The in-panel type pick list (SynthPanelView's TypeMenu): effect /
    // delay / chorus type, LFO shapes; typeMenu_ = its parameter, -1 = closed.
    struct TypeMenu {
        float x = 0, y = 0, cellW = 0, cellH = 17;
        int rows = 1, columns = 0;
        std::vector<int> values; // column by column
        Gdiplus::RectF rect() const { return Gdiplus::RectF(x, y, cellW * float(columns), cellH * float(rows)); }
    };
    int typeMenu_ = -1;
    int typeMenuHover_ = -1; // the value under the mouse
    TypeMenu typeMenu() const;
    int typeMenuHit(float x, float y) const;
    void openTypeMenu(int id);
    void closeTypeMenu();
    void invalidateTypeMenu() const;
    void drawTypeMenu(Gdiplus::Graphics& g);
    void drawChrome(Gdiplus::Graphics& g, int id, bool hovered);
    void drawBrowser(Gdiplus::Graphics& g);
    void showMainMenu(int x, int y);
    void showThemeMenu(int x, int y);
    void checkForUpdates();
    void updateArrived(); // releases.json came (or not)
    void appendThemes(HMENU menu, std::vector<themes::Palette>& picks) const;
    void appendSizes(HMENU menu, const wchar_t* suffix) const;
    bool pickSize(int command);
    bool pickTheme(int command, const std::vector<themes::Palette>& picks);
    void setTheme(const themes::Palette& palette, bool fromThemeEditor = false);
    void syncTheme();
    void showParameterMenu(int id, int x, int y);
    void stepPatch(int direction);
    void showFileDialog(bool save);
    void showImportDialog(int screenX, int screenY);
    void refreshBanks();
    bool isVersionBox(float x, float y) const;
    float versionLeft_ = 593.f; // the version box's left edge, sized to its text in drawStatic
    int displayBankNumber(int bank) const;
    int bankRowOf(int bank) const;       // its row in the bank list, -1 = not listed
    void scrollBankIntoView(int bank);
    int bankBySource(const std::string& source) const; // -1 = none
    int bankNow(const std::string& source); // the same, after adopting a newer scan
    void adoptLibrary();
    std::wstring slotText(int bank, int slot) const; // "NNN:name"
    void setValueOnce(int id, int value);
    void cycle(int id, int step);
    void mouseDown(float x, float y, bool doubleClick);
    void mouseMove(float x, float y);
    void updateHover(float x, float y);
    void mouseUp();
    void wheel(int direction, float x, float y);
    int wheelTarget(float x, float y) const;
    // The current wheel run's owner (wheelTarget at its first event) and its
    // last event's time: see wheel().
    int wheelOwner_ = -1;
    uint64_t wheelLastAt_ = 0;
    void browserClick(float x, float y, bool doubleClick);
    void slotClick(int index, bool doubleClick);
    void commitWrite();
    bool keyDown(UINT key); // true = handled
    void loadSlot(int bank, int slot);
    void dragMove(float x, float y);
    void dragEnd(bool drop); // false = cancelled
    Gdiplus::RectF dragLabelRect() const;
    HCURSOR dragCursor();
    void openBrowser(bool writing);
    void closeBrowser();
    int bankRowAt(float x, float y) const; // bank (or kNewUserBank) under a point, -1 = none
    int slotAt(float x, float y) const;    // slot of the shown bank under a point, -1 = none
    bool showLibraryMenu(float x, float y, int screenX, int screenY); // false = not over a row
    void buildLearnRects();
    void setLearn(bool on);
    void learnSelect(int id);
    int learnHit(float x, float y) const; // hotspot under a point, -1 = none
    void drawLearn(Gdiplus::Graphics& g);
    void requestWidth(int width);
    void tick();
    int value(int id) const;
    std::wstring formatted(int id) const;
    std::wstring patchName() const;
    void show(int id);
    // SynthModel.flash: a timed message on LCD line 2 (instead of the
    // bank); an empty text clears it.
    void flash(const std::wstring& text, unsigned milliseconds = 1500);
    static themes::Store& themeStore();                   // the user themes (themes.json)
    static std::vector<themes::Named> themeFiles();       // the Themes folder's

    // Design canvas and UI sizes.
    static constexpr float kDesignWidth = 770.f, kDesignHeight = 560.f;
    static constexpr int kMinWidth = 578;  // 75% of the 770-pixel design width, rounded up
    static constexpr int kMaxWidth = 2310; // 300% of the design width
    static constexpr uint64_t kSizeSettleMs = 1000, kSizeSaveDelayMs = 400; // host resizes (sizeSettledAt_)

    // ---- platform (Windows_Build/Source/DesktopEditor.cpp, Linux_Build/Source/LinuxEditor.cpp) ----
    static std::wstring toWide(const char* utf8);
    static std::string toUtf8(const std::wstring& wide);
    bool startGraphics();      // constructor: false = no TextCache (nothing drawn)
    void endGraphics();        // destructor, after the TextCache
    bool openWindow();         // attachedToParent: the child window and the 50 ms timer
    void closeWindow();        // removedFromParent: window, timer, buffers, name field
    void layoutChild();        // after a resize
    void repaint() const;      // everything
    void invalidateDevice(int left, int top, int right, int bottom) const; // device pixels
    void updateEditControl();  // the write dialog's name field follows browser_ / writing_
    // The keyboard: the panel takes it only for the browser (openBrowser, a
    // click while it is open); closing it, or a key that is not ours, gives
    // it back to the host (else the host's shortcuts, e.g. the space bar,
    // stop working).
    void takeKeyFocus();
    void releaseKeyFocus();
    HWND hostFocusWindow() const; // the keyboard's owner before us, else the host's window
    std::optional<std::wstring> nameFieldText() const; // nullopt = no field
    // Keys the host delivers (onKeyDown) that belong to the name field: true = taken.
    bool nameFieldKey(Steinberg::char16 key, Steinberg::int16 keyCode, Steinberg::int16 modifiers);
    HCURSOR copyCursor();      // the drag cursor over a bank row: an arrow with a "+"
    // Menus and dialogs answer through `then`: on Windows at once, after the
    // system's modal loop (it keeps the host's windows running); on Linux
    // later, from the host's run loop, which goes on meanwhile (a loop of
    // ours there made the desktop call the host "not responding"). So a
    // `then` keeps what it needs by value; it never runs once the window
    // is closed. askText / chooseFile / pickFolder call it only on OK.
    void popupMenu(HMENU menu, int screenX, int screenY, std::function<void(int)> then); // takes the menu; 0 = none chosen
    void messageBox(const std::wstring& text, const wchar_t* title, UINT flags, std::function<void(int)> then = {});
    void askText(const wchar_t* title, const wchar_t* label, const std::wstring& text,
                 std::function<void(const std::wstring&)> then);
    enum class FileDialog { ImportFile, OpenPatch, SavePatch };
    void chooseFile(FileDialog kind, std::function<void(const std::wstring&)> then);
    void revealInExplorer(const std::string& utf8Path) const;
    void pickFolder(const wchar_t* title, std::function<void(const std::wstring&)> then);
    bool startUpdateCheck(const std::string& url); // false = one is running
    static const char* const kUpdatePlatform;     // its key in releases.json: "win" / "linux"
    void showThemeEditor();
    void refreshThemeEditor(); // the palette changed elsewhere
    void closeThemeEditor();
    struct ThemeEditorDeleter { void operator()(ThemeEditorWindow* w) const; }; // in the platform file

    OxoSynth2Controller* controller_ = nullptr;
    OxoSynth2Hooks hooks_{};
    HWND window_{}; // the editor's child window (Win32 / X11)
    HWND focusBefore_{}; // who had the keyboard before takeKeyFocus (none = unknown)
    std::vector<Control> controls_;
    themes::Palette theme_ = themes::Synth2(); // this instance's palette (kept in the session)
    std::array<Gdiplus::Color, themes::kSlotCount> colors_; // theme_ as GDI+ colours
    std::unique_ptr<ThemeEditorWindow, ThemeEditorDeleter> themeEditor_;
    std::unique_ptr<TextCache> textCache_; // GDI+ objects: lives between startGraphics / endGraphics
    bool staticDirty_ = true;
    Gdiplus::RectF paintClip_{0, 0, 770, 560}; // design-space area being painted
    std::array<int, 100> knownValues_{};        // params 0..99 as last drawn (host automation polling)
    bool browser_ = false;
    bool writing_ = false;
    bool midiLearn_ = false;
    int learnTarget_ = -1;
    int learnPrevTarget_ = -1; // before a click: a double click restores it (as the mac's double tap)
    // MIDI learn hotspots (SynthControls' .learnable): one per patch param,
    // around all its controls; empty = not learnable.
    static constexpr int kLearnCount = 99; // patch params (SynthModel.paramCount)
    std::array<Gdiplus::RectF, kLearnCount> learnRects_{};
    std::vector<int> learnOrder_;          // learnable ids, larger hotspots first
    std::array<int, kLearnCount> learnCC_{}; // learned CC per param (0 = none), read on entering learn
    int lastCcToken_ = 0;
    int dragId_ = -1;
    float dragStartY_ = 0;
    int dragStartValue_ = 0;
#if defined(_WIN32)
    win::HttpGet updateCheck_; // "Check for updates": releases.json in the background
#else
    lx::HttpGet updateCheck_;
#endif
    std::wstring flashText_;
    ULONGLONG flashUntil_ = 0; // GetTickCount64 deadline, 0 = no flash
    // Write dialog: the proposed "User" bank the library lacks (a last row;
    // the write creates the folder). Never passed to a hook.
    static constexpr int kNewUserBank = -3;
    int userBank() const;           // the library's "User" bank, -1 = none
    bool offersNewUserBank() const; // write dialog shows the kNewUserBank row
    int bankRowCount() const;       // browser bank rows (+ new User)
    int selectedBank_ = -1;
    int selectedSlot_ = -1;
    int bankScroll_ = 0;
    int slotScroll_ = 0;
    int knownRevision_ = -1;
    int knownSerial_ = -1;
    uint64_t knownVoiceStates_ = 0;
    int hoveredControl_ = -1;
    int hoveredChrome_ = 0;
    bool resizeDragging_ = false;
    POINT resizeStartScreen_{};
    int resizeStartWidth_ = 0;
    int requestedZoomWidth_ = -1; // an editor-initiated resize (menu): saved at once
    // Host resizes (its window frame dragged, e.g. FL Studio) count only once
    // the editor has settled (attach + kSizeSettleMs: the opening onSize
    // calls are the host's own default, not the user's) and are saved once
    // they stop (kSizeSaveDelayMs, or on close).
    uint64_t sizeSettledAt_ = 0;
    int pendingSizeSave_ = -1;
    uint64_t pendingSizeSaveAt_ = 0;
    void savePendingSize(bool now);
    std::vector<Bank> banks_;
    std::vector<int> bankOrder_;
    std::string libraryRoot_;
    // A press on a patch in the browser: a click on release, or a drag.
    struct SlotPress {
        bool active = false, dragging = false;
        int bank = -1, slot = -1;
        std::string source;  // the bank's: found again after a rescan
        POINT start{};       // screen, for the drag threshold
        float x = 0, y = 0;  // the pointer, design coordinates
        std::wstring label;  // "NNN:name", drawn under the pointer
    };
    SlotPress press_;
    int dropSlot_ = -1, dropBank_ = -1; // the drag's move / copy target
    int dragFullBank_ = -1;             // a full bank under the drag (refused, said once)
    mutable int loadedBankForPaint_ = -1;
    mutable int loadedSlotForPaint_ = -1;

#if defined(_WIN32)
    // ---- Windows only (DesktopEditor.cpp) ----
    void paint(HDC screen, const RECT& dirty);
    bool ensureBuffers(HDC screen, int width, int height);
    void releaseBuffers();
    LRESULT windowMessage(UINT message, WPARAM w, LPARAM l);
    static LRESULT CALLBACK wndProc(HWND window, UINT message, WPARAM w, LPARAM l);
    static win::WindowClass& windowClass();
    static LRESULT CALLBACK nameFieldProc(HWND field, UINT message, WPARAM w, LPARAM l);
    bool hasKeyFocus() const;      // the panel or its name field
    void noteFocusFrom(HWND from); // WM_SETFOCUS: `from` lost it (focusBefore_ unless ours)
    static constexpr UINT kUpdateMessage = WM_APP + 1;
    static constexpr UINT kFieldKeyMessage = WM_APP + 2; // name field: Return / Esc (wParam)
    HWND editName_ = nullptr;
    ULONG_PTR gdiplusToken_ = 0;
    // Persistent bitmaps, recreated only on resize: the static layer and the back buffer.
    struct Buffer { HDC dc = nullptr; HBITMAP bitmap = nullptr; HGDIOBJ previous = nullptr; };
    Buffer static_, back_;
    int bufferWidth_ = 0, bufferHeight_ = 0;
    bool trackingMouseLeave_ = false;
    HCURSOR copyCursor_ = nullptr; // made on first use
#else
    // ---- Linux only (LinuxEditor.cpp) ----
    // The Win32 calls that need this editor's window (LinuxPlatform.hpp has the others).
    void GetCursorPos(POINT* p) const { p->x = rootX_; p->y = rootY_; }
    short GetKeyState(UINT key) const;
    void SetCursor(HCURSOR c);
    void SetCapture(HWND) {} // X: the pointer stays ours while a button is held
    void ReleaseCapture() {}
    HWND GetCapture() const { return window_; }

    void paint();
    void handleEvent(void* xevent, bool input); // an XEvent*; input = false while a menu / dialog is open
    bool modal() const;                         // a menu or a dialog is open: the panel's input waits
    void finishMenu();                          // the menu closed: its `then`
    void drawNameField(Gdiplus::Graphics& g);
    bool nameFieldEdit(unsigned long keysym, const std::string& utf8, bool ctrl); // true = taken
    ::_XDisplay* display_ = nullptr;
    Steinberg::Linux::IRunLoop* runLoop_ = nullptr;
    LinuxRunLoopHandler* handler_ = nullptr;
    cairo_region_t* dirty_ = nullptr;     // device pixels waiting for a paint
    cairo_surface_t* windowSurface_ = nullptr;
    cairo_surface_t* static_ = nullptr;   // the static layer
    cairo_surface_t* back_ = nullptr;     // the back buffer
    int bufferWidth_ = 0, bufferHeight_ = 0;
    std::array<unsigned long, 5> cursors_{};    // X cursors, by HCURSOR
    HCURSOR cursorShown_ = IDC_ARROW;
    long rootX_ = 0, rootY_ = 0;                // the pointer, screen pixels (GetCursorPos)
    unsigned modifiers_ = 0;                    // X modifier state of the last input event
    unsigned long lastClickTime_ = 0;
    int lastClickX_ = -100, lastClickY_ = -100;
    bool lastWasDouble_ = false;
    struct MenuDeleter { void operator()(lx::MenuTracker* m) const; };
    std::unique_ptr<lx::MenuTracker, MenuDeleter> menu_; // the open popup menu
    std::function<void(int)> menuThen_;
    // The write dialog's name field (Windows: a native EDIT control), drawn in the panel.
    bool nameFieldOpen_ = false;
    std::wstring nameText_;
    size_t nameCaret_ = 0;
    bool nameAllSelected_ = false;
    mutable lx::Dialogs dialogs_;
#endif
};
}
