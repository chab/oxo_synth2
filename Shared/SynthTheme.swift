import Combine
import SwiftUI
#if os(macOS)
import AppKit
#endif

/// The 13 user-editable panel colours (menu > Themes…). Everything else on
/// the panel is derived from them (off LEDs, knob rim / ticks, section
/// edges) or fixed (the red write / version boxes, the learn blue).
enum ThemeColor: String, CaseIterable, Codable {
    case panel, header, headerText, label, knobBody, knobGradient, knobDial, button, led, radio, displayText, displayTop,
         displayBottom

    var title: String {
        switch self {
        case .panel: return "Panel"
        case .header: return "Section header"
        case .headerText: return "Header text"
        case .label: return "Labels"
        case .knobBody: return "Knob body"
        case .knobGradient: return "Knob gradient"
        case .knobDial: return "Knob dial"
        case .button: return "Buttons"
        case .led: return "Button LED"
        case .radio: return "Radio LED"
        case .displayText: return "Display text"
        case .displayTop: return "Display top"
        case .displayBottom: return "Display bottom"
        }
    }

    /// The "Synth2" factory theme (the default).
    var defaultHex: String {
        switch self {
        case .panel: return "#B7B79B"
        case .header: return "#3B3B38"
        case .headerText: return "#E6E0B8"
        case .label: return "#1A1A0F"
        case .knobBody: return "#171717"
        case .knobGradient: return "#212121"
        case .knobDial: return "#FFFFFF"
        case .button: return "#363630"
        case .led: return "#5CF266"
        case .radio: return "#FF331A"
        case .displayText: return "#73F2FF"
        case .displayTop: return "#123D40"
        case .displayBottom: return "#05171A"
        }
    }
}

struct RGB: Codable, Equatable {
    var r: Double, g: Double, b: Double

    init(r: Double, g: Double, b: Double) { (self.r, self.g, self.b) = (r, g, b) }

    init?(hex: String) {
        let s = hex.trimmingCharacters(in: CharacterSet(charactersIn: "# "))
        guard s.count == 6, let v = UInt32(s, radix: 16) else { return nil }
        self.init(r: Double(v >> 16 & 0xFF) / 255, g: Double(v >> 8 & 0xFF) / 255, b: Double(v & 0xFF) / 255)
    }

    var hex: String {
        String(format: "#%02X%02X%02X", Int((r * 255).rounded()), Int((g * 255).rounded()), Int((b * 255).rounded()))
    }

    var color: Color { Color(red: r, green: g, blue: b) }
    func scaled(_ k: Double) -> Color { Color(red: r * k, green: g * k, blue: b * k) }

    init(_ color: Color) {
        #if os(macOS)
        let c = NSColor(color).usingColorSpace(.sRGB) ?? .black
        self.init(r: Double(c.redComponent), g: Double(c.greenComponent), b: Double(c.blueComponent))
        #else
        var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
        UIColor(color).getRed(&r, green: &g, blue: &b, alpha: &a)
        self.init(r: Double(r), g: Double(g), b: Double(b))
        #endif
    }
}

struct ThemePalette: Equatable {
    var colors: [ThemeColor: RGB]

    static let synth2 = ThemePalette(colors: Dictionary(uniqueKeysWithValues: ThemeColor.allCases.map {
        ($0, RGB(hex: $0.defaultHex)!)
    }))

