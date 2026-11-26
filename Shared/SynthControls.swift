import SwiftUI
#if os(macOS)
import AppKit
#endif

/// Synth1's panel palette and widget vocabulary, redrawn (khaki panel, black
/// knobs, dark buttons with LEDs, red radio LEDs, cyan value displays).
/// Instance members come from the instance's palette (`SynthModel.palette`,
/// menu > Themes…), handed down as `\.theme` by the panel root; statics are
/// fixed colours / fonts.
struct Theme: Hashable {
    private let p: ThemePalette
    init(_ palette: ThemePalette) { p = palette }

    var panel: Color { p[.panel].color }
    var header: Color { p[.header].color }
    var headerText: Color { p[.headerText].color }
    var ink: Color { p[.label].color }
    var dimInk: Color { p[.label].color.opacity(0.7) }
    var edge: Color { p[.panel].scaled(0.36) }
    var knobBody: Color { p[.knobBody].color }
    var knobDial: Color { p[.knobDial].color }
    /// Radial gradient colour (upper-left highlight fading into the body).
    var knobGradient: Color { p[.knobGradient].color }
    var knobRim: Color { p[.panel].scaled(0.5) }
    var knobTicks: Color { p[.label].color.opacity(0.3) }
    var button: Color { p[.button].color }
    static let buttonText = Color(white: 0.93)
    var ledOn: Color { p[.led].color }
    var ledOff: Color { p[.led].scaled(0.3) }
    var radioOn: Color { p[.radio].color }
    var radioOff: Color { p[.radio].scaled(0.42) }
    var displayText: Color { p[.displayText].color }
    var lcdTop: Color { p[.displayTop].color }
    var lcdBottom: Color { p[.displayBottom].color }
    var display: Color { p[.displayBottom].color }
    var displayFill: LinearGradient { LinearGradient(colors: [lcdTop, lcdBottom], startPoint: .top, endPoint: .bottom) }
    static let lcdText = Color(red: 0.88, green: 1.0, blue: 1.0)
    var waveOn: Color { p[.displayText].color }
    static let red = Color(red: 0.68, green: 0.06, blue: 0.05)
    static let learn = Color(red: 0.35, green: 0.65, blue: 0.91)
    static let labelFont = PanelFont(size: 9.5, weight: .medium)
    static let buttonFont = PanelFont(size: 10, weight: .semibold)
}

private struct ThemeKey: EnvironmentKey {
    static let defaultValue = Theme(.synth2)
}

/// The panel's live scale (view points per design point; window width /
/// design width). The panel is LAID OUT at its final size, never stretched
/// by a `.scaleEffect` (that magnified layers rendered at design size: text
/// and lines went soft above 100 %, issue #19). Design numbers (positions,
/// frames, paddings, fonts, stack spacings, corner radii, line widths) are
/// multiplied as they are applied: the `ui*` modifiers and `place` below,
/// `scale *` inline where a number is not a modifier's; drawing goes through
/// `PanelCanvas` / `FlatCanvas` (design points, the context scaled); gesture
/// distances are divided back into design points.
private struct EditorScaleKey: EnvironmentKey {
    static let defaultValue: CGFloat = 1
}

extension EnvironmentValues {
    var theme: Theme {
        get { self[ThemeKey.self] }
        set { self[ThemeKey.self] = newValue }
    }
    var editorScale: CGFloat {
        get { self[EditorScaleKey.self] }
        set { self[EditorScaleKey.self] = newValue }
    }
}

/// A system font in design points (`uiFont`; `font(scale)` for a Text
/// resolved by hand).
struct PanelFont {
    let size: CGFloat
    var weight: Font.Weight = .regular
    var design: Font.Design = .default
    var italic = false

    func font(_ scale: CGFloat) -> Font {
        let f = Font.system(size: size * scale, weight: weight, design: design)
        return italic ? f.italic() : f
    }
}

/// `content` built with the editor scale of where it is placed.
struct WithEditorScale<Content: View>: View {
    @Environment(\.editorScale) private var scale
    @ViewBuilder let content: (CGFloat) -> Content
    var body: some View { content(scale) }
}

// Value-type modifiers (not closures): SwiftUI compares them and skips the
// unchanged ones on every panel update.
private struct UIFrame: ViewModifier {
    @Environment(\.editorScale) private var scale
    let width: CGFloat?, height: CGFloat?, alignment: Alignment
    func body(content: Content) -> some View {
        content.frame(width: width.map { $0 * scale }, height: height.map { $0 * scale }, alignment: alignment)
    }
}

private struct UIPosition: ViewModifier {
    @Environment(\.editorScale) private var scale
    let x: CGFloat, y: CGFloat
    func body(content: Content) -> some View { content.position(x: x * scale, y: y * scale) }
}

