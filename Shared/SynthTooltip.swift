//
//  SynthTooltip.swift
//  App-drawn tooltips, Oxo Drums' OxoTooltip.swift (its timing and API).
//
//  One HOST per canvas (`.oxoTooltipHost()`: the center in the environment,
//  the bubble drawn over everything, hit-testing off) and any number of
//  sources: `.oxoTooltip(text)` always, `.oxoTruncationTooltip(text, ...)`
//  only while the Text it decorates is cut (`lineLimit(1)` + truncation —
//  the full width is measured once, when the pointer enters). Synth2 uses
//  only the latter (the browser's cut patch / bank names): no help tips
//  (user, 2026-10-31).
//
//  Timing: the first tip waits 0.5 s (Drums' value); while
//  WARM — a tip on screen, or one closed less than 5 s ago — the next shows
//  at once. A click or a key hides the tip (a scroll too, except over a
//  help tip: it turns that control), and its source stays quiet until the
//  pointer leaves. A tip follows its source's text (live values).
//
//  Every source says whether it is OPTIONAL (a help hint, Drums' "Show help
//  tooltips") or not (it shows something the UI cannot: a cut patch name).
//  Synth2's host keeps the optional ones off.
//

import SwiftUI
#if canImport(UIKit)
import UIKit
#else
import AppKit
#endif

@MainActor @Observable
final class OxoTooltipCenter {
    struct Tip: Equatable {
        let owner: UUID
        var text: String
        /// The pointer when the tip was asked for, in the host's space.
        let location: CGPoint
        let optional: Bool
    }

    static let initialDelay: Duration = .milliseconds(500)
    static let warmWindow: TimeInterval = 5
    static let coordinateSpace = "OxoTooltipSpace"

    private(set) var tip: Tip?
    /// Off = the OPTIONAL (help) tips never show (Synth2: always off).
    var showsOptionalTips = true {
        didSet { if !showsOptionalTips, tip?.optional == true { dismiss() } }
    }
    @ObservationIgnored private var pending: Task<Void, Never>?
    @ObservationIgnored private var pendingOwner: UUID?
    @ObservationIgnored private var pendingTip: Tip?
    @ObservationIgnored private var warmUntil = Date.distantPast
    @ObservationIgnored private var silenced: UUID?
#if !canImport(UIKit)
    @ObservationIgnored private var eventMonitor: Any?
#endif

    func hoverBegan(_ next: Tip) {
        guard silenced != next.owner, showsOptionalTips || !next.optional else { return }
        cancelPending()
        if tip != nil || Date() < warmUntil {
            show(next)
            return
        }
        pendingOwner = next.owner
        pendingTip = next
        pending = Task { [weak self] in
            try? await Task.sleep(for: Self.initialDelay)
            guard !Task.isCancelled, let self, let tip = self.pendingTip else { return }
            self.show(tip)
        }
    }

    /// The source's text changed (a knob turned under the pointer): the tip
    /// on screen (or waiting) shows the new value.
    func update(owner: UUID, text: String) {
        if tip?.owner == owner, tip?.text != text { tip?.text = text }
        if pendingOwner == owner { pendingTip?.text = text }
    }

    func hoverEnded(owner: UUID) {
        if silenced == owner { silenced = nil }
        if pendingOwner == owner { cancelPending() }
        if tip?.owner == owner { hide() }
    }

    /// A click / key (a scroll too, over a cut-name tip: the list moves):
    /// the tip goes, its source stays quiet until the pointer leaves it. A
    /// scroll over a HELP tip turns that control: the tip stays and follows.
    func dismiss() {
        silenced = tip?.owner ?? pendingOwner
        cancelPending()
        if tip != nil { hide() }
    }

