#pragma once

// The small part of GDI+ the Windows editor draws with, implemented on
// cairo, so LinuxEditor.cpp keeps WindowsEditor.cpp's drawing code as it is
// (same geometry, same calls). Only what that code uses is here: colours,
// points / rectangles, pens and brushes (solid, linear, the knob's path
// gradient), paths (lines, arcs, beziers, ellipses, text outlines) and a
// Graphics with fill / stroke / clip / transform / save-restore.

#include <cairo/cairo.h>
#include <fontconfig/fontconfig.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <strings.h>
#include <memory>
#include <string>
#include <vector>

namespace Gdiplus {

using BYTE = uint8_t;
using INT = int;
using ARGB = uint32_t;
enum Status { Ok = 0, GenericError = 1 };
enum SmoothingMode { SmoothingModeNone, SmoothingModeAntiAlias };
enum TextRenderingHint { TextRenderingHintAntiAlias };
enum PixelOffsetMode { PixelOffsetModeHighQuality };
enum LineCap { LineCapFlat, LineCapRound };
enum LineJoin { LineJoinMiter, LineJoinRound };
enum CombineMode { CombineModeReplace, CombineModeIntersect };
enum FontStyle { FontStyleRegular = 0, FontStyleBold = 1 };
enum StringFormatFlags { StringFormatFlagsNoWrap = 0x1000, StringFormatFlagsNoClip = 0x4000 };

class Color {
public:
    Color() = default;
    Color(BYTE a, BYTE r, BYTE g, BYTE b) : argb_((ARGB(a) << 24) | (ARGB(r) << 16) | (ARGB(g) << 8) | b) {}
    Color(BYTE r, BYTE g, BYTE b) : Color(255, r, g, b) {}
    BYTE GetA() const { return BYTE(argb_ >> 24); }
    BYTE GetR() const { return BYTE(argb_ >> 16); }
    BYTE GetG() const { return BYTE(argb_ >> 8); }
    BYTE GetB() const { return BYTE(argb_); }
    void source(cairo_t *cr) const { cairo_set_source_rgba(cr, GetR() / 255.0, GetG() / 255.0, GetB() / 255.0, GetA() / 255.0); }
    void stop(cairo_pattern_t *p, double offset) const {
        cairo_pattern_add_color_stop_rgba(p, offset, GetR() / 255.0, GetG() / 255.0, GetB() / 255.0, GetA() / 255.0);
    }

private:
    ARGB argb_ = 0xFF000000;
};

struct PointF {
    float X = 0, Y = 0;
    PointF() = default;
    PointF(float x, float y) : X(x), Y(y) {}
};

struct RectF {
    float X = 0, Y = 0, Width = 0, Height = 0;
    RectF() = default;
    RectF(float x, float y, float w, float h) : X(x), Y(y), Width(w), Height(h) {}
    bool Contains(float x, float y) const { return x >= X && x < X + Width && y >= Y && y < Y + Height; }
    void Inflate(float dx, float dy) { X -= dx; Y -= dy; Width += 2 * dx; Height += 2 * dy; }
    static bool Union(RectF &out, const RectF &a, const RectF &b) {
        const float l = std::min(a.X, b.X), t = std::min(a.Y, b.Y);
        const float r = std::max(a.X + a.Width, b.X + b.Width), bt = std::max(a.Y + a.Height, b.Y + b.Height);
        out = RectF(l, t, r - l, bt - t);
        return out.Width > 0 && out.Height > 0;
    }
};

// ---- fonts -------------------------------------------------------------------
// GDI+ names a Windows family; here the first installed family of a list
// that matches it in spirit (fontconfig decides what "installed" means).
class FontFamily {
public:
    explicit FontFamily(const wchar_t *windowsName) {
        const std::wstring w = windowsName ? windowsName : L"";
        static const char *sans[] = {"Segoe UI", "Noto Sans", "Open Sans", "Liberation Sans", "DejaVu Sans", "sans-serif"};
        static const char *mono[] = {"Consolas", "Noto Sans Mono", "Liberation Mono", "DejaVu Sans Mono", "monospace"};
        const bool isMono = w == L"Consolas";
        name_ = Pick(isMono ? mono : sans, isMono ? std::size(mono) : std::size(sans));
        squeeze_ = Measure(isMono);
    }
    const std::string &Name() const { return name_; }
    // Horizontal scale that brings the stand-in to the Windows font's
    // width (the layout's labels and LCD columns were fitted to it):
    // monospace = Consolas' advance (1126/2048 em); sans = 95% of Arial
    // Bold's width, about Segoe UI Bold's. Never wider than the font.
    float Squeeze() const { return squeeze_; }

private:
    float Measure(bool mono) const {
        cairo_surface_t *s = cairo_recording_surface_create(CAIRO_CONTENT_ALPHA, nullptr);
        cairo_t *cr = cairo_create(s);
        cairo_select_font_face(cr, name_.c_str(), CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 2048);
        cairo_text_extents_t e;
        cairo_text_extents(cr, mono ? "0000000000" : "abcdefghijklmnopqrstuvwxyz", &e);
        cairo_destroy(cr);
        cairo_surface_destroy(s);
        if (e.x_advance <= 0) return 1.f;
        // Arial Bold advances, a..z, in 2048ths of an em
        static constexpr int arialBold[26] = {1139, 1251, 1139, 1251, 1139, 682, 1251, 1251, 569, 569, 1139, 569, 1821,
                                              1251, 1251, 1251, 1251, 797, 1139, 682, 1251, 1139, 1593, 1139, 1139, 1024};
        double target = 0;
        if (mono) target = 1126.0 * 10;
        else { for (int a : arialBold) target += a; target *= 0.95; }
        return float(std::clamp(target / e.x_advance, 0.75, 1.0));
    }
    static std::string Pick(const char **list, size_t n) {
        FcInit();
        for (size_t i = 0; i + 1 < n; ++i) {
            FcPattern *pattern = FcNameParse(reinterpret_cast<const FcChar8 *>(list[i]));
            FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
            FcDefaultSubstitute(pattern);
            FcResult result;
            FcPattern *match = FcFontMatch(nullptr, pattern, &result);
            bool found = false;
            FcChar8 *family = nullptr;
            if (match && FcPatternGetString(match, FC_FAMILY, 0, &family) == FcResultMatch)
                found = strcasecmp(reinterpret_cast<const char *>(family), list[i]) == 0;
            if (match) FcPatternDestroy(match);
            FcPatternDestroy(pattern);
            if (found) return list[i];
        }
        return list[n - 1];
    }
    std::string name_;
    float squeeze_ = 1.f;
};

class StringFormat {
public:
    void SetFormatFlags(int) {}
};

// ---- paths -------------------------------------------------------------------
// Built in a scratch cairo context with an identity matrix, kept as a
// cairo_path_t and replayed under the drawing context's transform.
class GraphicsPath {
public:
    // An unbounded recording surface: on a small image surface cairo drops
    // the glyphs that fall outside it from a text path.
    GraphicsPath() : surface_(cairo_recording_surface_create(CAIRO_CONTENT_ALPHA, nullptr)), cr_(cairo_create(surface_)) {
        cairo_font_options_t *o = cairo_font_options_create();
        cairo_font_options_set_hint_style(o, CAIRO_HINT_STYLE_NONE);
        cairo_font_options_set_hint_metrics(o, CAIRO_HINT_METRICS_OFF);
        cairo_set_font_options(cr_, o);
        cairo_font_options_destroy(o);
    }
    ~GraphicsPath() {
        if (cached_) cairo_path_destroy(cached_);
        cairo_destroy(cr_);
        cairo_surface_destroy(surface_);
    }
    GraphicsPath(const GraphicsPath &) = delete;
    GraphicsPath &operator=(const GraphicsPath &) = delete;