private struct UIPlace: ViewModifier {
    @Environment(\.editorScale) private var scale
    let x: CGFloat, y: CGFloat, w: CGFloat, h: CGFloat
    func body(content: Content) -> some View {
        content.frame(width: w * scale, height: h * scale).position(x: (x + w / 2) * scale, y: (y + h / 2) * scale)
    }
}

private struct UIOffset: ViewModifier {
    @Environment(\.editorScale) private var scale
    let x: CGFloat, y: CGFloat
    func body(content: Content) -> some View { content.offset(x: x * scale, y: y * scale) }
}

private struct UIPadding: ViewModifier {
    @Environment(\.editorScale) private var scale
    let edges: Edge.Set, length: CGFloat
    func body(content: Content) -> some View { content.padding(edges, length * scale) }
}

private struct UIFontModifier: ViewModifier {
    @Environment(\.editorScale) private var scale
    let font: PanelFont
    func body(content: Content) -> some View { content.font(font.font(scale)) }
}

private struct UIShadow: ViewModifier {
    @Environment(\.editorScale) private var scale
    let color: Color, radius: CGFloat, x: CGFloat, y: CGFloat
    func body(content: Content) -> some View { content.shadow(color: color, radius: radius * scale, x: x * scale, y: y * scale) }
}

extension View {
    func uiFrame(width: CGFloat? = nil, height: CGFloat? = nil, alignment: Alignment = .center) -> some View {
        modifier(UIFrame(width: width, height: height, alignment: alignment))
    }

    func uiPosition(x: CGFloat, y: CGFloat) -> some View { modifier(UIPosition(x: x, y: y)) }

    /// Frame + place by top-left corner in the parent's coordinates (design points).
    func place(_ x: CGFloat, _ y: CGFloat, _ w: CGFloat, _ h: CGFloat) -> some View {
        modifier(UIPlace(x: x, y: y, w: w, h: h))
    }

    func uiOffset(x: CGFloat = 0, y: CGFloat = 0) -> some View { modifier(UIOffset(x: x, y: y)) }

    func uiPadding(_ edges: Edge.Set = .all, _ length: CGFloat) -> some View { modifier(UIPadding(edges: edges, length: length)) }

    func uiFont(_ font: PanelFont) -> some View { modifier(UIFontModifier(font: font)) }

    func uiFont(_ size: CGFloat, weight: Font.Weight = .regular, design: Font.Design = .default) -> some View {
        uiFont(PanelFont(size: size, weight: weight, design: design))
    }

    func uiShadow(color: Color, radius: CGFloat, x: CGFloat = 0, y: CGFloat = 0) -> some View {
        modifier(UIShadow(color: color, radius: radius, x: x, y: y))
    }
}

private func shiftHeld() -> Bool {
    #if os(macOS)
    return NSEvent.modifierFlags.contains(.shift)
    #else
    return false
    #endif
}

// MARK: - MIDI learn hotspots

/// In learn mode a control stops taking input and wears a hotspot instead
/// (Oxo Simple's look): dim border = not learned, bright 2 pt border = armed
/// and waiting for a CC, learn-colour fill + CC badge = learned. Tap arms,
/// double-tap unlearns.
private struct Learnable: ViewModifier {
    @ObservedObject var model: SynthModel
    let id: Int
    @Environment(\.editorScale) private var scale

    func body(content: Content) -> some View {
        content
            .allowsHitTesting(!model.learnActive)
            .overlay {
                if model.learnActive && id < SynthModel.paramCount {
                    let cc = model.learnTable[id]
                    let selected = model.learnSelection == id
                    ZStack {
                        RoundedRectangle(cornerRadius: 4 * scale).fill(cc != 0 ? Theme.learn.opacity(0.4) : Color.clear)
                        RoundedRectangle(cornerRadius: 4 * scale)
                            .strokeBorder(Theme.learn.opacity(selected ? 1 : 0.6), lineWidth: (selected ? 2 : 1) * scale)
                        if cc != 0 {
                            Text("\(cc)").uiFont(9, weight: .bold).foregroundColor(.white)
                                .uiPadding(.horizontal, 3).uiPadding(.vertical, 1)
                                .background(RoundedRectangle(cornerRadius: 2.4 * scale).fill(Theme.learn)
                                    .overlay(RoundedRectangle(cornerRadius: 2.4 * scale).strokeBorder(.white.opacity(0.75), lineWidth: scale)))
                        }
                    }
                    .uiPadding(.all, -3)
                    .contentShape(Rectangle())
                    .onTapGesture(count: 2) { model.learnUnlearn(id) }
                    .onTapGesture { model.learnSelect(id) }
                }
            }
    }
}

