#pragma once
// Linux, modeless theme editor: WindowsThemeEditor.hpp (itself the Mac
// ThemeDialog) as a small X11 window drawn with cairo, same layout and
// behaviour. Load (factory, then user themes, then theme files) and every
// colour edit apply LIVE to the editor that opened it; Save stores the
// current colours under a name, Delete removes a saved user theme (both in
// themes.json, EditorCore/Themes.hpp). Theme files are listed last and
// only read. Colours: a hex field (applies once complete) or the swatch
// (the desktop's colour picker: zenity / kdialog).
#include "EditorCore/Themes.hpp"
#include "LinuxX11.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace oxo {
class ThemeEditorWindow {
public:
    struct Host {
        std::function<themes::Palette()> theme;
        std::function<void(const themes::Palette &)> apply;
        std::function<themes::Store &()> store;
        std::function<std::vector<themes::Named>()> fileThemes;
        // the editor's popup menu (it takes the menu; `then` gets the choice, 0 = none)
        std::function<void(lx::Menu *, int, int, std::function<void(int)>)> track;
        lx::Dialogs *dialogs = nullptr;
    };
    ThemeEditorWindow() = default;
    ThemeEditorWindow(const ThemeEditorWindow &) = delete;
    ThemeEditorWindow &operator=(const ThemeEditorWindow &) = delete;
    ~ThemeEditorWindow() { Close(); }

