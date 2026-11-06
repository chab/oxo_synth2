import SwiftUI
#if os(macOS)
import AppKit
#endif

/// Synth1's panel palette and widget vocabulary, redrawn (khaki panel, black
/// knobs, dark buttons with LEDs, red radio LEDs, cyan value displays).
/// Instance members come from the instance's palette (`SynthModel.palette`,
/// menu > Themes…), handed down as `\.theme` by the panel root; statics are
/// fixed colours / fonts.
struct Theme {
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
    static let labelFont = Font.system(size: 9.5, weight: .medium)
    static let buttonFont = Font.system(size: 10, weight: .semibold)
}

private struct ThemeKey: EnvironmentKey {
    static let defaultValue = Theme(.synth2)
}

extension EnvironmentValues {
    var theme: Theme {
        get { self[ThemeKey.self] }
        set { self[ThemeKey.self] = newValue }
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

    func body(content: Content) -> some View {
        content
            .allowsHitTesting(!model.learnActive)
            .overlay {
                if model.learnActive && id < SynthModel.paramCount {
                    let cc = model.learnTable[id]
                    let selected = model.learnSelection == id
                    ZStack {
                        RoundedRectangle(cornerRadius: 4).fill(cc != 0 ? Theme.learn.opacity(0.4) : Color.clear)
                        RoundedRectangle(cornerRadius: 4)
                            .strokeBorder(Theme.learn.opacity(selected ? 1 : 0.6), lineWidth: selected ? 2 : 1)
                        if cc != 0 {
                            Text("\(cc)").font(.system(size: 9, weight: .bold)).foregroundColor(.white)
                                .padding(.horizontal, 3).padding(.vertical, 1)
                                .background(RoundedRectangle(cornerRadius: 2.4).fill(Theme.learn)
                                    .overlay(RoundedRectangle(cornerRadius: 2.4).strokeBorder(.white.opacity(0.75), lineWidth: 1)))
                        }
                    }
                    .padding(-3)
                    .contentShape(Rectangle())
                    .onTapGesture(count: 2) { model.learnUnlearn(id) }
                    .onTapGesture { model.learnSelect(id) }
                }
            }
    }
}

extension View {
    func learnable(_ model: SynthModel, _ id: Int) -> some View { modifier(Learnable(model: model, id: id)) }

