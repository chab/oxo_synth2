import SwiftUI

/// TEMPORARY render diagnosis (Logic slows down while the editor is on
/// screen, Oxo Steps' method: remove one kind of drawing at a time). Chips in
/// the title band; a lit chip = that drawing is gone (`group`: ADDS a
/// compositing group, the Oxo Steps fix candidate). Persisted per user so an
/// editor reopens with the same set. Remove with the chips once diagnosed.
enum RenderProbe: String, CaseIterable {
    case all, voices, knobs, buttons, leds, waves, displays, lcd
    case labels, boxes, effects, gradients, wheel, tips, poll, group

    static let chipsEnabled = true

    var chip: String {
        switch self {
        case .all: "ALL"
        case .voices: "VOI"
        case .knobs: "KNB"
        case .buttons: "BTN"
        case .leds: "LED"
        case .waves: "WAV"
        case .displays: "DSP"
        case .lcd: "LCD"
        case .labels: "TXT"
        case .boxes: "BOX"
        case .effects: "FX"
        case .gradients: "GRAD"
        case .wheel: "WHL"
        case .tips: "TIP"
        case .poll: "POLL"
        case .group: "+GRP"
        }
    }

    private static let defaultsKey = "oxo.synth2.renderProbes"

    static var saved: Set<RenderProbe> {
        get {
            guard chipsEnabled else { return [] }
            return Set((UserDefaults.standard.stringArray(forKey: defaultsKey) ?? []).compactMap(RenderProbe.init))
        }
        set { UserDefaults.standard.set(newValue.map(\.rawValue).sorted(), forKey: defaultsKey) }
    }
}

private struct RenderProbesKey: EnvironmentKey {
    static let defaultValue: Set<RenderProbe> = []
}

extension EnvironmentValues {
    var renderProbes: Set<RenderProbe> {
        get { self[RenderProbesKey.self] }
        set { self[RenderProbesKey.self] = newValue }
    }
}

/// The probe set: removes the content (nothing drawn, nothing hit-tested).
private struct ProbeGate: ViewModifier {
    let probe: RenderProbe
    @Environment(\.renderProbes) private var probes
    func body(content: Content) -> some View {
        if probes.contains(probe) { Color.clear } else { content }
    }
}

private struct ProbeShadow: ViewModifier {
    let color: Color, radius: CGFloat, x: CGFloat, y: CGFloat
    @Environment(\.renderProbes) private var probes
    func body(content: Content) -> some View {
        if probes.contains(.effects) { content } else { content.shadow(color: color, radius: radius, x: x, y: y) }
    }
}

private struct ProbeBlur: ViewModifier {
    let radius: CGFloat
    @Environment(\.renderProbes) private var probes
    func body(content: Content) -> some View {
        if probes.contains(.effects) { content } else { content.blur(radius: radius) }
    }
}

/// TIP: no tooltip host; +GRP: a compositing group (after the scale).
private struct ProbeHost: ViewModifier {
    let tips: Bool
    @Environment(\.renderProbes) private var probes
    func body(content: Content) -> some View {
        if tips {
            if probes.contains(.tips) { content } else { content.oxoTooltipHost() }
        } else if probes.contains(.group) {
            content.compositingGroup()
        } else {
            content
        }
    }
}

extension View {
    func probeTooltipHost() -> some View { modifier(ProbeHost(tips: true)) }
    func probeCompositingGroup() -> some View { modifier(ProbeHost(tips: false)) }
    func probe(_ p: RenderProbe) -> some View { modifier(ProbeGate(probe: p)) }
    func probeShadow(color: Color, radius: CGFloat, x: CGFloat = 0, y: CGFloat = 0) -> some View {
        modifier(ProbeShadow(color: color, radius: radius, x: x, y: y))
    }
    func probeBlur(radius: CGFloat) -> some View { modifier(ProbeBlur(radius: radius)) }
}

/// Two rows of chips; click toggles.
struct RenderProbeChips: View {
    @ObservedObject var model: SynthModel
    static let size = CGSize(width: 8 * 29 + 7 * 2, height: 2 * 11 + 2)

    var body: some View {
        let all = RenderProbe.allCases
        VStack(spacing: 2) {
            ForEach(0..<2, id: \.self) { row in
                HStack(spacing: 2) {
                    ForEach(all[row * 8..<min(all.count, row * 8 + 8)], id: \.self) { chip($0) }
                }
            }
        }
        .frame(width: Self.size.width, height: Self.size.height)
    }

    private func chip(_ p: RenderProbe) -> some View {
        let lit = model.renderOff.contains(p)
        return Text(p.chip)
            .font(.system(size: 7.5, weight: .bold, design: .monospaced))
            .foregroundColor(lit ? .white : Color(white: 0.8))
            .frame(width: 29, height: 11)
            .background(lit ? (p == .group ? Color(red: 0.1, green: 0.5, blue: 0.2) : Theme.red) : Color(white: 0.25))
            .overlay(Rectangle().strokeBorder(Color.black, lineWidth: 0.5))
            .contentShape(Rectangle())
            .onTapGesture { model.toggleProbe(p) }
    }
}
