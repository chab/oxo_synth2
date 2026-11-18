import SwiftUI
#if os(macOS)
import AppKit

/// Mouse wheel over a control (Oxo Simple's MacMouseWheelMonitor): a local
/// scroll monitor per control + an invisible probe view sized to it, asked
/// per event whether the pointer is over it (clicks stay SwiftUI's). The
/// control that took the last DIRECT gesture owns its momentum tail, so
/// inertia never jumps to the next control under a drifting cursor.
final class WheelMonitor: ObservableObject {
    private static weak var momentumOwner: WheelMonitor?

    weak var probe: WheelProbeView?
    var onWheel: ((NSEvent) -> Bool)?
    private var monitor: Any?

    func installIfNeeded() {
        guard monitor == nil else { return }
        monitor = NSEvent.addLocalMonitorForEvents(matching: .scrollWheel) { [weak self] event in
            guard let self, !WheelShieldView.covers(event) else { return event }
            if event.momentumPhase.isEmpty {
                guard self.probe?.contains(event) == true else {
                    if Self.momentumOwner === self { Self.momentumOwner = nil }
                    return event
                }
                Self.momentumOwner = self
            } else {
                guard Self.momentumOwner === self else { return event }
                if event.momentumPhase.contains(.ended) || event.momentumPhase.contains(.cancelled) {
                    Self.momentumOwner = nil
                }
            }
            guard let onWheel = self.onWheel else { return event }
            return onWheel(event) ? nil : event
        }
    }

    deinit {
        if let monitor { NSEvent.removeMonitor(monitor) }
    }
}

final class WheelProbeView: NSView {
    override func hitTest(_ point: NSPoint) -> NSView? { nil }

    func contains(_ event: NSEvent) -> Bool {
        guard let window, event.window === window else { return false }
        return convert(bounds, to: nil).contains(event.locationInWindow)
    }
}

/// Put behind an overlay (the Themes dialog): wheel events over it never
/// reach the controls underneath.
final class WheelShieldView: NSView {
    private static let all = NSHashTable<WheelShieldView>.weakObjects()

    static func covers(_ event: NSEvent) -> Bool {
        all.allObjects.contains { $0.window === event.window && $0.convert($0.bounds, to: nil).contains(event.locationInWindow) }
    }

    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        Self.all.add(self)
    }
}

struct WheelShield: NSViewRepresentable {
    func makeNSView(context: Context) -> WheelShieldView { WheelShieldView() }
    func updateNSView(_ view: WheelShieldView, context: Context) {}
}

private struct WheelProbe: NSViewRepresentable {
    let monitor: WheelMonitor
    let onWheel: (NSEvent) -> Bool

    func makeNSView(context: Context) -> WheelProbeView {
        let view = WheelProbeView()
        monitor.probe = view
        monitor.onWheel = onWheel
        monitor.installIfNeeded()
        return view
    }

    func updateNSView(_ view: WheelProbeView, context: Context) {
        monitor.probe = view
        monitor.onWheel = onWheel
    }
}

private struct WheelModifier: ViewModifier {
    let onWheel: (NSEvent) -> Bool
    @StateObject private var monitor = WheelMonitor()

    func body(content: Content) -> some View {
        content.background(WheelProbe(monitor: monitor, onWheel: onWheel))
    }
}

extension View {
    /// `onWheel` returns false to let the event through (e.g. while the
    /// browser covers the panel).
    func onMouseWheel(_ onWheel: @escaping (NSEvent) -> Bool) -> some View {
        modifier(WheelModifier(onWheel: onWheel))
    }
}

extension NSEvent {
    /// The vertical wheel delta in the DEVICE's direction: value wheels
    /// ignore natural scrolling (wheel away / fingers up = positive = value
    /// up, Live's rule); scrolled lists keep `scrollingDeltaY`.
    var deviceDeltaY: CGFloat { isDirectionInvertedFromDevice ? -scrollingDeltaY : scrollingDeltaY }
}

/// Scroll dead zone: a gesture moves nothing until it has travelled
/// `threshold` points (the browser's lists scrolled at the slightest touch).
/// A gesture = trackpad phases began..momentum end; for a plain wheel, a run
/// of clicks less than 0.3 s apart.
final class ScrollDeadZone {
    static let threshold = 8.0
    private var travelled = 0.0, open = false, last = Date.distantPast

    /// True = swallow the event.
    func swallow(_ event: NSEvent) -> Bool {
        let now = Date()
        let wheel = event.phase.isEmpty && event.momentumPhase.isEmpty
        if event.phase.contains(.began) || (wheel && now.timeIntervalSince(last) > 0.3) {
            travelled = 0
            open = false
        }
        last = now
        if open { return false }
        let dy = Double(event.scrollingDeltaY), dx = Double(event.scrollingDeltaX)
        travelled += hypot(dx, dy) * (event.hasPreciseScrollingDeltas ? 1 : 10) // wheel deltas are lines
        open = travelled >= Self.threshold
        return !open
    }
}

private struct DeadZoneModifier: ViewModifier {
    @State private var zone = ScrollDeadZone()
    func body(content: Content) -> some View { content.onMouseWheel { zone.swallow($0) } }
}
#endif

extension View {
    /// The ScrollView ignores the first few points of each scroll gesture
    /// (mac; iPad scrolls by touch).
    @ViewBuilder
    func scrollDeadZone() -> some View {
        #if os(macOS)
        modifier(DeadZoneModifier())
        #else
        self
        #endif
    }
}

/// Oxo Simple's wheel feel. Knob: the full range takes 3 x the 240 pt drag
/// throw, slowed 2x (Shift: 6x finer instead). Stepped display: 33 pt per
/// step. The remainder carries between events so slow scrolls still move.
enum WheelFeel {
    static let knobFullRangeTravel: Double = 240
    static let displayTravelPerStep: Double = 11

    static func knobSteps(deltaY: Double, span: Int, shift: Bool, remainder: inout Double) -> Int {
        let slow = shift ? 6.0 : 2.0
        return take(deltaY * Double(span) / (knobFullRangeTravel * 3) / slow, &remainder)
    }

    static func displaySteps(deltaY: Double, remainder: inout Double) -> Int {
        take(deltaY / (displayTravelPerStep * 3), &remainder)
    }

    private static func take(_ delta: Double, _ remainder: inout Double) -> Int {
        let raw = delta + remainder
        let whole = Int(raw.rounded(.towardZero))
        remainder = raw - Double(whole)
        return whole
    }
}