    private func show(_ next: Tip) {
        pending = nil
        pendingOwner = nil
        pendingTip = nil
        tip = next
#if !canImport(UIKit)
        if eventMonitor == nil {
            eventMonitor = NSEvent.addLocalMonitorForEvents(
                matching: [.leftMouseDown, .rightMouseDown, .otherMouseDown, .scrollWheel, .keyDown]) { [weak self] event in
                    MainActor.assumeIsolated {
                        guard let self, event.type != .scrollWheel || self.tip?.optional == false else { return }
                        self.dismiss()
                    }
                    return event
                }
        }
#endif
    }

    private func hide() {
        tip = nil
        warmUntil = Date().addingTimeInterval(Self.warmWindow)
#if !canImport(UIKit)
        if let monitor = eventMonitor {
            NSEvent.removeMonitor(monitor)
            eventMonitor = nil
        }
#endif
    }

    private func cancelPending() {
        pending?.cancel()
        pending = nil
        pendingOwner = nil
        pendingTip = nil
    }
}

private struct OxoTooltipCenterKey: EnvironmentKey {
    static let defaultValue: OxoTooltipCenter? = nil
}

extension EnvironmentValues {
    var oxoTooltipCenter: OxoTooltipCenter? {
        get { self[OxoTooltipCenterKey.self] }
        set { self[OxoTooltipCenterKey.self] = newValue }
    }
}

extension View {
    /// The canvas that draws the tips of every source inside it;
    /// `showsOptionalTips` = whether help (optional) tips show at all.
    func oxoTooltipHost(showsOptionalTips: Bool = false) -> some View {
        modifier(OxoTooltipHost(showsOptionalTips: showsOptionalTips))
    }

    /// A tip with `text` on hover. `optional`: a hint the user may turn off
    /// (`OxoTooltipCenter.showsOptionalTips`).
    func oxoTooltip(_ text: String, optional: Bool) -> some View {
        modifier(OxoTooltipSource(text: text, font: nil, optional: optional))
    }

    /// A tip with the FULL `text` while the decorated one-line Text is cut;
    /// `size` / `weight` / `monospaced` = its system font.
    func oxoTruncationTooltip(_ text: String, size: CGFloat, weight: Font.Weight = .regular, monospaced: Bool = false,
                              optional: Bool) -> some View {
        modifier(OxoTooltipSource(text: text, font: (size, weight, monospaced), optional: optional))
    }
}

/// System-font text widths (what a one-line `Text` would need).
enum OxoTextMetrics {
    static func width(of text: String, size: CGFloat, weight: Font.Weight, monospaced: Bool = false) -> CGFloat {
#if canImport(UIKit)
        let font = monospaced ? UIFont.monospacedSystemFont(ofSize: size, weight: platformWeight(weight))
            : UIFont.systemFont(ofSize: size, weight: platformWeight(weight))
#else
        let font = monospaced ? NSFont.monospacedSystemFont(ofSize: size, weight: platformWeight(weight))
            : NSFont.systemFont(ofSize: size, weight: platformWeight(weight))
#endif
        return ceil((text as NSString).size(withAttributes: [.font: font]).width)
    }

#if canImport(UIKit)
    private typealias PlatformWeight = UIFont.Weight
#else
    private typealias PlatformWeight = NSFont.Weight
#endif

    private static func platformWeight(_ weight: Font.Weight) -> PlatformWeight {
        switch weight {
        case .ultraLight: return .ultraLight
        case .thin: return .thin
        case .light: return .light
        case .medium: return .medium
        case .semibold: return .semibold
        case .bold: return .bold
        case .heavy: return .heavy
        case .black: return .black
        default: return .regular
        }
    }
}

// MARK: - Sources

/// Per source: its identity, the decorated view's width and the hover
/// edge. A reference so the measuring never re-renders the row.
private final class OxoTooltipTracker {
    let owner = UUID()
    var width: CGFloat = 0
    var hovering = false
}

