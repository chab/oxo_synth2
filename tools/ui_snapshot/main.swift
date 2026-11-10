// Renders the editor with a mock host to PNG (layout eye-check without a DAW).
//   swiftc Shared/*.swift tools/ui_snapshot/main.swift -o build/ui/snap
//   ./build/ui/snap out.png [browser|browser-loading|loading|write|write-factory|write-new-user|learn|themes|logo|logo-ipad|ipad|ipad-wide|effect|update|delete-bank]
import AppKit
import SwiftUI

final class MockHost: SynthHost {
    var values: [Int] = [2, 1, 51, 81, 1, 69, 0, 0, 64, 0, 0, 64, 0, 0, 1, 0, 40, 21, 69, 32, 22, 83, 64, 0, 0, 0, 64, 107,
                         78, 68, 0, 1, 0, 11, 64, 8, 51, 20, 0, 0, 12, 3, 5, 48, 19, 0, 5, 1, 64, 64, 74, 74, 64, 64, 50,
                         64, 40, 1, 1, 0, 64, 64, 64, 64, 2, 0, 0, 0, 0, 0, 0, 0, 64, 0, 0, 22, 0, 0, 0, 64, 64, 64, 0,
                         66, 64, 24, 45057, 44, 45057, 43, 64, 0, 0, 2, 16, 0, 1, 1, 64, 64]
    func value(_ id: Int) -> Int { values[id] }
    func beginEdit(_ id: Int) {}
    func setValue(_ id: Int, _ value: Int) { values[id] = value }
    func endEdit(_ id: Int) {}
    func range(_ id: Int) -> (min: Int, max: Int, def: Int) {
        let enums: [Int: (Int, Int)] = [0: (0, 3), 1: (1, 4), 14: (0, 4), 31: (1, 4), 32: (0, 3), 33: (0, 18), 35: (0, 19),
                                        38: (0, 2), 41: (1, 7), 42: (0, 5), 46: (1, 7), 47: (0, 5), 64: (1, 4), 71: (0, 2),
                                        78: (0, 9), 82: (0, 2), 93: (2, 8), 94: (1, 32), 96: (0, 3), 9: (-24, 24), 85: (0, 48), 40: (0, 24)]
        let toggles: Set<Int> = [4, 6, 7, 10, 24, 57, 58, 59, 65, 66, 67, 68, 69, 70, 73, 74, 77, 97]
        if let e = enums[id] { return (e.0, e.1, values[id]) }
        if toggles.contains(id) { return (0, 1, 0) }
        return (0, 127, 64)
    }
    func format(_ id: Int, _ value: Int) -> String { "\(value)" }
    var patchName: String { "Init" }
    func setPatchName(_ name: String) {}
    var patchSerial: Int { 0 }
    var patchText: String { "Init\n" }
    func loadPatchFile(_ path: String) -> Bool { true }
    func savePatchFile(_ path: String) -> Bool { true }
    func libraryJSON() -> String {
        func slots(_ names: [String]) -> String {
            "[" + (0..<128).map { $0 < names.count ? "\"\(names[$0])\"" : "\"\"" }.joined(separator: ",") + "]"
        }
        // write-factory / write-new-user: the read-only factory bank first
        // (loaded), with / without a User bank
        let factory = factoryLoaded
            ? #"{"name":"Synth2 Factory","source":"f","readOnly":true,"slots":"# + slots(["INIT", "Brass"]) + "},"
            : ""
        let user = hasUser ? #",{"name":"User","source":"a","slots":"# + slots(["My Lead", "", "Pad"]) + "}" : ""
        return #"{"root":"/Users/me/Music/Oxo Music/Oxo Synth2","banks":["# + factory
            + #"{"name":"Leads","source":"b","slots":"# + slots(["Mono Lead", "Sync Lead", "Fat Saw"]) + "},"
            + #"{"name":"Synth1Patches / Pads","source":"c","slots":"# + slots(["Init"]) + "}"
            + user + "]}"
    }
    var factoryLoaded = false
    var hasUser = true
    func rescanLibrary() {}
    func startLibraryScan() {}
    var libraryRevision = 1
    var libraryScanning = false
    func libraryNotice() -> String { "" }
    var libraryRoot: String { "/tmp" }
    func loadLibraryPatch(bank: Int, slot: Int) -> Bool { true }
    func writeLibraryPatch(bank: Int, slot: Int, name: String) -> Bool { true }
    func slotPath(bank: Int, slot: Int) -> String { "" }
    func newBank(_ name: String) -> Int { 0 }
    func movePatch(bank: Int, from: Int, to: Int) -> Bool { true }
    func copyPatch(bank: Int, slot: Int, to: Int) -> Int { 0 }
    func deleteBank(_ bank: Int) -> Bool { true }
    func exportBank(_ bank: Int, to dir: String) -> String { "" }
    func importItems(_ paths: String) -> [String] { [] }
    func name(_ id: Int) -> String { "param \(id)" }
    func displayName(_ id: Int) -> String { name(id) }
    var learned: [Int: Int] = [19: 74, 20: 71]
    func midiCC(_ id: Int) -> Int { learned[id] ?? 0 }
    func setMidiCC(_ id: Int, _ cc: Int) { learned[id] = cc }
    var lastCC: Int { 0 }
    func initializePatch() {}
    // 3 held, 2 releasing, a held one past a lowered polyphony
    var voiceStates: UInt64 { 0b01_01_10_10_10 | 2 << 40 }
    func loadedProgram() -> (bank: Int, slot: Int)? { (0, 0) }
    func allNotesOff() {}
    var themeText = ""
    func setThemeText(_ text: String) { themeText = text }
    var programChange = false
}