    /// Factory themes shipped with the plugin (the user's own designs);
    /// "Synth2" is the default, "Synth1" is the closest to the original.
    static let factory: [(name: String, palette: ThemePalette)] = [
        ("Synth2", .synth2),
        ("Blue", ThemePalette(hexMap: [
            "panel": "#8AA7B8", "header": "#1A1D26", "headerText": "#9DA198", "label": "#181A19",
            "knobBody": "#171717", "knobGradient": "#212121", "knobDial": "#FFFFFF", "button": "#363630",
            "led": "#5CF266", "radio": "#A4DE87", "displayText": "#73F2FF", "displayTop": "#24272F",
            "displayBottom": "#1C1F26"])),
        ("Gray Dark", ThemePalette(hexMap: [
            "panel": "#3F434D", "header": "#5F5F60", "headerText": "#D1D5D7", "label": "#E4E7EC",
            "knobBody": "#100F0F", "knobGradient": "#151516", "knobDial": "#B5B6B9", "button": "#363630",
            "led": "#8ED9B5", "radio": "#F3F9F1", "displayText": "#C2D3E1", "displayTop": "#161A1A",
            "displayBottom": "#0D100C"])),
        ("Gray Light", ThemePalette(hexMap: [
            "panel": "#CECEC0", "header": "#3B3B38", "headerText": "#E6E0B8", "label": "#1A1A0F",
            "knobBody": "#171717", "knobGradient": "#212121", "knobDial": "#FFFFFF", "button": "#363630",
            "led": "#5CF266", "radio": "#FF331A", "displayText": "#73F2FF", "displayTop": "#123D40",
            "displayBottom": "#05171A"])),
        ("Red", ThemePalette(hexMap: [
            "panel": "#A71818", "header": "#474747", "headerText": "#FFFFFF", "label": "#FFFFFF",
            "knobBody": "#171717", "knobGradient": "#212121", "knobDial": "#FFFFFF", "button": "#363630",
            "led": "#5CF266", "radio": "#A4DE87", "displayText": "#73F2FF", "displayTop": "#24272F",
            "displayBottom": "#1C1F26"])),
        ("Warm", ThemePalette(hexMap: [
            "panel": "#313336", "header": "#B78F0F", "headerText": "#000000", "label": "#E4E7EC",
            "knobBody": "#1B1B1B", "knobGradient": "#181818", "knobDial": "#B5B6B9", "button": "#363630",
            "led": "#8ED9B5", "radio": "#F3F9F1", "displayText": "#C2D3E1", "displayTop": "#161A1A",
            "displayBottom": "#0D100C"])),
        ("JP-8000", ThemePalette(hexMap: [
            "panel": "#304354", "header": "#24262C", "headerText": "#C6772D", "label": "#E4E7EC",
            "knobBody": "#1B1B1B", "knobGradient": "#181818", "knobDial": "#B5B6B9", "button": "#363630",
            "led": "#FAFFF2", "radio": "#F5FBF3", "displayText": "#C2D3E1", "displayTop": "#161A1A",
            "displayBottom": "#0D100C"])),
        ("JP-8000 Dark", ThemePalette(hexMap: [
            "panel": "#191D25", "header": "#24262C", "headerText": "#C6772D", "label": "#E4E7EC",
            "knobBody": "#1B1B1B", "knobGradient": "#181818", "knobDial": "#B5B6B9", "button": "#363630",
            "led": "#FAFFF2", "radio": "#F5FBF3", "displayText": "#C2D3E1", "displayTop": "#161A1A",
            "displayBottom": "#0D100C"])),
        ("Synth1", ThemePalette(hexMap: [
            "panel": "#C7C786", "header": "#3B3B38", "headerText": "#E6E0B8", "label": "#1A1A0F",
            "knobBody": "#171717", "knobGradient": "#212121", "knobDial": "#FF2D2D", "button": "#363630",
            "led": "#5CF266", "radio": "#FF331A", "displayText": "#73F2FF", "displayTop": "#123D40",
            "displayBottom": "#05171A"])),
    ]

    subscript(_ c: ThemeColor) -> RGB { colors[c] ?? RGB(hex: c.defaultHex)! }

    var hexMap: [String: String] { Dictionary(uniqueKeysWithValues: ThemeColor.allCases.map { ($0.rawValue, self[$0].hex) }) }

    init(colors: [ThemeColor: RGB]) { self.colors = colors }

    init(hexMap: [String: String]) {
        var colors = ThemePalette.synth2.colors
        for (k, v) in hexMap {
            if let c = ThemeColor(rawValue: k), let rgb = RGB(hex: v) { colors[c] = rgb }
        }
        self.colors = colors
    }

    /// The session-state form (`SynthHost.themeText`): the hex map as
    /// compact JSON, keys sorted.
    var stateText: String {
        let encoder = JSONEncoder()
        encoder.outputFormatting = .sortedKeys
        return (try? encoder.encode(hexMap)).flatMap { String(data: $0, encoding: .utf8) } ?? ""
    }

    /// nil = empty / not a hex map (the session then keeps the editor's).
    init?(stateText: String) {
        guard let data = stateText.data(using: .utf8), let map = try? JSONDecoder().decode([String: String].self, from: data)
        else { return nil }
        self.init(hexMap: map)
    }
}

/// Per-user theme state: the saved themes and the palette a NEW instance
/// starts with (the last one picked / edited in any instance), in
/// `<Application Support>/<OXO_DATA_FOLDER>/themes.json` — internal, never
/// an export format. Each instance wears its own palette (`SynthModel
/// .palette`, kept in the session), so a change never reaches the others.
final class ThemeStore: ObservableObject {
    static let shared = ThemeStore()