extension View {
    func learnable(_ model: SynthModel, _ id: Int) -> some View { modifier(Learnable(model: model, id: id)) }

    /// Wheel over a stepped control (displays, radios): 33 pt per step, up =
    /// value up. `enabled` false = the wheel passes through (vertical LEDs).
    func displayWheel(_ model: SynthModel, _ id: Int, remainder: Binding<Double>, enabled: Bool = true) -> some View {
        #if os(macOS)
        return onMouseWheel { event in
            guard enabled, !model.showBrowser, !model.learnActive else { return false }
            var r = remainder.wrappedValue
            let steps = WheelFeel.displaySteps(deltaY: Double(event.deviceDeltaY), remainder: &r)
            remainder.wrappedValue = r
            model.wheel(id, steps: steps)
            return true
        }
        #else
        return self
        #endif
    }
}

// MARK: - Knob

struct KnobView: View {
    /// Bipolar knobs: native value of the "zero" position.
    static let centres: [Int: Int] = [2: 64, 3: 64, 9: 0, 11: 64, 21: 64, 50: 64, 51: 64, 55: 64, 60: 64, 62: 64,
                                      72: 64, 83: 64, 84: 64, 85: 24, 90: 64, 98: 64]
    @ObservedObject var model: SynthModel
    @Environment(\.theme) private var theme
    let id: Int
    let label: String
    var size: CGFloat = 24
    @State private var dragStart: Int?
    @State private var dragAccum: CGFloat = 0
    @State private var lastY: CGFloat = 0
    /// A double-tap reset landed during this touch: the drag's later changes
    /// (finger jitter, touch-up) must not put the old value back (iPad).
    @State private var resetHeld = false
    @State private var wheelRemainder: Double = 0
    @Environment(\.editorScale) private var scale

    var body: some View {
        let r = model.host.range(id)
        let v = model.values[id]
        let frac = Double(v - r.min) / Double(max(1, r.max - r.min))
        VStack(spacing: scale) {
            KnobFace(theme: theme, size: size, frac: frac)
            .contentShape(Circle().scale(1.3))
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { g in
                    guard !resetHeld else { return }
                    // The live value, not the body's `v`: right after a
                    // double-tap reset the closure may still hold the old one.
                    let current = model.values[id]
                    if dragStart == nil {
                        dragStart = current
                        dragAccum = 0
                        lastY = g.translation.height / scale
                        model.begin(id)
                        model.show(id)
                    }
                    let dy = lastY - g.translation.height / scale
                    lastY = g.translation.height / scale
                    let span = CGFloat(max(1, r.max - r.min))
                    dragAccum += dy * span / (shiftHeld() ? 900 : 180)
                    model.set(id, (dragStart ?? current) + Int(dragAccum.rounded()))
                }
                .onEnded { _ in
                    dragStart = nil
                    resetHeld = false
                    model.end(id)
                })
            .simultaneousGesture(TapGesture(count: 2).onEnded {
                model.setOnce(id, r.def)
                if dragStart != nil { resetHeld = true }
            })
            #if os(macOS)
            .onMouseWheel { event in
                guard !model.showBrowser, !model.learnActive else { return false }
                let steps = WheelFeel.knobSteps(deltaY: Double(event.deviceDeltaY), span: r.max - r.min,
                                                shift: event.modifierFlags.contains(.shift), remainder: &wheelRemainder)
                model.wheel(id, steps: steps)
                return true
            }
            #endif
            Text(label).uiFont(Theme.labelFont).foregroundColor(theme.ink).lineLimit(1).fixedSize()
                .uiFrame(height: 12)
        }
        .learnable(model, id)
    }
}

/// The knob drawn in ONE Canvas: one layer, its shadow and blurred rim
/// rasterized with it. As separate shapes with `.shadow` / `.blur` it was
/// ~12 layers + live CoreAnimation filters per knob, which Logic's
/// out-of-process editor host composited again on every panel change (the
/// voice grid while playing): Logic's own UI lagged.
/// Drawn at the size it is shown, like the rest (issue #19). Only the dial
/// is feathered above 100 %: with a 1-pixel edge it stepped visibly while
/// turning (user, 2026-11-26); `dialFeather` softens it about halfway to
/// the old stretched rendering, nothing at 100 %.
private struct KnobFace: View {
    let theme: Theme
    let size: CGFloat
    let frac: Double
    @Environment(\.editorScale) private var scale