    void Reset() { cairo_new_path(cr_); changed(); ellipse_ = false; }
    void StartFigure() { cairo_new_sub_path(cr_); }
    void CloseFigure() { cairo_close_path(cr_); changed(); }
    void AddLine(float x1, float y1, float x2, float y2) {
        to(x1, y1);
        cairo_line_to(cr_, x2, y2);
        changed();
    }
    // GDI+ angles: degrees, clockwise from +x (y down), on the ellipse in (x, y, w, h).
    void AddArc(float x, float y, float w, float h, float start, float sweep) {
        if (w <= 0 || h <= 0) return;
        const double a0 = start * M_PI / 180.0, a1 = (start + sweep) * M_PI / 180.0;
        cairo_save(cr_);
        cairo_translate(cr_, x + w / 2.0, y + h / 2.0);
        cairo_scale(cr_, w / 2.0, h / 2.0);
        if (sweep >= 0) cairo_arc(cr_, 0, 0, 1, a0, a1);
        else cairo_arc_negative(cr_, 0, 0, 1, a0, a1);
        cairo_restore(cr_);
        changed();
    }
    void AddBezier(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4) {
        to(x1, y1);
        cairo_curve_to(cr_, x2, y2, x3, y3, x4, y4);
        changed();
    }
    void AddEllipse(float x, float y, float w, float h) {
        cairo_new_sub_path(cr_);
        AddArc(x, y, w, h, 0, 360);
        cairo_close_path(cr_);
        ellipse_ = true;
        ex_ = x; ey_ = y; ew_ = w; eh_ = h;
        changed();
    }
    Status AddString(const wchar_t *s, int, const FontFamily *family, int style, float emSize, const PointF &origin,
                     const StringFormat *) {
        if (!s || !family) return GenericError;
        const std::string utf8 = Utf8(s);
        cairo_select_font_face(cr_, family->Name().c_str(), CAIRO_FONT_SLANT_NORMAL,
                               (style & FontStyleBold) ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr_, emSize);
        cairo_font_extents_t fe;
        cairo_font_extents(cr_, &fe);
        cairo_save(cr_);
        cairo_translate(cr_, origin.X, origin.Y + fe.ascent); // GDI+: origin = top of the line
        cairo_scale(cr_, family->Squeeze(), 1.0);
        cairo_move_to(cr_, 0, 0);
        cairo_text_path(cr_, utf8.c_str());
        cairo_restore(cr_);
        changed();
        return Ok;
    }
    Status GetBounds(RectF *r) const {
        double x1, y1, x2, y2;
        cairo_fill_extents(cr_, &x1, &y1, &x2, &y2);
        if (x2 <= x1 || y2 <= y1) { *r = RectF(); return GenericError; }
        *r = RectF(float(x1), float(y1), float(x2 - x1), float(y2 - y1));
        return Ok;
    }
    // Replays the path into `cr` (current transform applies).
    void Append(cairo_t *cr) const {
        if (!cached_) cached_ = cairo_copy_path(cr_);
        cairo_new_path(cr);
        cairo_append_path(cr, cached_);
    }
    bool IsEllipse() const { return ellipse_; }
    RectF EllipseRect() const { return RectF(ex_, ey_, ew_, eh_); }