    /// Wheel over a stepped control (displays, radios): 33 pt per step.
    func displayWheel(_ model: SynthModel, _ id: Int, remainder: Binding<Double>) -> some View {
        #if os(macOS)
        return onMouseWheel { event in
            guard !model.showBrowser, !model.learnActive else { return false }
            var r = remainder.wrappedValue
            let steps = WheelFeel.displaySteps(deltaY: Double(event.scrollingDeltaY), remainder: &r)
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

    var body: some View {
        let r = model.host.range(id)
        let v = model.values[id]
        let frac = Double(v - r.min) / Double(max(1, r.max - r.min))
        VStack(spacing: 1) {
            ZStack {
                // Oxo Simple's 11-dot ring over the -140..+140 travel, at
                // 1.3 x the body radius, pulled in 1.5 pt.
                ForEach(0..<11, id: \.self) { i in
                    Circle().fill(theme.knobTicks).frame(width: 1.6, height: 1.6)
                        .offset(y: -size * 0.65 + 1.5)
                        .rotationEffect(.degrees(-140 + 28 * Double(i)))
                }
                ZStack {
                    Circle().fill(RadialGradient(colors: [theme.knobGradient, theme.knobBody], center: .init(x: 0.35, y: 0.3),
                                                 startRadius: 0, endRadius: size * 0.6))
                    // soft rim: dim, slightly blurred edge instead of a hard ring
                    Circle().strokeBorder(theme.knobRim.opacity(0.45), lineWidth: 1.6).blur(radius: 0.7)
                    Capsule().fill(theme.knobDial).frame(width: 2, height: size * 0.36).offset(y: -size * 0.24)
                        .rotationEffect(.degrees(-140 + 280 * frac))
                }
                .shadow(color: .black.opacity(0.3), radius: 1, x: 1, y: 1)
            }
            .frame(width: size, height: size)
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
                        lastY = g.translation.height
                        model.begin(id)
                        model.show(id)
                    }
                    let dy = lastY - g.translation.height
                    lastY = g.translation.height
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
                let steps = WheelFeel.knobSteps(deltaY: Double(event.scrollingDeltaY), span: r.max - r.min,
                                                shift: event.modifierFlags.contains(.shift), remainder: &wheelRemainder)
                model.wheel(id, steps: steps)
                return true
            }
            #endif
            Text(label).font(Theme.labelFont).foregroundColor(theme.ink).lineLimit(1).fixedSize()
                .frame(height: 12)
        }
        .learnable(model, id)
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

    var body: some View {
        let on = model.values[id] != 0
        HStack(spacing: 3) {
            RoundedRectangle(cornerRadius: 1).fill(on ? theme.ledOn : theme.ledOff)
                .frame(width: 5, height: 5)
                .shadow(color: on ? theme.ledOn.opacity(0.9) : .clear, radius: 2)
            Text(title).font(Theme.buttonFont).foregroundColor(Theme.buttonText).lineLimit(1).fixedSize()
        }
        .frame(width: width, height: 17)
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
        Text(title).font(Theme.buttonFont).foregroundColor(Theme.buttonText).lineLimit(1).fixedSize()
            .frame(width: width, height: 17)
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
        RoundedRectangle(cornerRadius: 3)
            .fill(LinearGradient(colors: [(fill ?? theme.button).opacity(0.85), fill ?? theme.button], startPoint: .top, endPoint: .bottom))
            .overlay(RoundedRectangle(cornerRadius: 3).strokeBorder(Color.black.opacity(0.6), lineWidth: 1))
            .shadow(color: .black.opacity(0.3), radius: 0.5, x: 1, y: 1)
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

    var body: some View {
        Button(action: action) {
            HStack(spacing: 4) {
                if let icon { Image(systemName: icon).font(.system(size: 9, weight: .bold)) }
                Text(title).font(.system(size: 11, weight: .semibold))
            }
            .foregroundColor(Theme.buttonText)
            .frame(width: width, height: height)
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

    var body: some View {
        let on = id.map { model.values[$0] != 0 } ?? true
        let face = Text(text).font(.system(size: 12, weight: .heavy))
            .foregroundColor(on ? (id == nil ? .white : Color(red: 1, green: 0.85, blue: 0.35)) : Color(white: 0.45))
            .frame(width: 16, height: 18)
            .background(RoundedRectangle(cornerRadius: 2).fill(on ? Color.black : Color(white: 0.22)))
            .overlay(RoundedRectangle(cornerRadius: 2).strokeBorder(Color(white: 0.5), lineWidth: 1))
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

    var body: some View {
        let layout = horizontal ? AnyLayout(HStackLayout(spacing: 8)) : AnyLayout(VStackLayout(alignment: ledTrailing ? .trailing : .leading, spacing: 0))
        layout {
            ForEach(options, id: \.value) { option in
                let on = model.values[id] == option.value
                HStack(spacing: 3) {
                    if !ledTrailing { led(on) }
                    if showLabels { Text(option.label).font(Theme.labelFont).foregroundColor(theme.ink).fixedSize() }
                    if ledTrailing { led(on) }
                }
                .frame(height: rowHeight)
                .contentShape(Rectangle())
                .onTapGesture { model.setOnce(id, option.value) }
            }
        }
        .displayWheel(model, id, remainder: $wheelRemainder)
        .learnable(model, id)
    }

    private func led(_ on: Bool) -> some View {
        Circle().fill(on ? theme.radioOn : theme.radioOff)
            .overlay(Circle().strokeBorder(Color.black.opacity(0.5), lineWidth: 0.5))
            .shadow(color: on ? theme.radioOn.opacity(0.9) : .clear, radius: 2.5)
            .frame(width: 7, height: 7)
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
        Canvas { ctx, size in
            let r = CGRect(origin: .zero, size: size).insetBy(dx: 4, dy: 3.5)
            ctx.stroke(icon.path(in: r), with: .color(on ? Color.black : Color(white: 0.85)),
                       style: StrokeStyle(lineWidth: 1.3, lineCap: .round, lineJoin: .round))
        }
        .frame(width: width, height: height)
        .background(RoundedRectangle(cornerRadius: 2).fill(on ? theme.waveOn : theme.button))
        .overlay(RoundedRectangle(cornerRadius: 2).strokeBorder(Color.black.opacity(0.6), lineWidth: 1))
        .shadow(color: on ? theme.waveOn.opacity(0.6) : .clear, radius: 2)
    }
}

/// Waveform radio grid (oscillator / sub shapes), row-major in value order.
struct WaveRadio: View {
    @ObservedObject var model: SynthModel
    let id: Int
    let shapes: [(value: Int, icon: WaveIcon)]
    var columns = 2
    @State private var wheelRemainder: Double = 0

    var body: some View {
        Grid(horizontalSpacing: 2, verticalSpacing: 2) {
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

/// One button showing the current wave, click steps to the next (LFO wave).
struct WaveCycle: View {
    @ObservedObject var model: SynthModel
    let id: Int
    let icons: [WaveIcon] // index = value - min

    var body: some View {
        let r = model.host.range(id)
        let i = min(max(model.values[id] - r.min, 0), icons.count - 1)
        WaveCell(icon: icons[i], on: true, width: 26, height: 17)
            .contentShape(Rectangle())
            .onTapGesture { model.cycle(id, shiftHeld() ? -1 : 1) }
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

    var body: some View {
        let v = model.values[id]
        Text(text?(v) ?? model.host.format(id, v))
            .font(.system(size: 11, weight: .bold, design: .monospaced))
            .foregroundColor(theme.displayText)
            .shadow(color: theme.displayText.opacity(0.5), radius: 2)
            .lineLimit(1).minimumScaleFactor(0.6)
            .frame(width: width, height: 17)
            .background(RoundedRectangle(cornerRadius: 2).fill(theme.displayFill))
            .overlay(RoundedRectangle(cornerRadius: 2).strokeBorder(Color.black.opacity(0.7), lineWidth: 1))
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { g in
                    if dragStart == nil {
                        dragStart = v
                        moved = false
                        model.begin(id)
                        model.show(id)
                    }
                    let steps = Int((-g.translation.height / WheelFeel.displayTravelPerStep).rounded())
                    if steps != 0 { moved = true }
                    if moved { model.set(id, (dragStart ?? v) + steps) }
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
            Text(label).font(.system(size: 10, weight: .bold, design: .monospaced))
                .foregroundColor(theme.displayText).lineLimit(1)
                .frame(width: width - 8, alignment: .leading)
        }
        .menuStyle(.borderlessButton)
        .menuIndicator(.hidden)
        .padding(.horizontal, 4)
        .frame(width: width, height: 16)
        .background(RoundedRectangle(cornerRadius: 2).fill(theme.displayFill))
        .overlay(RoundedRectangle(cornerRadius: 2).strokeBorder(Color.black.opacity(0.7), lineWidth: 1))
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

    var body: some View {
        ZStack(alignment: .topLeading) {
            Rectangle().fill(theme.panel)
            Text(title).font(.system(size: 12, weight: .semibold)).foregroundColor(theme.headerText)
                .padding(.leading, 5)
                .frame(width: width, height: 16, alignment: .leading)
                .background(theme.header)
            content.frame(width: width, height: height)
        }
        .frame(width: width, height: height)
        .overlay(Rectangle().strokeBorder(theme.edge, lineWidth: 1))
    }
}