    var body: some View {
        // ticks reach 0.65 x size from the centre, the shadow ~3 pt past the body
        let side = (size * 0.65 + 3) * 2
        let feather = Self.dialFeather(scale)
        PanelCanvas { ctx, box in
            let c = CGPoint(x: box.width / 2, y: box.height / 2)
            // Oxo Simple's 11-dot ring over the -140..+140 travel, at
            // 1.3 x the body radius, pulled in 1.5 pt.
            let tick = size * 0.65 - 1.5
            for i in 0..<11 {
                let a = (-140 + 28 * Double(i)) * .pi / 180
                let p = CGPoint(x: c.x + tick * sin(a), y: c.y - tick * cos(a))
                ctx.fill(Path(ellipseIn: CGRect(x: p.x - 0.8, y: p.y - 0.8, width: 1.6, height: 1.6)), with: .color(theme.knobTicks))
            }
            let body = CGRect(x: c.x - size / 2, y: c.y - size / 2, width: size, height: size)
            ctx.drawLayer { k in
                k.addFilter(.shadow(color: .black.opacity(0.3), radius: 1, x: 1, y: 1))
                k.fill(Path(ellipseIn: body), with: .radialGradient(
                    Gradient(colors: [theme.knobGradient, theme.knobBody]),
                    center: CGPoint(x: body.minX + 0.35 * size, y: body.minY + 0.3 * size), startRadius: 0, endRadius: size * 0.6))
                // soft rim: dim, slightly blurred edge instead of a hard ring
                k.drawLayer { rim in
                    rim.addFilter(.blur(radius: 0.7))
                    rim.stroke(Path(ellipseIn: body.insetBy(dx: 0.8, dy: 0.8)), with: .color(theme.knobRim.opacity(0.45)), lineWidth: 1.6)
                }
                var dial = k
                dial.translateBy(x: c.x, y: c.y)
                dial.rotate(by: .degrees(-140 + 280 * frac))
                let h = size * 0.36
                if feather > 0 { dial.addFilter(.blur(radius: feather)) }
                dial.fill(Path(roundedRect: CGRect(x: -1, y: -size * 0.24 - h / 2, width: 2, height: h), cornerRadius: 1),
                          with: .color(theme.knobDial))
            }
        }
        .uiFrame(width: side, height: side)
        .uiFrame(width: size, height: size)
    }

    /// Blur radius of the dial in design points. The stretched rendering's
    /// edge grew with the scale (a 1-pixel edge magnified), so does this:
    /// 0 at 100 %, 0.10 at 150 %, 0.12 at 200 %, about half the stretched
    /// edge's softness (0.15 ~ all of it at 200 %; measured on live window
    /// captures, a snapshot's cacheDisplay draws scaleEffect sharp).
    static func dialFeather(_ scale: CGFloat) -> CGFloat {
        scale > 1 ? 0.14 * (scale * scale - 1).squareRoot() / scale : 0
    }
}

// MARK: - Buttons

/// Dark push button with a LED: a parameter switch (on / off).
struct ToggleButton: View {
    @ObservedObject var model: SynthModel
    @Environment(\.theme) private var theme
    let id: Int
    let title: String
    var width: CGFloat = 40
    @Environment(\.editorScale) private var scale

    var body: some View {
        let on = model.values[id] != 0
        HStack(spacing: 3 * scale) {
            FlatCanvas(bleed: 4, key: [on, theme]) { ctx, r in
                let led = Path(roundedRect: r, cornerRadius: 1)
                if on { ctx.glow(led, theme.ledOn.opacity(0.9), radius: 2) }
                ctx.fill(led, with: .color(on ? theme.ledOn : theme.ledOff))
            }
            .uiFrame(width: 5, height: 5)
            Text(title).uiFont(Theme.buttonFont).foregroundColor(Theme.buttonText).lineLimit(1).fixedSize()
        }
        .uiFrame(width: width, height: 17)
        .background(ButtonFace())
        .contentShape(Rectangle())
        .onTapGesture { model.setOnce(id, on ? 0 : 1) }
        .learnable(model, id)
    }
}

/// Dark push button stepping a choice parameter (Synth1's "type", "dst",
/// "range" buttons); Shift-click steps back. The LEDs / display next to it
/// show the value.
struct CycleButton: View {
    @ObservedObject var model: SynthModel
    let id: Int
    let title: String
    var width: CGFloat = 40

    var body: some View {
        Text(title).uiFont(Theme.buttonFont).foregroundColor(Theme.buttonText).lineLimit(1).fixedSize()
            .uiFrame(width: width, height: 17)
            .background(ButtonFace())
            .contentShape(Rectangle())
            .onTapGesture { model.cycle(id, shiftHeld() ? -1 : 1) }
            .learnable(model, id)
    }
}