private struct OxoTooltipSource: ViewModifier {
    let text: String
    /// nil = always; else the font the decorated Text is drawn with.
    let font: (size: CGFloat, weight: Font.Weight, monospaced: Bool)?
    let optional: Bool
    @Environment(\.oxoTooltipCenter) private var center
    @State private var tracker = OxoTooltipTracker()

    func body(content: Content) -> some View {
        if let center {
            content
                .onGeometryChange(for: CGFloat.self) { $0.size.width } action: { tracker.width = $0 }
                // SwiftUI's hover, in the host's space (Drums uses AppKit
                // tracking areas for its row overlays; here they missed
                // whole columns of the scaled browser grid)
                .onContinuousHover(coordinateSpace: .named(OxoTooltipCenter.coordinateSpace)) { phase in
                    switch phase {
                    case .active(let location): entered(at: location, center: center)
                    case .ended: exited(center: center)
                    }
                }
                .onChange(of: text) { _, now in center.update(owner: tracker.owner, text: now) }
                .onDisappear { exited(center: center) }
        } else {
            content
        }
    }

    private func entered(at location: CGPoint, center: OxoTooltipCenter) {
        guard !tracker.hovering else { return }
        tracker.hovering = true
        guard !text.isEmpty, isCut else { return }
        center.hoverBegan(.init(owner: tracker.owner, text: text, location: location, optional: optional))
    }

    private func exited(center: OxoTooltipCenter) {
        tracker.hovering = false
        center.hoverEnded(owner: tracker.owner)
    }

    private var isCut: Bool {
        guard let font else { return true }
        return OxoTextMetrics.width(of: text, size: font.size, weight: font.weight, monospaced: font.monospaced)
            > tracker.width + 1
    }
}

// MARK: - Host

private struct OxoTooltipHost: ViewModifier {
    let showsOptionalTips: Bool
    @State private var center = OxoTooltipCenter()

    func body(content: Content) -> some View {
        content
            .environment(\.oxoTooltipCenter, center)
            .onChange(of: showsOptionalTips, initial: true) { center.showsOptionalTips = showsOptionalTips }
            .overlay(alignment: .topLeading) {
                OxoTooltipLayer(center: center)
                    .allowsHitTesting(false)
            }
            .coordinateSpace(name: OxoTooltipCenter.coordinateSpace)
    }
}

/// The bubble: below-right of the pointer, kept inside the host (above the
/// pointer when there is no room below). The LCD's colours.
private struct OxoTooltipLayer: View {
    let center: OxoTooltipCenter
    @Environment(\.theme) private var theme
    @Environment(\.scale) private var scale
    @State private var bubbleSize: CGSize = .zero

    private static let margin: CGFloat = 4

    var body: some View {
        GeometryReader { proxy in
            if let tip = center.tip {
                Text(tip.text)
                    .oxoFont(10)
                    .foregroundStyle(Theme.lcdText)
                    .lineLimit(1)
                    .fixedSize()
                    .padding(.horizontal, 6 * scale)
                    .padding(.vertical, 3 * scale)
                    .background(theme.lcdBottom)
                    .overlay(RoundedRectangle(cornerRadius: 4).stroke(Color.black.opacity(0.8), lineWidth: 1))
                    .clipShape(RoundedRectangle(cornerRadius: 4))
                    .shadow(color: .black.opacity(0.35), radius: 4, x: 0, y: 2)
                    .onGeometryChange(for: CGSize.self) { $0.size } action: { bubbleSize = $0 }
                    .offset(origin(for: tip.location, in: proxy.size))
            }
        }
    }

    private func origin(for pointer: CGPoint, in bounds: CGSize) -> CGSize {
        let m = Self.margin * scale
        let x = min(max(m, pointer.x + 10 * scale), bounds.width - bubbleSize.width - m)
        var y = pointer.y + 18 * scale
        if y + bubbleSize.height > bounds.height - m { y = pointer.y - bubbleSize.height - 6 * scale }
        return CGSize(width: max(m, x), height: max(m, y))
    }
}