    static std::string Utf8(const wchar_t *s) {
        std::string out;
        for (; *s; ++s) {
            const uint32_t c = uint32_t(*s);
            if (c < 0x80) out += char(c);
            else if (c < 0x800) { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 63)); }
            else if (c < 0x10000) { out += char(0xE0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 63)); out += char(0x80 | (c & 63)); }
            else { out += char(0xF0 | (c >> 18)); out += char(0x80 | ((c >> 12) & 63)); out += char(0x80 | ((c >> 6) & 63)); out += char(0x80 | (c & 63)); }
        }
        return out;
    }

private:
    void to(float x, float y) {
        if (cairo_has_current_point(cr_)) cairo_line_to(cr_, x, y);
        else cairo_move_to(cr_, x, y);
    }
    void changed() {
        if (cached_) { cairo_path_destroy(cached_); cached_ = nullptr; }
    }
    cairo_surface_t *surface_;
    cairo_t *cr_;
    mutable cairo_path_t *cached_ = nullptr;
    bool ellipse_ = false;
    float ex_ = 0, ey_ = 0, ew_ = 0, eh_ = 0;
};

// ---- pens / brushes ----------------------------------------------------------
class Pen {
public:
    Pen(const Color &c, float width = 1.f) : color_(c), width_(width) {}
    void SetStartCap(LineCap c) { cap_ = c; }
    void SetEndCap(LineCap c) { cap_ = c; }
    void SetLineJoin(LineJoin j) { join_ = j; }
    void Apply(cairo_t *cr) const {
        color_.source(cr);
        cairo_set_line_width(cr, width_);
        cairo_set_line_cap(cr, cap_ == LineCapRound ? CAIRO_LINE_CAP_ROUND : CAIRO_LINE_CAP_BUTT);
        cairo_set_line_join(cr, join_ == LineJoinRound ? CAIRO_LINE_JOIN_ROUND : CAIRO_LINE_JOIN_MITER);
    }

private:
    Color color_;
    float width_;
    LineCap cap_ = LineCapFlat;
    LineJoin join_ = LineJoinMiter;
};

