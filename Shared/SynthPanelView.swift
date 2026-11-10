import SwiftUI

/// Synth1's panel, redrawn: same sections, same places, same widget kinds
/// (so Synth1 users find everything where their hands expect it). Fixed
/// 770 x 560 design canvas (Synth1's own proportions) scaled to fit the
/// view (mac window aspect-locked; iPad host windows: centred). iPad UI
/// width mode: the width leads (minus two side scroll strips), the panel
/// scrolls vertically.
struct SynthPanelView: View {
    static let designSize = CGSize(width: 770, height: 560)
    /// Editor width at "100 %" (UI size menu, default window) = Synth1's
    /// window at its 150 % (measured side by side).
    static let unitWidth: CGFloat = 700
    /// Width mode: the invisible scroll strip on each side (view points).
    static let scrollStripWidth: CGFloat = 40

    static let canvasSpace = "synth2.canvas"
    /// The project's home (the version box opens it).
    static let projectURL = URL(string: "https://github.com/chab/oxo_synth2")!

    @ObservedObject var model: SynthModel
    @Environment(\.openURL) private var openURL
    @State private var themeDialogPos = CGPoint(x: 220, y: 70)
    /// Width mode: scroll position in view points (0 = top).
    @State private var scrollOffset: CGFloat = 0
    /// This instance's palette; the panel hands it down as `\.theme`.
    private var theme: Theme { Theme(model.palette) }

    var body: some View {
        GeometryReader { geo in
            let size = geo.size
            let fit = min(size.width / Self.designSize.width, size.height / Self.designSize.height)
            let wide = (size.width - 2 * Self.scrollStripWidth) / Self.designSize.width
            // Width mode scrolls only when the panel would not fit anyway, and
            // never under a dialog (browser / write, themes, update): those
            // show the whole panel at the fit scale, text fields and the
            // bottom bar on screen.
            let scrolls = model.uiWidthMode && wide > fit
                && !model.showBrowser && !model.showThemes && model.update == nil
            let scale = scrolls ? wide : fit
            let contentHeight = Self.designSize.height * scale
            let maxOffset = scrolls ? max(0, contentHeight - size.height) : 0
            ZStack(alignment: .topLeading) {
                panel(scale: scale)
                    .offset(x: (size.width - Self.designSize.width * scale) / 2,
                            // top-aligned (a tall window leaves the room below)
                            y: scrolls ? -min(max(0, scrollOffset), maxOffset) : 0)
                if scrolls {
                    PanelScrollStrip(maxOffset: maxOffset, offset: $scrollOffset)
                        .frame(width: Self.scrollStripWidth, height: size.height)
                    PanelScrollStrip(maxOffset: maxOffset, offset: $scrollOffset)
                        .frame(width: Self.scrollStripWidth, height: size.height)
                        .offset(x: size.width - Self.scrollStripWidth)
                }
            }
            .frame(width: size.width, height: size.height, alignment: .topLeading)
            .clipped()
            // a resize / rotation shrank the range (kept while a dialog shows
            // the fit layout: closing it returns to the same place)
            .onChange(of: maxOffset) { _, newMax in
                if scrolls, scrollOffset > newMax { scrollOffset = newMax }
            }
            .onChange(of: model.uiWidthMode) { _, _ in scrollOffset = 0 }
        }
        .background(theme.panel)
        #if os(iOS)
        .ignoresSafeArea(.keyboard) // never rescale for the keyboard
        #endif
        .modifier(FileActions(model: model))
        .environment(\.theme, theme)
    }

