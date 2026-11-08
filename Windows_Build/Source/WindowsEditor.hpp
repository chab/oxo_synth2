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
#include "WindowsFavourites.hpp"

namespace oxo {
class OxoSynth2Controller;
class ThemeEditorWindow;

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
    };
    struct Palette {
        std::array<Gdiplus::Color, 17> c;
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
    void draw(Gdiplus::Graphics& g);
    void drawControl(Gdiplus::Graphics& g, const Control& c, bool hovered);
    void drawBrowser(Gdiplus::Graphics& g);
    void showMainMenu(int x, int y);
    void showThemeMenu(int x, int y);
    void showThemeEditor();
    void previewTheme(int index, const std::array<std::uint32_t,17>& colors);
    void showParameterMenu(int id, int x, int y);
    void stepPatch(int direction);
    void showFileDialog(bool save);
    void showImportDialog(int screenX, int screenY);
    void refreshBanks();
    void toggleFavourite();
    void showAbout();
    int displayBankNumber(int bank) const;
    std::string bankKey(int bank) const;
    bool resolveFavourite(const winfav::Item& favourite,int& bank,int& slot) const;
    bool isFavourite(int bank,int slot) const;
    void setValueOnce(int id, int value);
    void cycle(int id, int step);
    void mouseDown(float x, float y, bool doubleClick);
    void mouseMove(float x, float y);
    void updateHover(float x, float y);
    void mouseUp();
    void wheel(int direction, float x, float y);
    void browserClick(float x, float y, bool doubleClick);
    void openBrowser(bool writing);
    void closeBrowser();
    void applyTheme(int index);
    bool selectTheme(int index);
    void readTheme();
    void requestWidth(int width);
    void updateEditControl();
    void layoutChild();
    void tick();
    LRESULT windowMessage(UINT message, WPARAM w, LPARAM l);
    static LRESULT CALLBACK wndProc(HWND window, UINT message, WPARAM w, LPARAM l);
    void repaint() const;
    int value(int id) const;
    std::wstring formatted(int id) const;
    std::wstring patchName() const;
    std::wstring readout() const;
    static std::wstring toWide(const char* utf8);
    static std::string toUtf8(const std::wstring& wide);

    OxoSynth2Controller* controller_ = nullptr;
    OxoSynth2Hooks hooks_{};
    HWND window_ = nullptr;
    HWND editName_ = nullptr;
    ULONG_PTR gdiplusToken_ = 0;
    std::vector<Control> controls_;
    Palette palette_{};
    int currentThemeIndex_ = 0;
    std::unique_ptr<ThemeEditorWindow> themeEditor_;
    bool browser_ = false;
    bool writing_ = false;
    bool midiLearn_ = false;
    int learnTarget_ = -1;
    int lastCcToken_ = 0;
    int dragId_ = -1;
    float dragStartY_ = 0;
    int dragStartValue_ = 0;
    int selectedId_ = -1;
    static constexpr int kFavouritesBank = -2;
    int selectedBank_ = -1;
    int selectedSlot_ = -1;
    int bankScroll_ = 0;
    int slotScroll_ = 0;
    int knownRevision_ = -1;
    int knownSerial_ = -1;
    uint64_t knownVoiceStates_ = 0;
    int knownGridMode_ = -1;
    int knownGridPolyphony_ = -1;
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
    std::vector<winfav::Item> favourites_;
    bool favouritesReadable_ = true;
    mutable int loadedBankForPaint_ = -1;
    mutable int loadedSlotForPaint_ = -1;
};
}