let args = CommandLine.arguments
let mock = MockHost()
// loading: first browser open, no list yet; browser-loading: a rescan over a shown list
let mode = args.count > 2 ? args[2] : ""
if mode == "loading" { mock.libraryRevision = 0 }
mock.factoryLoaded = mode == "write-factory" || mode == "write-new-user"
mock.hasUser = mode != "write-new-user"
mock.libraryScanning = mode == "loading" || mode == "browser-loading"
let model = SynthModel(host: mock)
if args.count > 2 {
    if mode != "loading" { model.loadLibrary() }
    if mode == "loading" {
        model.openBrowser()
    } else if args[2] == "themes" {
    } else if args[2] == "logo" || args[2] == "logo-ipad" {
        // logo-ipad: no resize callback (AUv3) = themes only
        if args[2] == "logo" { model.requestEditorWidth = { _ in } }
        model.showLogoMenu = true
    } else if args[2] == "ipad" || args[2] == "ipad-wide" {
        // the AUv3's UI mode toggle; ipad-wide = width mode in a wide, short
        // window (the panel scrolls, side strips)
        var wide = args[2] == "ipad-wide"
        model.uiWidthModeStorage = (get: { wide }, set: { wide = $0 })
    } else if args[2] == "effect" {
        model.showEffectMenu = true
    } else if args[2] == "update" {
        model.update = .available(latest: "V1.1", installed: "V1.0",
                                  page: URL(string: "https://oxo-music.com/products/oxo_synth2")!)
    } else if args[2] == "learn" {
        model.toggleLearn()
        model.learnSelect(21)
    } else if args[2] == "write" || args[2] == "write-factory" || args[2] == "write-new-user" {
        model.load(bank: 0, slot: 0)
        model.openBrowser(write: true)
    } else {
        model.openBrowser()
        model.load(bank: 1, slot: 1)
        if args[2] == "delete-bank" { model.bankToDelete = model.banks[0].id }
    }
}
let width = 1155.0
let height = args.count > 2 && args[2] == "ipad-wide" ? 600.0
    : width * SynthPanelView.designSize.height / SynthPanelView.designSize.width
MainActor.assumeIsolated {
    let app = NSApplication.shared
    app.setActivationPolicy(.prohibited)
    let themes = args.count > 2 && args[2] == "themes"
    if themes { model.showThemes = true }
    let host = NSHostingView(rootView: SynthPanelView(model: model))
    host.frame = NSRect(x: 0, y: 0, width: width, height: height)
    let window = NSWindow(contentRect: host.frame, styleMask: [.borderless], backing: .buffered, defer: false)
    window.contentView = host
    host.layoutSubtreeIfNeeded()
    RunLoop.main.run(until: Date().addingTimeInterval(0.5))
    host.layoutSubtreeIfNeeded()
    if let rep = host.bitmapImageRepForCachingDisplay(in: host.bounds) {
        host.cacheDisplay(in: host.bounds, to: rep)
        let png = rep.representation(using: .png, properties: [:])!
        try! png.write(to: URL(fileURLWithPath: args.count > 1 ? args[1] : "/tmp/synth2_ui.png"))
        print("ok")
    }
}
