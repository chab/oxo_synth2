#pragma once

#include "public.sdk/source/common/pluginview.h"
#include "EditorCore/OxoSynth2Bridge.h"
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <filesystem>
#include "EditorCore/Themes.hpp"
#include "WindowsModule.hpp"
#include "WindowsHttp.hpp"

namespace oxo {
class OxoSynth2Controller;
class ThemeEditorWindow;
class TextCache;

// Windows-only implementation of the original 770x560 SwiftUI design canvas.
// It uses the existing SynthEditorCore bridge; the DSP and parameter layout
// belong exclusively to the original Synth2 project.
class WindowsEditor final : public Steinberg::CPluginView {
public:
    explicit WindowsEditor(OxoSynth2Controller* controller);
    ~WindowsEditor() override;
    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override;
    Steinberg::tresult PLUGIN_API canResize() override { return Steinberg::kResultTrue; }
    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* r) override;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* r) override;
    void attachedToParent() override;
    void removedFromParent() override;
    // Keys from hosts that deliver them to the view (else WM_KEYDOWN).
    Steinberg::tresult PLUGIN_API onKeyDown(Steinberg::char16 key, Steinberg::int16 keyCode,
                                            Steinberg::int16 modifiers) override;

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
    void drawChrome(Gdiplus::Graphics& g, int id, bool hovered);
    void drawBrowser(Gdiplus::Graphics& g);
    void paint(HDC screen, const RECT& dirty);
    bool ensureBuffers(HDC screen, int width, int height);
    void releaseBuffers();
    void showMainMenu(int x, int y);
    void showThemeMenu(int x, int y);
    void showThemeEditor();
    void checkForUpdates();
    void updateArrived(); // kUpdateMessage: releases.json came (or not)
    void appendThemes(HMENU menu, std::vector<themes::Palette>& picks) const;
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
    void browserClick(float x, float y, bool doubleClick);
    void slotClick(int index, bool doubleClick);
    void commitWrite();
    bool keyDown(UINT key); // true = handled
    void loadSlot(int bank, int slot);
    static LRESULT CALLBACK nameFieldProc(HWND field, UINT message, WPARAM w, LPARAM l);
    static constexpr UINT kFieldKeyMessage = WM_APP + 2; // name field: Return / Esc (wParam)
    void dragMove(float x, float y);
    void dragEnd(bool drop); // false = cancelled
    Gdiplus::RectF dragLabelRect() const;
    HCURSOR dragCursor();
    void openBrowser(bool writing);
    void closeBrowser();
    int bankRowAt(float x, float y) const; // bank (or kNewUserBank) under a point, -1 = none
    int slotAt(float x, float y) const;    // slot of the shown bank under a point, -1 = none
    bool showLibraryMenu(float x, float y, int screenX, int screenY); // false = not over a row
    void revealInExplorer(const std::string& utf8Path) const;
    bool pickFolder(const wchar_t* title, std::wstring& folder) const;
    void buildLearnRects();
    void setLearn(bool on);
    void learnSelect(int id);
    int learnHit(float x, float y) const; // hotspot under a point, -1 = none
    void drawLearn(Gdiplus::Graphics& g);
    void requestWidth(int width);
    void updateEditControl();
    void layoutChild();
    void tick();
    LRESULT windowMessage(UINT message, WPARAM w, LPARAM l);
    static LRESULT CALLBACK wndProc(HWND window, UINT message, WPARAM w, LPARAM l);
    static win::WindowClass& windowClass();
    void repaint() const;
    int value(int id) const;
    std::wstring formatted(int id) const;
    std::wstring patchName() const;
    void show(int id);
    // SynthModel.flash: a timed message on LCD line 2 (instead of the
    // bank); an empty text clears it.
    void flash(const std::wstring& text, unsigned milliseconds = 1500);
    static std::wstring toWide(const char* utf8);
    static std::string toUtf8(const std::wstring& wide);

    OxoSynth2Controller* controller_ = nullptr;
    OxoSynth2Hooks hooks_{};
    HWND window_ = nullptr;
    HWND editName_ = nullptr;
    ULONG_PTR gdiplusToken_ = 0;
    std::vector<Control> controls_;
    themes::Palette theme_ = themes::Synth2(); // this instance's palette (kept in the session)
    std::array<Gdiplus::Color, themes::kSlotCount> colors_; // theme_ as GDI+ colours
    std::unique_ptr<ThemeEditorWindow> themeEditor_;
    std::unique_ptr<TextCache> textCache_; // GDI+ objects: lives between GdiplusStartup / Shutdown
    // Persistent bitmaps, recreated only on resize: the static layer and the back buffer.
    struct Buffer { HDC dc = nullptr; HBITMAP bitmap = nullptr; HGDIOBJ previous = nullptr; };
    Buffer static_, back_;
    int bufferWidth_ = 0, bufferHeight_ = 0;
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
    static constexpr UINT kUpdateMessage = WM_APP + 1;
    win::HttpGet updateCheck_; // "Check for updates": releases.json in the background
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
    bool trackingMouseLeave_ = false;
    bool resizeDragging_ = false;
    POINT resizeStartScreen_{};
    int resizeStartWidth_ = 0;
    int requestedZoomWidth_ = -1; // accept/persist only editor-initiated resizes
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
    HCURSOR copyCursor_ = nullptr;      // made on first use
    mutable int loadedBankForPaint_ = -1;
    mutable int loadedSlotForPaint_ = -1;
};
}