    private(set) var palette: ThemePalette
    @Published private(set) var saved: [(name: String, palette: ThemePalette)] = []
    private var pendingSave: DispatchWorkItem?

    /// User themes not already identical to a factory theme of that name.
    var userThemes: [(name: String, palette: ThemePalette)] {
        saved.filter { item in !ThemePalette.factory.contains { $0.name == item.name && $0.palette == item.palette } }
    }

    private struct File: Codable {
        struct Saved: Codable { var name: String; var colors: [String: String] }
        var current: [String: String]
        var saved: [Saved]
    }

    private let url: URL = {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? URL(fileURLWithPath: NSTemporaryDirectory())
        return base.appendingPathComponent(OXO_DATA_FOLDER).appendingPathComponent("themes.json")
    }()

    private init() {
        palette = .synth2
        load()
    }

    private func load() {
        guard let data = try? Data(contentsOf: url), let file = try? JSONDecoder().decode(File.self, from: data) else { return }
        palette = ThemePalette(hexMap: file.current)
        saved = file.saved.map { ($0.name, ThemePalette(hexMap: $0.colors)) }
    }

    /// The palette of an instance without a session theme. Re-reads the
    /// file: the last pick may come from another process (host sandboxing).
    func startPalette() -> ThemePalette {
        if pendingSave != nil { persist() } else { load() }
        return palette
    }

    /// An instance's palette changed: the default for the next instances.
    /// The colour panel streams changes while dragging: write once it
    /// settles.
    func remember(_ p: ThemePalette) {
        guard palette != p else { return }
        palette = p
        pendingSave?.cancel()
        let work = DispatchWorkItem { [weak self] in self?.persist() }
        pendingSave = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.4, execute: work)
    }

    /// Same name = replaced.
    func save(_ p: ThemePalette, as name: String) {
        let n = name.trimmingCharacters(in: .whitespaces)
        guard !n.isEmpty else { return }
        saved.removeAll { $0.name == n }
        saved.append((n, p))
        saved.sort { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
        persist()
    }

    func delete(_ name: String) {
        saved.removeAll { $0.name == name }
        persist()
    }

    private func persist() {
        pendingSave?.cancel()
        pendingSave = nil
        let file = File(current: palette.hexMap, saved: saved.map { .init(name: $0.name, colors: $0.palette.hexMap) })
        try? FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        if let data = try? JSONEncoder().encode(file) { try? data.write(to: url, options: .atomic) }
    }
}

extension RGB {
    init(h: Double, s: Double, v: Double) {
        let i = Int(h * 6) % 6, f = h * 6 - Double(Int(h * 6))
        let p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s)
        switch i {
        case 0: self.init(r: v, g: t, b: p)
        case 1: self.init(r: q, g: v, b: p)
        case 2: self.init(r: p, g: v, b: t)
        case 3: self.init(r: p, g: q, b: v)
        case 4: self.init(r: t, g: p, b: v)
        default: self.init(r: v, g: p, b: q)
        }
    }

    var hsv: (h: Double, s: Double, v: Double) {
        let mx = max(r, g, b), mn = min(r, g, b), d = mx - mn
        var h = 0.0
        if d > 0 {
            if mx == r { h = (g - b) / d } else if mx == g { h = (b - r) / d + 2 } else { h = (r - g) / d + 4 }
            h /= 6
            if h < 0 { h += 1 }
        }
        return (h, mx > 0 ? d / mx : 0, mx)
    }
}

/// Fixed dark chrome of the in-panel dialogs (not themed, so it stays
/// readable whatever palette is being tuned).
enum Dlg {
    static let bg = Color(white: 0.17)
    static let bar = Color(white: 0.24)
    static let field = Color(white: 0.11)
    static let text = Color(white: 0.9)
    static let dim = Color(white: 0.6)
    static let accent = Color(red: 0.35, green: 0.65, blue: 0.91)
    static let font = Font.system(size: 10)
}

/// The Themes dialog, drawn INSIDE the plugin panel (same on mac VST3 /
/// AUv2 and iPad AUv3): drag it by its title bar to uncover what is being
/// tuned. Swatch = pick the colour to edit; the picker below edits it
/// live; Load / Delete / Save named themes.
struct ThemeDialog: View {
    static let width: CGFloat = 330