    /// The design canvas + its overlays, scaled (top-left anchored).
    private func panel(scale: CGFloat) -> some View {
        ZStack(alignment: .topLeading) {
            ZStack(alignment: .topLeading) {
                canvas
                if model.showBrowser { PatchBrowserView(model: model).place(6, 34, 758, 444) }
            }
            // tips over everything at the canvas' scale (SynthTooltip.swift)
            .oxoTooltipHost()
            if model.showThemes {
                ThemeDialog(model: model, position: $themeDialogPos, canvas: Self.designSize)
            }
            if model.showLogoMenu {
                // A click anywhere else closes it.
                Color.clear.contentShape(Rectangle())
                    .frame(width: Self.designSize.width, height: Self.designSize.height)
                    .onTapGesture { model.showLogoMenu = false }
                LogoMenu(model: model, scale: scale).offset(x: 6, y: 30)
            }
            if model.showEffectMenu {
                Color.clear.contentShape(Rectangle())
                    .frame(width: Self.designSize.width, height: Self.designSize.height)
                    .onTapGesture { model.showEffectMenu = false }
                // under the effect type display (section x 66 +- 23, y 40 + 8.5)
                EffectTypeMenu(model: model).offset(x: 548 + 66 - 23, y: 34 + 40 + 9)
            }
            if let state = model.update {
                UpdateDialog(model: model, state: state)
                    .frame(width: Self.designSize.width, height: Self.designSize.height)
            }
        }
        .frame(width: Self.designSize.width, height: Self.designSize.height, alignment: .topLeading)
        .coordinateSpace(name: Self.canvasSpace)
        .scaleEffect(scale, anchor: .topLeading)
    }

    private var canvas: some View {
        ZStack(alignment: .topLeading) {
            theme.panel
            titleBand
            oscillators.place(6, 34, 312, 240)
            lfo.place(6, 278, 312, 130)
            wheelMidi.place(6, 412, 536, 66)
            amplifier.place(324, 34, 218, 80)
            filter.place(324, 118, 218, 156)
            arpeggiator.place(324, 278, 218, 130)
            effect.place(548, 34, 216, 80)
            equalizer.place(548, 118, 216, 58)
            delay.place(548, 180, 216, 58)
            chorus.place(548, 242, 216, 58)
            voice.place(548, 304, 216, 174)
            BottomBar(model: model).place(6, 484, 758, 70)
        }
        .frame(width: Self.designSize.width, height: Self.designSize.height)
    }

    // MARK: - placement helpers (section coordinates)

    private func knob(_ id: Int, _ label: String, _ x: CGFloat, _ y: CGFloat, size: CGFloat = 24) -> some View {
        KnobView(model: model, id: id, label: label, size: size).position(x: x, y: y + 6.5)
    }

    private func toggle(_ id: Int, _ title: String, _ x: CGFloat, _ y: CGFloat, w: CGFloat = 40) -> some View {
        ToggleButton(model: model, id: id, title: title, width: w).position(x: x, y: y)
    }

    private func cycler(_ id: Int, _ title: String, _ x: CGFloat, _ y: CGFloat, w: CGFloat = 40) -> some View {
        CycleButton(model: model, id: id, title: title, width: w).position(x: x, y: y)
    }

    private func display(_ id: Int, _ x: CGFloat, _ y: CGFloat, w: CGFloat = 30, cycles: Bool = false,
                         text: ((Int) -> String)? = nil, onTap: (() -> Void)? = nil) -> some View {
        ValueDisplay(model: model, id: id, width: w, tapCycles: cycles, text: text, onTap: onTap).position(x: x, y: y)
    }

    private static let twoDigits: (Int) -> String = { String(format: "%02d", $0) }

    /// Dim separator line (section-header colour, half transparent).
    private func divider(_ x1: CGFloat, _ y1: CGFloat, _ x2: CGFloat, _ y2: CGFloat) -> some View {
        Rectangle().fill(theme.header.opacity(0.55))
            .frame(width: max(1, x2 - x1), height: max(1, y2 - y1))
            .position(x: (x1 + x2) / 2, y: (y1 + y2) / 2)
            .allowsHitTesting(false)
    }

