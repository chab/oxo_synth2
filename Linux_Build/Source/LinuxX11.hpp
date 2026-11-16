#pragma once

// The X11 half of the Linux helpers (LinuxPlatform.hpp is the X-free
// half, safe in the shared editor header): the popup menu, with submenus,
// check marks and column breaks.
// Include it after the editor headers: Xlib's macros (Bool, None,
// Status...) would break EditorCore/Json.hpp.

#include "LinuxPlatform.hpp"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <cairo/cairo-xlib.h>

namespace oxo::lx {

// An open popup menu. Not modal: it never runs a loop of its own (the
// host's UI thread must keep running, or the desktop calls the host "not
// responding"). It opens and grabs the pointer and keyboard; the editor's
// run-loop handler then feeds it the display's events (Handle) until Done.
class MenuTracker {
public:
    MenuTracker(Display *d, std::unique_ptr<Menu> menu, int rootX, int rootY)
        : d_(d), font_(L"Segoe UI"), menu_(std::move(menu)), opened_(NowMs()) {
        if (!menu_ || menu_->items.empty()) { done_ = true; return; }
        openLevel(menu_.get(), rootX, rootY, rootX);
        paint(levels_.back());
        XGrabPointer(d_, levels_[0].win, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync,
                     GrabModeAsync, None, None, CurrentTime);
        XGrabKeyboard(d_, levels_[0].win, False, GrabModeAsync, GrabModeAsync, CurrentTime);
        XFlush(d_);
    }
    MenuTracker(const MenuTracker &) = delete;
    MenuTracker &operator=(const MenuTracker &) = delete;
    ~MenuTracker() {
        if (!levels_.empty()) {
            XUngrabKeyboard(d_, CurrentTime);
            XUngrabPointer(d_, CurrentTime);
        }
        while (!levels_.empty()) closeLevel();
        XFlush(d_);
    }
    bool Done() const { return done_; }
    int Result() const { return result_; } // the chosen item's id, 0 = none

    // An event of the display while the menu is open: false = not the
    // menu's (the editor's own windows: their Expose...). Input is the
    // menu's wherever it lands (as its grab makes it), a press outside
    // every level cancels.
    bool Handle(XEvent &e) {
        if (done_) return false;
        int li = -1;
        for (size_t k = 0; k < levels_.size(); ++k) if (levels_[k].win == e.xany.window) li = int(k);
        auto choose = [&](int level, int i) {
            const Level &l = levels_[size_t(level)];
            if (!selectable(l, i) || l.menu->items[size_t(i)].sub) return false;
            result_ = l.menu->items[size_t(i)].id;
            return done_ = true;
        };
        switch (e.type) {
        case Expose:
            if (li < 0) return false;
            paint(levels_[size_t(li)]);
            break;
        case MotionNotify: {
            const auto [hl, hi] = hit(e.xmotion.x_root, e.xmotion.y_root);
            if (hl >= 0) setHot(hl, hi);
            break;
        }
        case ButtonPress: {
            if (e.xbutton.button > 3) break; // wheel
            const auto [hl, hi] = hit(e.xbutton.x_root, e.xbutton.y_root);
            if (hl < 0) { done_ = true; break; } // outside: cancelled
            pressedInside_ = true;
            break;
        }
        case ButtonRelease: {
            if (e.xbutton.button > 3) break;
            const auto [hl, hi] = hit(e.xbutton.x_root, e.xbutton.y_root);
            // the release of the click that opened the menu selects only
            // after a drag onto an item (as TrackPopupMenu)
            if (hl >= 0 && (pressedInside_ || NowMs() - opened_ > 350)) choose(hl, hi);
            break;
        }
        case KeyPress: {
            const KeySym k = XLookupKeysym(&e.xkey, 0);
            Level &top = levels_.back();
            const int tl = int(levels_.size()) - 1;
            if (k == XK_Escape) { if (levels_.size() > 1) closeLevel(); else done_ = true; }
            else if (k == XK_Down || k == XK_Up) setHot(tl, step(top, top.hot, k == XK_Down ? 1 : -1));
            else if (k == XK_Left && levels_.size() > 1) closeLevel();
            else if (k == XK_Right && selectable(top, top.hot) && top.menu->items[size_t(top.hot)].sub) {
                setHot(tl, top.hot);
                setHot(tl + 1, step(levels_.back(), -1, 1));
            } else if ((k == XK_Return || k == XK_KP_Enter) && top.hot >= 0) {
                if (!choose(tl, top.hot) && selectable(top, top.hot)) setHot(tl, top.hot);
            }
            break;
        }
        default:
            if (li < 0) return false;
            break;
        }
        XFlush(d_);
        return true;
    }

private:
    static constexpr int kRow = 22, kSep = 9, kPadX = 10, kCheck = 20, kArrow = 18;
    struct Level {
        Menu *menu = nullptr;
        Window win = 0;
        cairo_surface_t *surface = nullptr;
        int x = 0, y = 0, w = 0, h = 0;
        std::vector<int> colX, colW;
        struct Cell { int x, y, w, h; };
        std::vector<Cell> cells;
        int hot = -1;
    };