    @ObservedObject var model: SynthModel
    @ObservedObject var store = ThemeStore.shared
    /// Top-left corner in canvas coordinates.
    @Binding var position: CGPoint
    let canvas: CGSize
    @State private var selected: ThemeColor = .panel
    @State private var name = ""
    @State private var dragStart: CGPoint?
    @State private var height: CGFloat = 380

    var body: some View {
        VStack(spacing: 0) {
            titleBar
            VStack(alignment: .leading, spacing: 8) {
                // Theme name / Load / Save first: on iPad the keyboard covers
                // the bottom of the panel, a field up here stays visible.
                HStack(spacing: 6) {
                    menu("Load") {
                        ForEach(ThemePalette.factory, id: \.name) { item in
                            Button(item.name == "Synth2" ? "Synth2 (default)" : item.name) {
                                model.applyTheme(item.palette)
                                name = item.name
                            }
                        }
                        if !store.userThemes.isEmpty { Divider() }
                        ForEach(store.userThemes, id: \.name) { item in
                            Button(item.name) {
                                model.applyTheme(item.palette)
                                name = item.name
                            }
                        }
                    }
                    menu("Delete") {
                        ForEach(store.userThemes, id: \.name) { item in Button(item.name) { store.delete(item.name) } }
                    }
                    .disabled(store.userThemes.isEmpty)
                    .opacity(store.userThemes.isEmpty ? 0.4 : 1)
                    Spacer()
                }
                HStack(spacing: 6) {
                    TextField("theme name", text: $name)
                        .textFieldStyle(.plain).font(Dlg.font).foregroundColor(Dlg.text)
                        .padding(.horizontal, 6).frame(height: 20)
                        .background(RoundedRectangle(cornerRadius: 3).fill(Dlg.field))
                        .onSubmit { store.save(model.palette, as: name) }
                    chip("Save") { store.save(model.palette, as: name) }
                        .disabled(name.trimmingCharacters(in: .whitespaces).isEmpty)
                }
                Rectangle().fill(Color.white.opacity(0.12)).frame(height: 1)
                swatches
                ColorEditor(rgb: Binding(get: { model.palette[selected] }, set: { model.setThemeColor(selected, $0) }))
                    // fresh hue / saturation state per colour and per loaded theme
                    .id("\(selected.rawValue)#\(model.themeRevision)")
            }
            .padding(10)
        }
        .frame(width: Self.width)
        .background(GeometryReader { g in Color.clear.onAppear { height = g.size.height } })
        .background(RoundedRectangle(cornerRadius: 8).fill(Dlg.bg))
        .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(Color.black.opacity(0.7), lineWidth: 1))
        .clipShape(RoundedRectangle(cornerRadius: 8))
        .shadow(color: .black.opacity(0.5), radius: 10, y: 4)
        #if os(macOS)
        .background(WheelShield())
        #endif
        .offset(x: position.x, y: position.y)
    }

    private var titleBar: some View {
        ZStack {
            Text("Synth2 Themes").font(.system(size: 11, weight: .semibold)).foregroundColor(Dlg.text)
            HStack {
                Circle().fill(Color(red: 1, green: 0.37, blue: 0.34)).frame(width: 11, height: 11)
                    .overlay(Image(systemName: "xmark").font(.system(size: 6, weight: .bold)).foregroundColor(.black.opacity(0.6)))
                    .padding(6)
                    .contentShape(Rectangle())
                    .onTapGesture { model.showThemes = false }
                Spacer()
            }
        }
        .frame(height: 22)
        .frame(maxWidth: .infinity)
        .background(Dlg.bar)
        .contentShape(Rectangle())
        .gesture(DragGesture(coordinateSpace: .named(SynthPanelView.canvasSpace))
            .onChanged { g in
                let start = dragStart ?? position
                dragStart = start
                position = CGPoint(x: min(max(start.x + g.translation.width, 0), canvas.width - Self.width),
                                   y: min(max(start.y + g.translation.height, 0), canvas.height - min(height, 60)))
            }
            .onEnded { _ in dragStart = nil })
    }

    private var swatches: some View {
        Grid(alignment: .leading, horizontalSpacing: 10, verticalSpacing: 4) {
            ForEach(Array(stride(from: 0, to: ThemeColor.allCases.count, by: 2)), id: \.self) { i in
                GridRow {
                    swatch(ThemeColor.allCases[i])
                    if i + 1 < ThemeColor.allCases.count { swatch(ThemeColor.allCases[i + 1]) }
                }
            }
        }
    }

    private func swatch(_ c: ThemeColor) -> some View {
        HStack(spacing: 6) {
            RoundedRectangle(cornerRadius: 3).fill(model.palette[c].color)
                .overlay(RoundedRectangle(cornerRadius: 3)
                    .strokeBorder(selected == c ? Dlg.accent : Color.white.opacity(0.25), lineWidth: selected == c ? 2 : 1))
                .frame(width: 34, height: 17)
            Text(c.title).font(Dlg.font).foregroundColor(selected == c ? Dlg.text : Dlg.dim)
        }
        .frame(width: 150, alignment: .leading)
        .contentShape(Rectangle())
        .onTapGesture { selected = c }
    }

    private func chip(_ title: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(title).font(Dlg.font).foregroundColor(Dlg.text)
                .frame(width: 56, height: 20)
                .background(RoundedRectangle(cornerRadius: 3).fill(Dlg.bar))
        }
        .buttonStyle(.plain)
    }

    private func menu<C: View>(_ title: String, @ViewBuilder content: () -> C) -> some View {
        Menu(content: content) {
            HStack(spacing: 4) {
                Text(title).font(Dlg.font)
                Image(systemName: "chevron.down").font(.system(size: 7, weight: .bold))
            }
            .foregroundColor(Dlg.text)
            .frame(width: 70, height: 20)
            .background(RoundedRectangle(cornerRadius: 3).fill(Dlg.bar))
        }
        .menuStyle(.button)
        .buttonStyle(.plain)
        .menuIndicator(.hidden)
        .fixedSize()
    }
}