class Brush {
public:
    virtual ~Brush() = default;
    virtual void Apply(cairo_t *cr) const = 0;
};
class SolidBrush final : public Brush {
public:
    explicit SolidBrush(const Color &c) : color_(c) {}
    void Apply(cairo_t *cr) const override { color_.source(cr); }

private:
    Color color_;
};
class LinearGradientBrush final : public Brush {
public:
    LinearGradientBrush(const PointF &a, const PointF &b, const Color &ca, const Color &cb)
        : a_(a), b_(b), ca_(ca), cb_(cb) {}
    void Apply(cairo_t *cr) const override {
        cairo_pattern_t *p = cairo_pattern_create_linear(a_.X, a_.Y, b_.X, b_.Y);
        ca_.stop(p, 0);
        cb_.stop(p, 1);
        cairo_pattern_set_extend(p, CAIRO_EXTEND_PAD);
        cairo_set_source(cr, p);
        cairo_pattern_destroy(p);
    }

private:
    PointF a_, b_;
    Color ca_, cb_;
};
// GDI+'s path gradient on an ellipse (the knob body): the centre colour at
// an off-centre point, blending to the surround colour at the rim. cairo's
// two-circle radial gradient (a point to the rim circle) is the same shape.
class PathGradientBrush final : public Brush {
public:
    explicit PathGradientBrush(const GraphicsPath *path) {
        if (path && path->IsEllipse()) rim_ = path->EllipseRect();
        else if (path) path->GetBounds(&rim_);
        centre_ = PointF(rim_.X + rim_.Width / 2, rim_.Y + rim_.Height / 2);
    }
    void SetCenterPoint(const PointF &p) { centre_ = p; }
    void SetCenterColor(const Color &c) { centreColor_ = c; }
    void SetSurroundColors(const Color *c, INT *count) {
        if (c && count && *count > 0) surround_ = c[0];
    }
    void Apply(cairo_t *cr) const override {
        const double cx = rim_.X + rim_.Width / 2.0, cy = rim_.Y + rim_.Height / 2.0;
        const double r = std::max(rim_.Width, rim_.Height) / 2.0;
        cairo_pattern_t *p = cairo_pattern_create_radial(centre_.X, centre_.Y, 0, cx, cy, r);
        centreColor_.stop(p, 0);
        surround_.stop(p, 1);
        cairo_pattern_set_extend(p, CAIRO_EXTEND_PAD);
        cairo_set_source(cr, p);
        cairo_pattern_destroy(p);
    }

private:
    RectF rim_;
    PointF centre_;
    Color centreColor_, surround_;
};

// ---- Graphics ----------------------------------------------------------------
using GraphicsState = int;

class Graphics {
public:
    explicit Graphics(cairo_t *cr) : cr_(cr) { cairo_save(cr_); }
    ~Graphics() { cairo_restore(cr_); }
    Graphics(const Graphics &) = delete;
    Graphics &operator=(const Graphics &) = delete;
    cairo_t *Context() const { return cr_; }