    private func label(_ text: String, _ x: CGFloat, _ y: CGFloat) -> some View {
        Text(text).font(Theme.labelFont).foregroundColor(theme.ink).fixedSize().position(x: x, y: y)
    }

    private func radio(_ id: Int, _ labels: [String], from: Int = 0) -> [(value: Int, label: String)] {
        labels.enumerated().map { (from + $0.offset, $0.element) }
    }

    // MARK: - title band

    private var titleBand: some View {
        HStack(alignment: .firstTextBaseline, spacing: 8) {
            Text("Synth2").font(.system(size: 20, weight: .bold)).foregroundColor(theme.ink)
                .contentShape(Rectangle())
                // Logo menu (UI size where the host can resize + theme).
                // Press + release, not onTapGesture: a tap is cancelled by
                // the slightest mouse travel (Oxo Drums' logo).
                .gesture(DragGesture(minimumDistance: 0).onEnded { g in
                    guard hypot(g.translation.width, g.translation.height) < 8 else { return }
                    model.showLogoMenu.toggle()
                })
            Text("Virtual Analog").font(.system(size: 12, weight: .bold).italic()).foregroundColor(theme.dimInk)
            Spacer()
            if model.uiWidthModeStorage != nil {
                UIModeToggle(model: model).alignmentGuide(.firstTextBaseline) { $0[VerticalAlignment.center] + 4 }
            }
            Text("V\(Self.version)  a Synth1 recreation")
                .font(.system(size: 12, weight: .bold)).foregroundColor(.white)
                .padding(.horizontal, 10).frame(height: 20)
                .background(Rectangle().fill(Theme.red))
                .overlay(Rectangle().strokeBorder(Color.black, lineWidth: 1))
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0).onEnded { g in
                    if hypot(g.translation.width, g.translation.height) < 8 { openURL(Self.projectURL) }
                })
        }
        .padding(.horizontal, 8)
        .place(0, 4, 770, 26)
    }

    private static let version = UpdateCheck.installed.version + (UpdateCheck.installed.build > 0 ? "(\(UpdateCheck.installed.build))" : "")

    // MARK: - sections

    private var oscillators: some View {
        SectionBox(title: "Oscillators", width: 312, height: 240) {
            ZStack {
                // Synth1's sub-areas: osc1 | sub, osc2 + mod env | key shift
                // column, osc2 / mod env (dim header-colour lines)
                divider(166, 22, 166, 76)
                divider(8, 78, 304, 78)
                divider(212, 84, 212, 234)
                divider(8, 163, 204, 163)
                Badge(model: model, text: "1").position(x: 14, y: 36)
                WaveRadio(model: model, id: 0, shapes: [(0, .sine), (1, .saw), (2, .pulse), (3, .tri)]).position(x: 50, y: 40)
                knob(76, "det", 100, 36)
                knob(45, "FM", 136, 36)
                label("sub", 186, 24)
                WaveRadio(model: model, id: 96, shapes: [(0, .sine), (1, .tri), (2, .saw), (3, .square)]).position(x: 208, y: 44)
                knob(95, "sub", 260, 36)
                LedRadio(model: model, id: 97, options: radio(97, ["0oct", "-1oct"]), horizontal: true).position(x: 234, y: 70)

                Badge(model: model, text: "2").position(x: 14, y: 94)
                WaveRadio(model: model, id: 1, shapes: [(1, .saw), (2, .pulse), (3, .tri), (4, .noise)]).position(x: 50, y: 98)
                toggle(7, "ring", 104, 94)
                toggle(6, "sync", 150, 94)
                display(9, 260, 90, w: 32, text: Self.twoDigits)
                label("key shift", 260, 106)

                toggle(4, "track", 36, 136, w: 52)
                knob(2, "pitch", 104, 132)
                knob(3, "fine", 146, 132)
                knob(5, "mix", 238, 132)
                knob(8, "p/w", 282, 132)

                toggle(10, "m.env", 36, 184, w: 52)
                knob(12, "A", 104, 184)
                knob(13, "D", 140, 184)
                knob(11, "amt", 176, 184)

                cycler(71, "dest.", 36, 220, w: 52)
                LedRadio(model: model, id: 71, options: radio(71, ["osc2", "FM", "p/w"]), horizontal: true).position(x: 126, y: 220)
                knob(91, "phase", 238, 206)
                knob(72, "tune", 282, 206)
            }
        }
    }

    private var lfo: some View {
        let dests = radio(41, ["osc2", "osc1,2", "filter", "amp", "p/w", "FM", "pan"], from: 1)
        return SectionBox(title: "LFO", width: 312, height: 130) {
            ZStack {
                Badge(model: model, text: "1", id: 57).position(x: 14, y: 30)
                WaveCycle(model: model, id: 42, icons: [.sawDown, .tri, .square, .sampleHold, .smooth, .sine]).position(x: 42, y: 30)
                cycler(41, "dst", 78, 30, w: 32)
                knob(43, "spd", 30, 62)
                knob(44, "amt", 76, 62)
                label("-sync-", 52, 99)
                toggle(67, "tempo", 30, 114, w: 46)
                toggle(68, "key", 78, 114, w: 36)

                LedRadio(model: model, id: 41, options: dests, showLabels: false).position(x: 124, y: 72)
                VStack(spacing: 0) {
                    ForEach(dests, id: \.value) { Text($0.label).font(Theme.labelFont).foregroundColor(theme.ink).frame(height: 13) }
                }
                .position(x: 156, y: 72)
                LedRadio(model: model, id: 46, options: radio(46, dests.map(\.label), from: 1), ledTrailing: true, showLabels: false)
                    .position(x: 188, y: 72)

                cycler(46, "dst", 234, 30, w: 32)
                WaveCycle(model: model, id: 47, icons: [.sawDown, .tri, .square, .sampleHold, .smooth, .sine]).position(x: 270, y: 30)
                Badge(model: model, text: "2", id: 58).position(x: 298, y: 30)
                knob(48, "spd", 236, 62)
                knob(49, "amt", 282, 62)
                label("-sync-", 260, 99)
                toggle(69, "tempo", 236, 114, w: 46)
                toggle(70, "key", 284, 114, w: 36)
            }
        }
    }

    private var wheelMidi: some View {
        SectionBox(title: "Wheel / MIDI", width: 536, height: 66) {
            ZStack {
                label("p.b range", 32, 28)
                display(40, 32, 46, w: 30, text: Self.twoDigits)
                label("src1", 82, 31)
                ctrlSrc(86).position(x: 158, y: 31)
                label("->", 228, 31)
                ctrlAssign(87).position(x: 298, y: 31)
                label("src2", 82, 51)
                ctrlSrc(88).position(x: 158, y: 51)
                label("->", 228, 51)
                ctrlAssign(89).position(x: 298, y: 51)
                knob(50, "sens1", 400, 34, size: 22)
                knob(51, "sens2", 446, 34, size: 22)
            }
        }
    }

    private var amplifier: some View {
        SectionBox(title: "Amplifier", width: 218, height: 80) {
            ZStack {
                knob(25, "A", 20, 40)
                knob(26, "D", 55, 40)
                knob(27, "S", 90, 40)
                knob(28, "R", 125, 40)
                knob(29, "gain", 162, 40)
                knob(30, "vel", 198, 40)
            }
        }
    }

    private var filter: some View {
        SectionBox(title: "Filter", width: 218, height: 156) {
            ZStack {
                knob(15, "A", 20, 44)
                knob(16, "D", 52, 44)
                knob(17, "S", 84, 44)
                knob(18, "R", 116, 44)
                knob(21, "amt", 150, 44)
                cycler(14, "type", 196, 30, w: 36)
                LedRadio(model: model, id: 14, options: radio(14, ["LP12", "LP24", "HP12", "BP12", "LPDL"]), rowHeight: 14)
                    .position(x: 196, y: 82)
                knob(19, "frq", 20, 106)
                knob(20, "res", 52, 106)
                knob(23, "sat", 84, 106)
                knob(22, "trk", 116, 106)
                toggle(24, "vel", 150, 112, w: 36)
            }
        }
    }

    private var arpeggiator: some View {
        SectionBox(title: "Arpeggiator", width: 218, height: 130) {
            ZStack {
                toggle(59, "ON", 22, 30, w: 32)
                cycler(31, "type", 66, 30, w: 40)
                cycler(32, "range", 128, 30, w: 44)
                LedRadio(model: model, id: 31, options: radio(31, ["updown", "up", "down", "random"], from: 1), rowHeight: 14)
                    .position(x: 40, y: 78)
                LedRadio(model: model, id: 32, options: radio(32, ["1oct", "2oct", "3oct", "4oct"]), rowHeight: 14)
                    .position(x: 126, y: 78)
                knob(33, "beat", 190, 36)
                knob(34, "gate", 190, 88)
            }
        }
    }

    private var effect: some View {
        SectionBox(title: "Effect", width: 216, height: 80) {
            ZStack {
                toggle(77, "ON", 22, 40, w: 32)
                display(78, 66, 40, w: 46, cycles: true, onTap: { model.showEffectMenu = true })
                knob(79, "ctl1", 118, 36)
                knob(80, "ctl2", 154, 36)
                knob(81, "level", 190, 36)
            }
        }
    }

    private var equalizer: some View {
        SectionBox(title: "Equalizer / Pan", width: 216, height: 58) {
            ZStack {
                knob(61, "freq", 24, 30, size: 22)
                knob(62, "level", 66, 30, size: 22)
                knob(63, "Q", 108, 30, size: 22)
                knob(60, "tone", 150, 30, size: 22)
                knob(90, "L-R", 192, 30, size: 22)
            }
        }
    }

    private var delay: some View {
        SectionBox(title: "Tempo Delay", width: 216, height: 58) {
            ZStack {
                toggle(65, "ON", 17, 32, w: 28)
                display(82, 48, 32, w: 28, cycles: true)
                knob(35, "time", 84, 30, size: 22)
                knob(83, "sprd", 111, 30, size: 22)
                knob(36, "fdbk", 138, 30, size: 22)
                knob(98, "tone", 165, 30, size: 22)
                knob(37, "d/w", 192, 30, size: 22)
            }
        }
    }

    private var chorus: some View {
        SectionBox(title: "Chorus / Flanger", width: 216, height: 58) {
            ZStack {
                toggle(66, "ON", 17, 32, w: 28)
                display(64, 48, 32, w: 28, cycles: true)
                knob(52, "time", 84, 30, size: 22)
                knob(53, "depth", 111, 30, size: 22)
                knob(54, "rate", 138, 30, size: 22)
                knob(55, "fdbk", 165, 30, size: 22)
                knob(56, "level", 192, 30, size: 22)
            }
        }
    }

    private var voice: some View {
        SectionBox(title: "Voice", width: 216, height: 174) {
            ZStack {
                LedRadio(model: model, id: 38, options: radio(38, ["poly", "mono", "legato"]), rowHeight: 14).position(x: 34, y: 46)
                display(94, 130, 38, w: 30)
                label("poly", 130, 55)
                toggle(73, "unison", 36, 100, w: 56)
                // Synth1: one "portamento" caption over the time knob + auto.
                label("portamento", 153, 77)
                knob(39, "", 128, 100)
                toggle(74, "auto", 178, 100, w: 38)
                display(93, 24, 138, w: 26)
                label("num", 24, 155)
                knob(75, "det", 68, 136)
                knob(92, "phase", 106, 136)
                knob(84, "sprd", 144, 136)
                knob(85, "pitch", 182, 136)
            }
        }
    }

    // MARK: - MIDI ctrl routing (Synth1's src -> assign words)

    private static let ccNames: [Int: String] = [
        1: "mod wheel", 2: "breath", 4: "foot", 5: "porta time", 7: "volume", 8: "balance", 10: "pan",
        11: "expression", 64: "sustain", 65: "portamento", 66: "sostenuto", 67: "soft", 71: "resonance",
        72: "release", 73: "attack", 74: "cutoff", 91: "reverb", 93: "chorus",
    ]

    static func ctrlSrcLabel(_ v: Int) -> String {
        switch v >> 8 {
        case 0xB0: return "#\(v & 0xFF):\(ccNames[v & 0xFF] ?? "cc")"
        case 0xE0: return "pitch bend"
        case 0xD0: return "ch pressure"
        default: return v <= 0 ? "none" : String(format: "0x%04X", v)
        }
    }

    private func ctrlSrc(_ id: Int) -> some View {
        let groups: [(title: String, items: [(value: Int, label: String)])] =
            [("", [(0xE000, "pitch bend"), (0xD000, "ch pressure")])] +
            stride(from: 0, to: 128, by: 32).map { base in
                ("CC \(base)–\(base + 31)", (base..<base + 32).map { (0xB000 | $0, Self.ctrlSrcLabel(0xB000 | $0)) })
            }
        return MenuDisplay(model: model, id: id, width: 120, label: Self.ctrlSrcLabel(model.values[id]), groups: groups)
    }

    /// Synth1's assign list, in its order (v1.12 manual): -1 = none.
    private static let assignTargets = [
        76, 45, 95, 2, 12, 13, 11, 5, 8, 91, 15, 16, 17, 18, 19, 20, 21, 23, 22, 25, 26, 27, 28, 29, 79, 80, 81,
        61, 62, 63, 60, 90, 35, 36, 37, 83, 98, 52, 53, 54, 55, 56, 39, 75, 92, 84, 85, 34, 43, 44, 48, 49,
    ]

    private func ctrlAssign(_ id: Int) -> some View {
        let v = model.values[id]
        let name = v < 0 ? "none" : v < SynthModel.paramCount ? model.host.name(v) : "\(v)"
        return MenuDisplay(model: model, id: id, width: 120, label: name,
                           groups: [("", [(-1, "none")] + Self.assignTargets.map { ($0, model.host.name($0)) })])
    }
}