/// Click on the "Synth2" logo: UI sizes left (percent of
/// `SynthPanelView.unitWidth`; only where the editor can resize its window =
/// mac VST3 / AUv2, not the iPad AUv3), themes right (factory, then the
/// user's). A pick applies and closes, like Oxo Drums' logo menu.
struct LogoMenu: View {
    static let sizes = [75, 100, 115, 125, 140, 150, 200]
    private static let rowHeight: CGFloat = 20

    @ObservedObject var model: SynthModel
    @ObservedObject var store = ThemeStore.shared
    /// The live editor scale (window width / design width).
    let scale: CGFloat
    @State private var hovered: String?

    var body: some View {
        // The window is freely resizable: the active row is the nearest step.
        let unit = scale * SynthPanelView.designSize.width / SynthPanelView.unitWidth
        let size = Self.sizes.min { abs(CGFloat($0) / 100 - unit) < abs(CGFloat($1) / 100 - unit) }
        let theme = ThemePalette.factory.first { $0.palette == model.palette }.map { "f:" + $0.name }
            ?? store.userThemes.first { $0.palette == model.palette }.map { "u:" + $0.name }
        HStack(alignment: .top, spacing: 0) {
            if let resize = model.requestEditorWidth {
                column("UI size") {
                    ForEach(Self.sizes, id: \.self) { p in
                        row("s:\(p)", "\(p)%", active: p == size) {
                            resize(Double(SynthPanelView.unitWidth) * Double(p) / 100)
                        }
                    }
                }
                Rectangle().fill(Color.white.opacity(0.12)).frame(width: 1).padding(.vertical, 6)
            }
            column("Theme") {
                ForEach(ThemePalette.factory, id: \.name) { t in
                    row("f:" + t.name, t.name, active: theme == "f:" + t.name) { model.applyTheme(t.palette) }
                }
                if !store.userThemes.isEmpty {
                    Rectangle().fill(Color.white.opacity(0.12)).frame(height: 1).padding(.vertical, 3)
                }
                ForEach(store.userThemes, id: \.name) { t in
                    row("u:" + t.name, t.name, active: theme == "u:" + t.name) { model.applyTheme(t.palette) }
                }
            }
        }
        .fixedSize(horizontal: false, vertical: true) // the divider is greedy
        .padding(4)
        .background(RoundedRectangle(cornerRadius: 6).fill(Dlg.bg))
        .overlay(RoundedRectangle(cornerRadius: 6).strokeBorder(Color.black.opacity(0.7), lineWidth: 1))
        .clipShape(RoundedRectangle(cornerRadius: 6))
        .shadow(color: .black.opacity(0.5), radius: 10, y: 4)
        #if os(macOS)
        .background(WheelShield())
        #endif
    }

    private func column<C: View>(_ title: String, @ViewBuilder content: () -> C) -> some View {
        VStack(alignment: .leading, spacing: 1) {
            Text(title).font(.system(size: 9, weight: .semibold)).foregroundColor(Dlg.dim)
                .padding(.horizontal, 8).frame(height: 16)
            content()
        }
        .frame(width: 120, alignment: .leading)
        .padding(.horizontal, 2)
    }