struct ButtonFace: View {
    var fill: Color?
    @Environment(\.theme) private var theme
    var body: some View {
        let base = fill ?? theme.button
        FlatCanvas(bleed: 3, key: [base]) { ctx, r in
            let face = Path(roundedRect: r, cornerRadius: 3)
            var shadow = ctx
            shadow.addFilter(.shadow(color: .black.opacity(0.3), radius: 0.5, x: 1, y: 1))
            shadow.fill(face, with: .linearGradient(Gradient(colors: [base.opacity(0.85), base]),
                                                    startPoint: CGPoint(x: 0, y: r.minY), endPoint: CGPoint(x: 0, y: r.maxY)))
            ctx.strokeBorder(r, cornerRadius: 3, .black.opacity(0.6), lineWidth: 1)
        }
    }
}

/// Plain (non-parameter) push button: write / opt / arrows.
struct PanelButton: View {
    let title: String
    var width: CGFloat = 50
    var height: CGFloat = 22
    var fill: Color?
    var icon: String?
    let action: () -> Void
    @Environment(\.editorScale) private var scale

    var body: some View {
        Button(action: action) {
            HStack(spacing: 4 * scale) {
                if let icon { Image(systemName: icon).uiFont(9, weight: .bold) }
                Text(title).uiFont(11, weight: .semibold)
            }
            .foregroundColor(Theme.buttonText)
            .uiFrame(width: width, height: height)
            .background(ButtonFace(fill: fill))
        }
        .buttonStyle(.plain)
    }
}

/// Synth1's black number badge ("1", "2"). With an id it is the LFO's
/// on / off switch (lit digit = on).
struct Badge: View {
    @ObservedObject var model: SynthModel
    let text: String
    var id: Int?
    @Environment(\.editorScale) private var scale

    var body: some View {
        let on = id.map { model.values[$0] != 0 } ?? true
        let face = Text(text).uiFont(12, weight: .heavy)
            .foregroundColor(on ? (id == nil ? .white : Color(red: 1, green: 0.85, blue: 0.35)) : Color(white: 0.45))
            .uiFrame(width: 16, height: 18)
            .background(RoundedRectangle(cornerRadius: 2 * scale).fill(on ? Color.black : Color(white: 0.22)))
            .overlay(RoundedRectangle(cornerRadius: 2 * scale).strokeBorder(Color(white: 0.5), lineWidth: scale))
        if let id {
            face.contentShape(Rectangle())
                .onTapGesture { model.setOnce(id, on ? 0 : 1) }
                .learnable(model, id)
        } else {
            face
        }
    }
}

// MARK: - Radio LEDs

/// Red LED radio list (filter type, LFO dest, arp type, ...). `ledTrailing`
/// puts the LED right of its label (LFO 2's half of the shared dest column).
struct LedRadio: View {
    @ObservedObject var model: SynthModel
    @Environment(\.theme) private var theme
    let id: Int
    let options: [(value: Int, label: String)]
    var horizontal = false
    var ledTrailing = false
    var showLabels = true
    var rowHeight: CGFloat = 13
    @State private var wheelRemainder: Double = 0
    @Environment(\.editorScale) private var scale

    var body: some View {
        let layout = horizontal ? AnyLayout(HStackLayout(spacing: 8 * scale)) : AnyLayout(VStackLayout(alignment: ledTrailing ? .trailing : .leading, spacing: 0))
        layout {
            ForEach(options, id: \.value) { option in
                let on = model.values[id] == option.value
                HStack(spacing: 3 * scale) {
                    if !ledTrailing { led(on) }
                    if showLabels { Text(option.label).uiFont(Theme.labelFont).foregroundColor(theme.ink).fixedSize() }
                    if ledTrailing { led(on) }
                }
                .uiFrame(height: rowHeight)
                .contentShape(Rectangle())
                .onTapGesture { model.setOnce(id, option.value) }
            }
        }
        // a vertical list ignores the wheel (it read as moving the LED the
        // wrong way); a horizontal pair / trio steps like a display
        .displayWheel(model, id, remainder: $wheelRemainder, enabled: horizontal)
        .learnable(model, id)
    }

    private func led(_ on: Bool) -> some View {
        FlatCanvas(bleed: 5, key: [on, theme]) { ctx, r in
            let led = Path(ellipseIn: r)
            if on { ctx.glow(led, theme.radioOn.opacity(0.9), radius: 2.5) }
            ctx.fill(led, with: .color(on ? theme.radioOn : theme.radioOff))
            ctx.stroke(Path(ellipseIn: r.insetBy(dx: 0.25, dy: 0.25)), with: .color(.black.opacity(0.5)), lineWidth: 0.5)
        }
        .uiFrame(width: 7, height: 7)
    }
}

// MARK: - Flat drawing