/// Width mode: one of the two invisible side strips. A swipe here grabs
/// the panel 1:1 (drag down = panel moves down), a flick carries on and
/// eases out, clamped (no rubber band). The panel itself never scrolls:
/// its knobs own the vertical drags (Oxo Sample's scroll strips).
private struct PanelScrollStrip: View {
    let maxOffset: CGFloat
    @Binding var offset: CGFloat
    @State private var dragStart: CGFloat?

    var body: some View {
        // near-zero alpha: hit-testable everywhere, invisible
        Color.white.opacity(0.001)
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { g in
                    if dragStart == nil { dragStart = min(max(0, offset), maxOffset) }
                    guard let start = dragStart else { return }
                    offset = min(max(0, start - g.translation.height), maxOffset)
                }
                .onEnded { g in
                    defer { dragStart = nil }
                    guard let start = dragStart else { return }
                    let end = min(max(0, start - g.predictedEndTranslation.height), maxOffset)
                    withAnimation(.easeOut(duration: 0.45)) { offset = end }
                })
    }
}

/// iPad UI mode toggle (title band, left of the version box): vertical
/// arrow lit = height mode (the whole panel fits), horizontal lit = width
/// mode (the width leads, the panel scrolls).
private struct UIModeToggle: View {
    @ObservedObject var model: SynthModel
    @Environment(\.theme) private var theme