    private func row(_ id: String, _ title: String, active: Bool, action: @escaping () -> Void) -> some View {
        Text(title).font(.system(size: 11, weight: active ? .semibold : .regular))
            .foregroundColor(active ? .white : Dlg.text)
            .lineLimit(1).truncationMode(.tail)
            .padding(.horizontal, 8)
            .frame(maxWidth: .infinity, minHeight: Self.rowHeight, maxHeight: Self.rowHeight, alignment: .leading)
            .background(RoundedRectangle(cornerRadius: 3)
                .fill(active ? Dlg.accent : hovered == id ? Color.white.opacity(0.1) : Color.clear))
            .contentShape(Rectangle())
            .onHover { inside in
                if inside { hovered = id } else if hovered == id { hovered = nil }
            }
            .onTapGesture {
                model.showLogoMenu = false
                action()
            }
    }
}

/// Saturation / brightness square + hue bar + hex field, editing one colour
/// live. Hue and saturation are kept locally so they survive black / grey.
private struct ColorEditor: View {
    @Binding var rgb: RGB
    @State private var h: Double
    @State private var s: Double
    @State private var v: Double
    @State private var hex: String

    init(rgb: Binding<RGB>) {
        _rgb = rgb
        let c = rgb.wrappedValue.hsv
        _h = State(initialValue: c.h)
        _s = State(initialValue: c.s)
        _v = State(initialValue: c.v)
        _hex = State(initialValue: rgb.wrappedValue.hex)
    }

    var body: some View {
        HStack(alignment: .top, spacing: 10) {
            VStack(spacing: 6) {
                svSquare.frame(width: 210, height: 84)
                hueBar.frame(width: 210, height: 12)
            }
            VStack(alignment: .leading, spacing: 6) {
                RoundedRectangle(cornerRadius: 3).fill(rgb.color)
                    .overlay(RoundedRectangle(cornerRadius: 3).strokeBorder(Color.white.opacity(0.25), lineWidth: 1))
                    .frame(width: 90, height: 56)
                TextField("#RRGGBB", text: $hex)
                    .textFieldStyle(.plain)
                    .font(.system(size: 10, design: .monospaced)).foregroundColor(Dlg.text)
                    .padding(.horizontal, 6).frame(width: 90, height: 20)
                    .background(RoundedRectangle(cornerRadius: 3).fill(Dlg.field))
                    .onSubmit {
                        guard let c = RGB(hex: hex) else { return hex = rgb.hex }
                        rgb = c
                        (h, s, v) = c.hsv
                        hex = c.hex
                    }
            }
        }
    }

    private func push() {
        rgb = RGB(h: h, s: s, v: v)
        hex = rgb.hex
    }

    private var svSquare: some View {
        GeometryReader { g in
            ZStack {
                Color(hue: h, saturation: 1, brightness: 1)
                LinearGradient(colors: [.white, .white.opacity(0)], startPoint: .leading, endPoint: .trailing)
                LinearGradient(colors: [.black.opacity(0), .black], startPoint: .top, endPoint: .bottom)
                Circle().strokeBorder(Color.white, lineWidth: 1.5)
                    .background(Circle().strokeBorder(Color.black.opacity(0.5), lineWidth: 2.5))
                    .frame(width: 10, height: 10)
                    .position(x: s * g.size.width, y: (1 - v) * g.size.height)
            }
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0).onChanged { d in
                s = min(max(d.location.x / g.size.width, 0), 1)
                v = 1 - min(max(d.location.y / g.size.height, 0), 1)
                push()
            })
        }
        .clipShape(RoundedRectangle(cornerRadius: 3))
    }

    private var hueBar: some View {
        GeometryReader { g in
            ZStack(alignment: .leading) {
                LinearGradient(colors: stride(from: 0.0, through: 1.0, by: 1.0 / 6).map { Color(hue: $0, saturation: 1, brightness: 1) },
                               startPoint: .leading, endPoint: .trailing)
                RoundedRectangle(cornerRadius: 2).strokeBorder(Color.white, lineWidth: 1.5)
                    .frame(width: 6, height: g.size.height + 2)
                    .offset(x: h * (g.size.width - 6))
            }
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0).onChanged { d in
                h = min(max(d.location.x / g.size.width, 0), 0.9999)
                push()
            })
        }
        .clipShape(RoundedRectangle(cornerRadius: 3))
    }
}