/// A widget drawn in ONE Canvas = one layer, its glow / shadow rasterized
/// with it. SwiftUI `.shadow` / `.blur` are live CoreAnimation filters on
/// their own layers (even with a clear colour); Logic's out-of-process
/// editor host composited all of them again on every panel change (the
/// voice grid while playing) and its own UI lagged. `bleed`: room drawn
/// past the frame on each side for the glow; `draw` gets the frame's rect,
/// in design points (the context scaled, like `PanelCanvas`).
/// `key` = EVERYTHING `draw` reads (state, theme, text): the canvas redraws
/// only when it (or the scale / size) changes. A closure cannot be compared,
/// so without it all ~57 of them redrew on every panel update (any value
/// change), at full resolution since issue #19. A value missing from the
/// key = a stale drawing.
struct FlatCanvas: View {
    @Environment(\.editorScale) private var scale
    let bleed: CGFloat
    let key: [AnyHashable]
    let draw: (inout GraphicsContext, CGRect) -> Void

    var body: some View {
        FlatLayer(bleed: bleed, scale: scale, key: key, draw: draw)
            .equatable()
            .padding(-bleed * scale)
    }
}

private struct FlatLayer: View, Equatable {
    let bleed: CGFloat
    let scale: CGFloat
    let key: [AnyHashable]
    let draw: (inout GraphicsContext, CGRect) -> Void

    static func == (a: Self, b: Self) -> Bool { a.bleed == b.bleed && a.scale == b.scale && a.key == b.key }

    var body: some View {
        Canvas { ctx, size in
            ctx.scaleBy(x: scale, y: scale)
            draw(&ctx, CGRect(x: bleed, y: bleed, width: size.width / scale - 2 * bleed, height: size.height / scale - 2 * bleed))
        }
    }
}

/// A Canvas drawn in DESIGN points: its context scaled by the editor scale
/// (paths, text and filter radii alike), so the drawing is rasterized at
/// the size it is shown; `draw` gets the size in design points.
struct PanelCanvas: View {
    @Environment(\.editorScale) private var scale
    let draw: (inout GraphicsContext, CGSize) -> Void

    var body: some View {
        Canvas { ctx, size in
            ctx.scaleBy(x: scale, y: scale)
            draw(&ctx, CGSize(width: size.width / scale, height: size.height / scale))
        }
    }
}

extension GraphicsContext {
    /// SwiftUI's `.shadow(color:radius:)` glow under `path`.
    func glow(_ path: Path, _ color: Color, radius: CGFloat) {
        var g = self
        g.addFilter(.shadow(color: color, radius: radius))
        g.fill(path, with: .color(color))
    }

    /// `strokeBorder` of a rounded rectangle: the line inside `r`.
    func strokeBorder(_ r: CGRect, cornerRadius: CGFloat, _ color: Color, lineWidth: CGFloat) {
        let h = lineWidth / 2
        stroke(Path(roundedRect: r.insetBy(dx: h, dy: h), cornerRadius: max(0, cornerRadius - h)), with: .color(color), lineWidth: lineWidth)
    }
}

// MARK: - Waveform buttons

enum WaveIcon {
    case sine, saw, sawDown, pulse, square, tri, noise, sampleHold, smooth

    func path(in r: CGRect) -> Path {
        var p = Path()
        let x0 = r.minX, x1 = r.maxX, y0 = r.minY, y1 = r.maxY, w = r.width, h = r.height
        switch self {
        case .sine:
            p.move(to: CGPoint(x: x0, y: r.midY))
            for i in 1...24 {
                let t = Double(i) / 24
                p.addLine(to: CGPoint(x: x0 + w * t, y: r.midY - h / 2 * sin(2 * .pi * t)))
            }
        case .saw, .sawDown:
            // Synth1's two-stroke falling saw: edge up, ramp down
            p.move(to: CGPoint(x: x0, y: y1))
            p.addLine(to: CGPoint(x: x0, y: y0))
            p.addLine(to: CGPoint(x: x1, y: y1))
        case .pulse, .square:
            let duty = self == .pulse ? 0.3 : 0.5
            p.move(to: CGPoint(x: x0, y: y1))
            p.addLine(to: CGPoint(x: x0, y: y0))
            p.addLine(to: CGPoint(x: x0 + w * duty, y: y0))
            p.addLine(to: CGPoint(x: x0 + w * duty, y: y1))
            p.addLine(to: CGPoint(x: x1, y: y1))
            p.addLine(to: CGPoint(x: x1, y: y0))
        case .tri:
            p.move(to: CGPoint(x: x0, y: y1))
            p.addLine(to: CGPoint(x: r.midX, y: y0))
            p.addLine(to: CGPoint(x: x1, y: y1))
        case .noise:
            let ys: [Double] = [0.5, 0.1, 0.8, 0.3, 0.95, 0.2, 0.6, 0.0, 0.7, 0.4, 0.9, 0.25, 0.55]
            for (i, y) in ys.enumerated() {
                let pt = CGPoint(x: x0 + w * Double(i) / Double(ys.count - 1), y: y0 + h * y)
                if i == 0 { p.move(to: pt) } else { p.addLine(to: pt) }
            }
        case .sampleHold, .smooth:
            // smooth = the S&H steps with rounded edges (smooth random)
            let ys: [Double] = [0.6, 0.1, 0.8, 0.35, 0.95]
            let step = w / Double(ys.count), round = self == .smooth ? step * 0.35 : 0
            p.move(to: CGPoint(x: x0, y: y0 + h * ys[0]))
            for (i, y) in ys.enumerated() {
                let edge = x0 + step * Double(i), level = y0 + h * y
                if i > 0 {
                    let prev = y0 + h * ys[i - 1]
                    if round > 0 {
                        p.addCurve(to: CGPoint(x: edge + round, y: level),
                                   control1: CGPoint(x: edge, y: prev), control2: CGPoint(x: edge, y: level))
                    } else {
                        p.addLine(to: CGPoint(x: edge, y: level))
                    }
                }
                p.addLine(to: CGPoint(x: x0 + step * Double(i + 1) - (i < ys.count - 1 ? round : 0), y: level))
            }
        }
        return p
    }
}