    var body: some View {
        let wide = model.uiWidthMode
        ZStack {
            // the dim one shorter: its heads stay clear of the lit one's
            DoubleArrow(horizontal: !wide, length: 0.7)
                .stroke(theme.ink.opacity(0.4), style: StrokeStyle(lineWidth: 1.1, lineCap: .round, lineJoin: .round))
            DoubleArrow(horizontal: wide)
                .stroke(theme.ink, style: StrokeStyle(lineWidth: 2, lineCap: .round, lineJoin: .round))
        }
        .frame(width: 22, height: 22)
        .padding(.horizontal, 6)
        .contentShape(Rectangle())
        .gesture(DragGesture(minimumDistance: 0).onEnded { g in
            if hypot(g.translation.width, g.translation.height) < 8 { model.toggleUIWidthMode() }
        })
    }
}

private struct DoubleArrow: Shape {
    let horizontal: Bool
    /// Fraction of the full half-length.
    var length: CGFloat = 1

    func path(in rect: CGRect) -> Path {
        let c = CGPoint(x: rect.midX, y: rect.midY), r = (min(rect.width, rect.height) / 2 - 1.5) * length
        let head: CGFloat = length < 1 ? 2.2 : 2.8
        var p = Path()
        p.move(to: CGPoint(x: c.x, y: c.y - r))
        p.addLine(to: CGPoint(x: c.x, y: c.y + r))
        for tip in [-r, r] {
            let back = tip < 0 ? head : -head
            p.move(to: CGPoint(x: c.x - head, y: c.y + tip + back))
            p.addLine(to: CGPoint(x: c.x, y: c.y + tip))
            p.addLine(to: CGPoint(x: c.x + head, y: c.y + tip + back))
        }
        guard horizontal else { return p }
        return p.applying(CGAffineTransform(translationX: c.x, y: c.y).rotated(by: .pi / 2).translatedBy(x: -c.x, y: -c.y))
    }
}