    void Show(Display *d, Window owner, Host host) {
        host_ = std::move(host);
        if (win_) { Refresh(); XMapRaised(d_, win_); XFlush(d_); return; }
        d_ = d;
        XSetWindowAttributes a{};
        a.event_mask = ExposureMask | ButtonPressMask | KeyPressMask | StructureNotifyMask;
        win_ = XCreateWindow(d_, DefaultRootWindow(d_), 80, 80, kWidth, kHeight, 0, CopyFromParent, InputOutput,
                             CopyFromParent, CWEventMask, &a);
        XStoreName(d_, win_, "Synth2 Themes");
        XSizeHints *hints = XAllocSizeHints();
        hints->flags = PMinSize | PMaxSize;
        hints->min_width = hints->max_width = kWidth;
        hints->min_height = hints->max_height = kHeight;
        XSetWMNormalHints(d_, win_, hints);
        XFree(hints);
        if (const Window top = Toplevel(owner)) XSetTransientForHint(d_, win_, top);
        Atom type = XInternAtom(d_, "_NET_WM_WINDOW_TYPE", False);
        Atom dialog = XInternAtom(d_, "_NET_WM_WINDOW_TYPE_DIALOG", False);
        XChangeProperty(d_, win_, type, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char *>(&dialog), 1);
        deleteAtom_ = XInternAtom(d_, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(d_, win_, &deleteAtom_, 1);
        surface_ = cairo_xlib_surface_create(d_, win_, DefaultVisual(d_, DefaultScreen(d_)), kWidth, kHeight);
        name_.clear();
        focus_ = -1;
        Populate();
        RebuildList();
        XMapRaised(d_, win_);
        XFlush(d_);
    }
    void Close() {
        if (!win_) return;
        cairo_surface_destroy(surface_);
        surface_ = nullptr;
        XDestroyWindow(d_, win_);
        XFlush(d_);
        win_ = 0;
    }
    void Refresh() {
        if (!win_) return;
        Populate();
        SelectCurrent();
        Paint();
    }
    bool Owns(const XEvent &e) const { return win_ && e.xany.window == win_; }
    void Handle(XEvent &e) {
        switch (e.type) {
        case Expose: if (e.xexpose.count == 0) Paint(); break;
        case ClientMessage:
            if (Atom(e.xclient.data.l[0]) == deleteAtom_) Close();
            break;
        case ButtonPress: if (e.xbutton.button == Button1) Click(e.xbutton.x, e.xbutton.y, e.xbutton.x_root, e.xbutton.y_root); break;
        case KeyPress: Key(e.xkey); break;
        default: break;
        }
    }

private:
    static constexpr int kWidth = 360, kRowHeight = 30, kRowsTop = 86,
                         kHeight = kRowsTop + themes::kSlotCount * kRowHeight + 10;
    struct Box { int x, y, w, h; bool in(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; } };
    static constexpr Box kLoadBox{64, 11, 200, 24}, kDeleteBox{274, 10, 74, 24}, kNameBox{64, 46, 200, 22},
                         kSaveBox{274, 45, 74, 24};
    static Box HexBox(size_t i) { return {140, kRowsTop + int(i) * kRowHeight, 90, 22}; }
    static Box SwatchBox(size_t i) { return {240, kRowsTop + int(i) * kRowHeight - 1, 60, 24}; }

    Window Toplevel(Window w) const {
        Window root = 0, parent = 0, *children = nullptr;
        unsigned n = 0;
        while (w) {
            if (!XQueryTree(d_, w, &root, &parent, &children, &n)) return 0;
            if (children) XFree(children);
            if (parent == root || !parent) return w;
            w = parent;
        }
        return 0;
    }
    void RebuildList() {
        entries_ = themes::Factory();
        userBegin_ = entries_.size();
        for (auto &t : host_.store().UserThemes()) entries_.push_back(std::move(t));
        userEnd_ = entries_.size();
        if (host_.fileThemes) for (auto &t : host_.fileThemes()) entries_.push_back(std::move(t));
        SelectCurrent();
        Paint();
    }
    bool IsUser(int index) const { return index >= int(userBegin_) && index < int(userEnd_); }
    void SelectCurrent() {
        const themes::Palette p = host_.theme();
        current_ = -1;
        for (size_t i = 0; i < entries_.size(); ++i) if (entries_[i].palette == p) { current_ = int(i); break; }
    }
    void Populate() {
        const themes::Palette p = host_.theme();
        for (size_t i = 0; i < themes::kSlotCount; ++i) hex_[i] = themes::Hex(p[i]);
    }
    void Edit(size_t slot, uint32_t rgb) {
        themes::Palette p = host_.theme();
        if (p[slot] == rgb) return;
        p[slot] = rgb;
        host_.apply(p);
        SelectCurrent();
    }
    std::string &Field(int f) { return f == 0 ? name_ : hex_[size_t(f - 1)]; }
    // field 0 = name, 1..kSlotCount = hex
    void FieldChanged(int f) {
        if (f < 1) return;
        uint32_t rgb = 0; // a complete colour applies at once, an unfinished one waits
        if (themes::ParseHex(hex_[size_t(f - 1)], rgb)) Edit(size_t(f - 1), rgb);
    }
    void Click(int x, int y, int rx, int ry) {
        focus_ = -1;
        if (kLoadBox.in(x, y) && !entries_.empty() && host_.track) {
            auto *menu = new lx::Menu;
            for (size_t i = 0; i < entries_.size(); ++i) {
                const std::string name = entries_[i].name + (i == 0 ? " (default)" : "");
                menu->items.push_back({lx::Wide(name.c_str()), int(i) + 1, int(i) == current_ ? unsigned(lx::MF_CHECKED) : 0u, nullptr});
            }
            // the list as shown: entries_ may be rebuilt before the answer
            host_.track(menu, rx - x + kLoadBox.x, ry - y + kLoadBox.y + kLoadBox.h, [this, list = entries_](int choice) {
                if (!win_ || choice <= 0 || choice > int(list.size())) return;
                const auto &e = list[size_t(choice - 1)];
                host_.apply(e.palette);
                name_ = e.name;
                Populate();
                SelectCurrent();
                Paint();
            });
        } else if (kDeleteBox.in(x, y)) {
            if (IsUser(current_)) { host_.store().Delete(entries_[size_t(current_)].name); RebuildList(); }
        } else if (kSaveBox.in(x, y)) {
            if (host_.store().Save(host_.theme(), name_)) RebuildList();
        } else if (kNameBox.in(x, y)) {
            focus_ = 0;
        } else {
            for (size_t i = 0; i < themes::kSlotCount; ++i) {
                if (HexBox(i).in(x, y)) { focus_ = int(i) + 1; break; }
                if (SwatchBox(i).in(x, y) && host_.dialogs) {
                    const std::string title = std::string("Synth2 Themes: ") + themes::kTitles[i];
                    host_.dialogs->PickColor(title, host_.theme()[i], [this, i](uint32_t rgb) {
                        if (!win_) return;
                        Edit(i, rgb);
                        Populate();
                        Paint();
                    });
                    break;
                }
            }
        }
        if (win_) {
            XSetInputFocus(d_, win_, RevertToParent, CurrentTime);
            Paint();
        }
    }
    void Key(XKeyEvent &k) {
        char buf[32] = {};
        KeySym sym = 0;
        const int n = XLookupString(&k, buf, sizeof buf - 1, &sym, nullptr);
        if (sym == XK_Tab || sym == XK_ISO_Left_Tab) {
            const int count = int(themes::kSlotCount) + 1;
            focus_ = ((focus_ < 0 ? -1 : focus_) + ((sym == XK_ISO_Left_Tab || (k.state & ShiftMask)) ? -1 : 1) + count) % count;
            Paint();
            return;
        }
        if (focus_ < 0) return;
        std::string &s = Field(focus_);
        if (sym == XK_BackSpace) { PopUtf8(s); }
        else if (sym == XK_Escape || sym == XK_Return || sym == XK_KP_Enter) { focus_ = -1; Paint(); return; }
        else if (n > 0 && !(k.state & ControlMask) && static_cast<unsigned char>(buf[0]) >= 32 && buf[0] != 127) {
            for (int i = 0; i < n; ++i) { // Latin-1 from XLookupString -> UTF-8
                const unsigned char c = static_cast<unsigned char>(buf[i]);
                if (c < 0x80) s += char(c);
                else { s += char(0xC0 | (c >> 6)); s += char(0x80 | (c & 63)); }
            }
        } else return;
        FieldChanged(focus_);
        Paint();
    }
    static void PopUtf8(std::string &s) {
        while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
        if (!s.empty()) s.pop_back();
    }

    // ---- painting ----
    void Text(cairo_t *cr, const std::string &s, double x, double y, double h, double grey = 0.92) {
        cairo_font_extents_t fe;
        cairo_font_extents(cr, &fe);
        cairo_set_source_rgb(cr, grey, grey, grey);
        cairo_move_to(cr, x, y + (h - fe.height) / 2 + fe.ascent);
        cairo_show_text(cr, s.c_str());
    }
    void Button(cairo_t *cr, const Box &b, const char *label, bool enabled) {
        cairo_set_source_rgb(cr, enabled ? 0.30 : 0.22, enabled ? 0.30 : 0.22, enabled ? 0.31 : 0.23);
        cairo_rectangle(cr, b.x, b.y, b.w, b.h);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0.08, 0.08, 0.08);
        cairo_rectangle(cr, b.x + .5, b.y + .5, b.w - 1, b.h - 1);
        cairo_stroke(cr);
        cairo_text_extents_t te;
        cairo_text_extents(cr, label, &te);
        Text(cr, label, b.x + (b.w - te.x_advance) / 2, b.y, b.h, enabled ? 0.92 : 0.5);
    }
    void DrawField(cairo_t *cr, const Box &b, const std::string &s, bool focused) {
        cairo_set_source_rgb(cr, 0.10, 0.10, 0.10);
        cairo_rectangle(cr, b.x, b.y, b.w, b.h);
        cairo_fill(cr);
        if (focused) cairo_set_source_rgb(cr, 0.35, 0.60, 0.90);
        else cairo_set_source_rgb(cr, 0.35, 0.35, 0.36);
        cairo_rectangle(cr, b.x + .5, b.y + .5, b.w - 1, b.h - 1);
        cairo_stroke(cr);
        cairo_save(cr);
        cairo_rectangle(cr, b.x + 2, b.y, b.w - 4, b.h);
        cairo_clip(cr);
        cairo_text_extents_t te;
        cairo_text_extents(cr, s.c_str(), &te);
        const double shift = std::min(0.0, b.w - 10 - te.x_advance); // keep the end in view
        Text(cr, s, b.x + 4 + shift, b.y, b.h);
        if (focused) {
            cairo_set_source_rgb(cr, 0.92, 0.92, 0.92);
            cairo_rectangle(cr, b.x + 4 + shift + te.x_advance + 1, b.y + 4, 1, b.h - 8);
            cairo_fill(cr);
        }
        cairo_restore(cr);
    }
    void Paint() {
        if (!win_) return;
        cairo_t *cr = cairo_create(surface_);
        cairo_set_source_rgb(cr, 0.17, 0.17, 0.18);
        cairo_paint(cr);
        cairo_select_font_face(cr, font_.Name().c_str(), CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 12.5);
        cairo_set_line_width(cr, 1);
        Text(cr, "Load", 12, 13, 20);
        // the drop-down
        const std::string shown = current_ >= 0 ? entries_[size_t(current_)].name + (current_ == 0 ? " (default)" : "") : "";
        Button(cr, kLoadBox, "", true);
        cairo_save(cr);
        cairo_rectangle(cr, kLoadBox.x, kLoadBox.y, kLoadBox.w - 20, kLoadBox.h);
        cairo_clip(cr);
        Text(cr, shown, kLoadBox.x + 6, kLoadBox.y, kLoadBox.h);
        cairo_restore(cr);
        const double ax = kLoadBox.x + kLoadBox.w - 14, ay = kLoadBox.y + kLoadBox.h / 2.0;
        cairo_move_to(cr, ax - 4, ay - 2); cairo_line_to(cr, ax + 4, ay - 2); cairo_line_to(cr, ax, ay + 3);
        cairo_close_path(cr);
        cairo_fill(cr);
        Button(cr, kDeleteBox, "Delete", IsUser(current_));
        Text(cr, "Name", 12, 47, 20);
        DrawField(cr, kNameBox, name_, focus_ == 0);
        Button(cr, kSaveBox, "Save", true);
        const themes::Palette p = host_.theme();
        for (size_t i = 0; i < themes::kSlotCount; ++i) {
            const int y = kRowsTop + int(i) * kRowHeight;
            Text(cr, themes::kTitles[i], 12, y + 1, 20);
            DrawField(cr, HexBox(i), hex_[i], focus_ == int(i) + 1);
            const Box s = SwatchBox(i);
            Button(cr, s, "", true);
            const uint32_t c = p[i];
            cairo_set_source_rgb(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0);
            cairo_rectangle(cr, s.x + 4, s.y + 4, s.w - 8, s.h - 8);
            cairo_fill(cr);
        }
        cairo_destroy(cr);
        cairo_surface_flush(surface_);
        XFlush(d_);
    }

    Host host_;
    Display *d_ = nullptr;
    Window win_ = 0;
    Atom deleteAtom_ = 0;
    cairo_surface_t *surface_ = nullptr;
    Gdiplus::FontFamily font_{L"Segoe UI"};
    std::vector<themes::Named> entries_;
    size_t userBegin_ = 0, userEnd_ = 0;
    int current_ = -1;
    int focus_ = -1;
    std::string name_;
    std::array<std::string, themes::kSlotCount> hex_{};
};
} // namespace oxo