private struct WaveCell: View {
    @Environment(\.theme) private var theme
    let icon: WaveIcon
    let on: Bool
    var width: CGFloat = 22
    var height: CGFloat = 14

    var body: some View {
        FlatCanvas(bleed: 4, key: [icon, on, theme]) { ctx, cell in
            let face = Path(roundedRect: cell, cornerRadius: 2)
            if on { ctx.glow(face, theme.waveOn.opacity(0.6), radius: 2) }
            ctx.fill(face, with: .color(on ? theme.waveOn : theme.button))
            ctx.strokeBorder(cell, cornerRadius: 2, .black.opacity(0.6), lineWidth: 1)
            ctx.stroke(icon.path(in: cell.insetBy(dx: 4, dy: 3.5)), with: .color(on ? Color.black : Color(white: 0.85)),
                       style: StrokeStyle(lineWidth: 1.3, lineCap: .round, lineJoin: .round))
        }
        .uiFrame(width: width, height: height)
    }
}

/// Waveform radio grid (oscillator / sub shapes), row-major in value order.
struct WaveRadio: View {
    @ObservedObject var model: SynthModel
    let id: Int
    let shapes: [(value: Int, icon: WaveIcon)]
    var columns = 2
    @State private var wheelRemainder: Double = 0
    @Environment(\.editorScale) private var scale

    var body: some View {
        Grid(horizontalSpacing: 2 * scale, verticalSpacing: 2 * scale) {
            ForEach(Array(stride(from: 0, to: shapes.count, by: columns)), id: \.self) { row in
                GridRow {
                    ForEach(row..<min(row + columns, shapes.count), id: \.self) { i in
                        WaveCell(icon: shapes[i].icon, on: model.values[id] == shapes[i].value)
                            .contentShape(Rectangle())
                            .onTapGesture { model.setOnce(id, shapes[i].value) }
                    }
                }
            }
        }
        .displayWheel(model, id, remainder: $wheelRemainder)
        .learnable(model, id)
    }
}

/// One button showing the current wave (LFO shape): a click opens its pick
/// list (`TypeMenu`), the wheel steps.
struct WaveDisplay: View {
    @ObservedObject var model: SynthModel
    let id: Int
    let icons: [WaveIcon] // index = value - min
    @State private var wheelRemainder: Double = 0

    var body: some View {
        let r = model.host.range(id)
        let i = min(max(model.values[id] - r.min, 0), icons.count - 1)
        WaveCell(icon: icons[i], on: true, width: 26, height: 17)
            .contentShape(Rectangle())
            .onTapGesture { model.typeMenu = id }
            .displayWheel(model, id, remainder: $wheelRemainder)
            .learnable(model, id)
    }
}

// MARK: - Value displays

/// Small cyan-on-dark display ("00" key shift, "16" poly, "ST" delay type):
/// drag vertically / wheel to change, double-click = default; `tapCycles`
/// = a click steps to the next value (type displays).
struct ValueDisplay: View {
    @ObservedObject var model: SynthModel
    @Environment(\.theme) private var theme
    let id: Int
    var width: CGFloat = 30
    var tapCycles = false
    var text: ((Int) -> String)?
    /// A click (no drag) calls this instead of cycling (effect type menu).
    var onTap: (() -> Void)?
    @State private var dragStart: Int?
    @State private var moved = false
    @State private var wheelRemainder: Double = 0
    @Environment(\.editorScale) private var scale