extension View {
    /// Frame + place by top-left corner in the parent's coordinates.
    func place(_ x: CGFloat, _ y: CGFloat, _ w: CGFloat, _ h: CGFloat) -> some View {
        frame(width: w, height: h).position(x: x + w / 2, y: y + h / 2)
    }
}

/// Effect type pick list under its display (Synth1's 2 columns of 5): a
/// direct pick instead of cycling through the distortions. Display colours.
struct EffectTypeMenu: View {
    private static let id = 78, cell = CGSize(width: 46, height: 17)
    @ObservedObject var model: SynthModel
    @Environment(\.theme) private var theme
    @State private var hovered: Int?

    var body: some View {
        HStack(spacing: 0) {
            ForEach(0..<2, id: \.self) { col in
                VStack(spacing: 0) {
                    ForEach(0..<5, id: \.self) { row in item(col * 5 + row) }
                }
            }
        }
        .background(theme.displayFill)
        .overlay(Rectangle().strokeBorder(Color.black.opacity(0.7), lineWidth: 1))
        .shadow(color: .black.opacity(0.5), radius: 6, y: 3)
        #if os(macOS)
        .background(WheelShield())
        #endif
    }

    private func item(_ v: Int) -> some View {
        let on = model.values[Self.id] == v
        return Text(model.host.format(Self.id, v))
            .font(.system(size: 11, weight: .bold, design: .monospaced))
            .foregroundColor(on ? theme.lcdBottom : theme.displayText)
            .frame(width: Self.cell.width, height: Self.cell.height)
            .background(on ? theme.displayText : hovered == v ? theme.displayText.opacity(0.2) : Color.clear)
            .overlay(Rectangle().strokeBorder(Color.black.opacity(0.35), lineWidth: 0.5))
            .contentShape(Rectangle())
            .onHover { inside in
                if inside { hovered = v } else if hovered == v { hovered = nil }
            }
            .onTapGesture {
                model.showEffectMenu = false
                model.setOnce(Self.id, v)
                model.show(Self.id)
            }
    }
}