    void SetSmoothingMode(SmoothingMode m) {
        cairo_set_antialias(cr_, m == SmoothingModeNone ? CAIRO_ANTIALIAS_NONE : CAIRO_ANTIALIAS_DEFAULT);
    }
    void SetTextRenderingHint(TextRenderingHint) {}
    void SetPixelOffsetMode(PixelOffsetMode) {}
    void ScaleTransform(float sx, float sy) { cairo_scale(cr_, sx, sy); }
    void TranslateTransform(float dx, float dy) { cairo_translate(cr_, dx, dy); }
    GraphicsState Save() { cairo_save(cr_); return ++depth_; }
    void Restore(GraphicsState) { cairo_restore(cr_); --depth_; }
    void SetClip(const RectF &r, CombineMode = CombineModeReplace) {
        cairo_rectangle(cr_, r.X, r.Y, r.Width, r.Height);
        cairo_clip(cr_);
    }
    void Clear(const Color &c) {
        cairo_save(cr_);
        cairo_set_operator(cr_, CAIRO_OPERATOR_SOURCE);
        c.source(cr_);
        cairo_paint(cr_);
        cairo_restore(cr_);
    }

    void FillPath(const Brush *b, const GraphicsPath *p) {
        p->Append(cr_);
        b->Apply(cr_);
        cairo_set_fill_rule(cr_, CAIRO_FILL_RULE_WINDING);
        cairo_fill(cr_);
    }
    void DrawPath(const Pen *pen, const GraphicsPath *p) {
        p->Append(cr_);
        pen->Apply(cr_);
        cairo_stroke(cr_);
    }
    void FillRectangle(const Brush *b, float x, float y, float w, float h) {
        cairo_new_path(cr_);
        cairo_rectangle(cr_, x, y, w, h);
        b->Apply(cr_);
        cairo_fill(cr_);
    }
    void FillRectangle(const Brush *b, const RectF &r) { FillRectangle(b, r.X, r.Y, r.Width, r.Height); }
    void DrawRectangle(const Pen *pen, const RectF &r) { DrawRectangle(pen, r.X, r.Y, r.Width, r.Height); }
    void DrawRectangle(const Pen *pen, float x, float y, float w, float h) {
        cairo_new_path(cr_);
        cairo_rectangle(cr_, x, y, w, h);
        pen->Apply(cr_);
        cairo_stroke(cr_);
    }
    void FillEllipse(const Brush *b, float x, float y, float w, float h) {
        ellipse(x, y, w, h);
        b->Apply(cr_);
        cairo_fill(cr_);
    }
    void DrawEllipse(const Pen *pen, float x, float y, float w, float h) {
        ellipse(x, y, w, h);
        pen->Apply(cr_);
        cairo_stroke(cr_);
    }
    void DrawLine(const Pen *pen, float x1, float y1, float x2, float y2) {
        cairo_new_path(cr_);
        cairo_move_to(cr_, x1, y1);
        cairo_line_to(cr_, x2, y2);
        pen->Apply(cr_);
        cairo_stroke(cr_);
    }
    void DrawLine(const Pen *pen, const PointF &a, const PointF &b) { DrawLine(pen, a.X, a.Y, b.X, b.Y); }
    void DrawLines(const Pen *pen, const PointF *pts, int n) {
        if (n < 2) return;
        polyline(pts, n, false);
        pen->Apply(cr_);
        cairo_stroke(cr_);
    }
    void FillPolygon(const Brush *b, const PointF *pts, int n) {
        polyline(pts, n, true);
        b->Apply(cr_);
        cairo_fill(cr_);
    }
    void DrawPolygon(const Pen *pen, const PointF *pts, int n) {
        polyline(pts, n, true);
        pen->Apply(cr_);
        cairo_stroke(cr_);
    }

private:
    void ellipse(float x, float y, float w, float h) {
        cairo_new_path(cr_);
        if (w <= 0 || h <= 0) return;
        cairo_save(cr_);
        cairo_translate(cr_, x + w / 2.0, y + h / 2.0);
        cairo_scale(cr_, w / 2.0, h / 2.0);
        cairo_arc(cr_, 0, 0, 1, 0, 2 * M_PI);
        cairo_restore(cr_);
        cairo_close_path(cr_);
    }
    void polyline(const PointF *pts, int n, bool close) {
        cairo_new_path(cr_);
        cairo_move_to(cr_, pts[0].X, pts[0].Y);
        for (int i = 1; i < n; ++i) cairo_line_to(cr_, pts[i].X, pts[i].Y);
        if (close) cairo_close_path(cr_);
    }
    cairo_t *cr_;
    int depth_ = 0;
};

} // namespace Gdiplus