    var body: some View {
        let v = model.values[id]
        let string = text?(v) ?? model.host.format(id, v)
        let label = Text(string)
            .font(.system(size: 11, weight: .bold, design: .monospaced))
            .foregroundColor(theme.displayText)
        FlatCanvas(bleed: 0, key: [string, theme]) { ctx, r in
            ctx.fill(Path(roundedRect: r, cornerRadius: 2), with: .linearGradient(
                Gradient(colors: [theme.lcdTop, theme.lcdBottom]), startPoint: CGPoint(x: 0, y: r.minY), endPoint: CGPoint(x: 0, y: r.maxY)))
            ctx.strokeBorder(r, cornerRadius: 2, .black.opacity(0.7), lineWidth: 1)
            // the text's glow; long values shrink to fit (down to 60 %)
            let t = ctx.resolve(label)
            let w = t.measure(in: CGSize(width: CGFloat.infinity, height: .infinity)).width
            let k = min(1, max(0.6, r.width / max(1, w)))
            var g = ctx
            g.addFilter(.shadow(color: theme.displayText.opacity(0.5), radius: 2))
            g.translateBy(x: r.midX, y: r.midY)
            g.scaleBy(x: k, y: k)
            g.draw(t, at: .zero, anchor: .center)
        }
            .uiFrame(width: width, height: 17)
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { g in
                    if dragStart == nil {
                        dragStart = v
                        moved = false
                        model.begin(id)
                        model.show(id)
                    }
                    let steps = Int((-g.translation.height / scale / WheelFeel.displayTravelPerStep).rounded())
                    if steps != 0 { moved = true }
                    if moved { model.set(id, model.stepped(id, from: dragStart ?? v, by: steps)) }
                }
                .onEnded { _ in
                    if !moved, let onTap { onTap() }
                    else if !moved && tapCycles { model.cycle(id, shiftHeld() ? -1 : 1, gesture: false) }
                    dragStart = nil
                    model.end(id)
                })
            .simultaneousGesture(TapGesture(count: 2).onEnded { if !tapCycles { model.setOnce(id, model.host.range(id).def) } })
            .displayWheel(model, id, remainder: $wheelRemainder)
            .learnable(model, id)
    }
}

/// Display-styled pop-up menu (MIDI ctrl source / assign).
struct MenuDisplay: View {
    @ObservedObject var model: SynthModel
    @Environment(\.theme) private var theme
    let id: Int
    let width: CGFloat
    let label: String
    let groups: [(title: String, items: [(value: Int, label: String)])]
    @Environment(\.editorScale) private var scale

    var body: some View {
        Menu {
            ForEach(groups.indices, id: \.self) { g in
                if groups[g].title.isEmpty {
                    items(groups[g].items)
                } else {
                    Menu(groups[g].title) { items(groups[g].items) }
                }
            }
        } label: {
            // plain modifiers on the Text: the borderless menu (mac) rebuilds
            // its label from them, a `ui*` wrapper view would hide them
            Text(label).font(PanelFont(size: 10, weight: .bold, design: .monospaced).font(scale))
                .foregroundColor(theme.displayText).lineLimit(1)
                .frame(width: (width - 8) * scale, alignment: .leading)
        }
        .menuStyle(.borderlessButton)
        .menuIndicator(.hidden)
        .menuOrder(.fixed)
        .uiPadding(.horizontal, 4)
        .uiFrame(width: width, height: 16)
        .background(RoundedRectangle(cornerRadius: 2 * scale).fill(theme.displayFill))
        .overlay(RoundedRectangle(cornerRadius: 2 * scale).strokeBorder(Color.black.opacity(0.7), lineWidth: scale))
    }

    private func items(_ list: [(value: Int, label: String)]) -> some View {
        ForEach(list, id: \.value) { item in
            Button(item.label) { model.setOnce(id, item.value) }
        }
    }
}

// MARK: - Section box

/// Framed section with Synth1's title strip. Children are placed with
/// `.position` in the box's own coordinates (title strip = top 16 pt).
struct SectionBox<Content: View>: View {
    @Environment(\.theme) private var theme
    let title: String
    let width: CGFloat
    let height: CGFloat
    @ViewBuilder let content: Content
    @Environment(\.editorScale) private var scale

    var body: some View {
        ZStack(alignment: .topLeading) {
            Rectangle().fill(theme.panel)
            Text(title).uiFont(12, weight: .semibold).foregroundColor(theme.headerText)
                .uiPadding(.leading, 5)
                .uiFrame(width: width, height: 16, alignment: .leading)
                .background(theme.header)
            content.uiFrame(width: width, height: height)
        }
        .uiFrame(width: width, height: height)
        .overlay(Rectangle().strokeBorder(theme.edge, lineWidth: scale))
    }
}