    double textWidth(const std::wstring &s) {
        cairo_surface_t *s1 = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
        cairo_t *cr = cairo_create(s1);
        font(cr);
        cairo_text_extents_t e;
        cairo_text_extents(cr, Utf8(s).c_str(), &e);
        cairo_destroy(cr);
        cairo_surface_destroy(s1);
        return e.x_advance;
    }
    void font(cairo_t *cr) {
        cairo_select_font_face(cr, font_.Name().c_str(), CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 13);
    }
    void layout(Level &l) {
        std::vector<std::vector<int>> columns(1);
        for (size_t i = 0; i < l.menu->items.size(); ++i) {
            if ((l.menu->items[i].flags & MF_MENUBREAK) && !columns.back().empty()) columns.emplace_back();
            columns.back().push_back(int(i));
        }
        l.cells.assign(l.menu->items.size(), {});
        int x = 1, height = 0;
        for (const auto &col : columns) {
            int w = 60, y = 3;
            for (int i : col) {
                const auto &it = l.menu->items[size_t(i)];
                if (!(it.flags & MF_SEPARATOR))
                    w = std::max(w, int(kCheck + textWidth(it.text) + kPadX + (it.sub ? kArrow : kPadX)));
            }
            for (int i : col) {
                const int h = (l.menu->items[size_t(i)].flags & MF_SEPARATOR) ? kSep : kRow;
                l.cells[size_t(i)] = {x, y, w, h};
                y += h;
            }
            x += w;
            height = std::max(height, y + 3);
        }
        l.w = x + 1;
        l.h = height;
    }
    void openLevel(Menu *menu, int x, int y, int altX) {
        Level l;
        l.menu = menu;
        layout(l);
        const int sw = DisplayWidth(d_, DefaultScreen(d_)), sh = DisplayHeight(d_, DefaultScreen(d_));
        if (x + l.w > sw) x = std::max(0, altX - l.w);
        if (y + l.h > sh) y = std::max(0, sh - l.h);
        l.x = x;
        l.y = y;
        XSetWindowAttributes a{};
        a.override_redirect = True;
        a.save_under = True;
        a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask;
        l.win = XCreateWindow(d_, DefaultRootWindow(d_), x, y, unsigned(l.w), unsigned(l.h), 0, CopyFromParent,
                              InputOutput, CopyFromParent, CWOverrideRedirect | CWSaveUnder | CWEventMask, &a);
        Atom type = XInternAtom(d_, "_NET_WM_WINDOW_TYPE", False);
        Atom popup = XInternAtom(d_, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
        XChangeProperty(d_, l.win, type, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char *>(&popup), 1);
        XMapRaised(d_, l.win);
        l.surface = cairo_xlib_surface_create(d_, l.win, DefaultVisual(d_, DefaultScreen(d_)), l.w, l.h);
        levels_.push_back(std::move(l));
    }
    void closeLevel() {
        Level &l = levels_.back();
        cairo_surface_destroy(l.surface);
        XDestroyWindow(d_, l.win);
        levels_.pop_back();
    }
    void paint(Level &l) {
        cairo_t *cr = cairo_create(l.surface);
        cairo_set_source_rgb(cr, 0.17, 0.17, 0.18);
        cairo_paint(cr);
        cairo_set_source_rgb(cr, 0.05, 0.05, 0.05);
        cairo_set_line_width(cr, 1);
        cairo_rectangle(cr, 0.5, 0.5, l.w - 1, l.h - 1);
        cairo_stroke(cr);
        font(cr);
        cairo_font_extents_t fe;
        cairo_font_extents(cr, &fe);
        for (size_t i = 0; i < l.menu->items.size(); ++i) {
            const auto &it = l.menu->items[i];
            const auto &c = l.cells[i];
            if (it.flags & MF_SEPARATOR) {
                cairo_set_source_rgb(cr, 0.32, 0.32, 0.33);
                cairo_rectangle(cr, c.x + 6, c.y + c.h / 2, c.w - 12, 1);
                cairo_fill(cr);
                continue;
            }
            const bool grey = it.flags & MF_GRAYED;
            if (int(i) == l.hot && !grey) {
                cairo_set_source_rgb(cr, 0.24, 0.44, 0.71);
                cairo_rectangle(cr, c.x + 2, c.y, c.w - 4, c.h);
                cairo_fill(cr);
            }
            const double ink = grey ? 0.5 : 0.92;
            cairo_set_source_rgb(cr, ink, ink, ink);
            if (it.flags & MF_CHECKED) { // a tick
                cairo_set_line_width(cr, 1.8);
                cairo_move_to(cr, c.x + 6, c.y + c.h / 2.0);
                cairo_line_to(cr, c.x + 9.5, c.y + c.h / 2.0 + 3.5);
                cairo_line_to(cr, c.x + 15, c.y + c.h / 2.0 - 4);
                cairo_stroke(cr);
            }
            cairo_move_to(cr, c.x + kCheck, c.y + (c.h - fe.height) / 2 + fe.ascent);
            cairo_show_text(cr, Utf8(it.text).c_str());
            if (it.sub) {
                const double ax = c.x + c.w - 12, ay = c.y + c.h / 2.0;
                cairo_move_to(cr, ax, ay - 4);
                cairo_line_to(cr, ax + 4, ay);
                cairo_line_to(cr, ax, ay + 4);
                cairo_close_path(cr);
                cairo_fill(cr);
            }
        }
        cairo_destroy(cr);
        cairo_surface_flush(l.surface);
    }
    // level, item under a root point; -1 = outside every menu
    std::pair<int, int> hit(int rx, int ry) const {
        for (int li = int(levels_.size()) - 1; li >= 0; --li) {
            const Level &l = levels_[size_t(li)];
            const int x = rx - l.x, y = ry - l.y;
            if (x < 0 || y < 0 || x >= l.w || y >= l.h) continue;
            for (size_t i = 0; i < l.cells.size(); ++i) {
                const auto &c = l.cells[i];
                if (x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h) return {li, int(i)};
            }
            return {li, -1};
        }
        return {-1, -1};
    }
    bool selectable(const Level &l, int i) const {
        if (i < 0 || i >= int(l.menu->items.size())) return false;
        const auto &it = l.menu->items[size_t(i)];
        return !(it.flags & (MF_SEPARATOR | MF_GRAYED));
    }
    void setHot(int li, int item) {
        Level &l = levels_[size_t(li)];
        if (l.hot != item) { l.hot = item; paint(l); }
        while (int(levels_.size()) > li + 1) closeLevel();
        if (selectable(l, item) && l.menu->items[size_t(item)].sub) { // hover opens the submenu
            const auto &c = l.cells[size_t(item)];
            openLevel(l.menu->items[size_t(item)].sub, l.x + c.x + c.w - 2, l.y + c.y - 3, l.x + c.x + 2);
            paint(levels_.back());
        }
    }
    int step(const Level &l, int from, int dir) const {
        const int n = int(l.menu->items.size());
        for (int k = 1; k <= n; ++k) {
            const int i = ((from < 0 ? (dir > 0 ? -1 : 0) : from) + dir * k + n * 4) % n;
            if (selectable(l, i)) return i;
        }
        return -1;
    }

    Display *d_;
    Gdiplus::FontFamily font_;
    std::unique_ptr<Menu> menu_;
    std::vector<Level> levels_;
    uint64_t opened_;
    bool pressedInside_ = false, done_ = false;
    int result_ = 0;
};

} // namespace oxo::lx
