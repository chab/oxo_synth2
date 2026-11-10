import Foundation

/// SynthHost over the C hooks of a `SynthEditorCore` (EditorCore/
/// OxoSynth2Bridge.h): the VST3 controller on mac, the AUv3 instance on iPad.
final class HooksSynthHost: SynthHost {
    private let h: OxoSynth2Hooks

    init(hooks: OxoSynth2Hooks) { h = hooks }

    private func str(_ p: UnsafePointer<CChar>?) -> String { p.map { String(cString: $0) } ?? "" }

    func value(_ id: Int) -> Int { Int(h.getValue(h.ctx, Int32(id))) }
    func beginEdit(_ id: Int) { h.beginEdit(h.ctx, Int32(id)) }
    func setValue(_ id: Int, _ value: Int) { h.setValue(h.ctx, Int32(id), Int32(value)) }
    func endEdit(_ id: Int) { h.endEdit(h.ctx, Int32(id)) }
    func range(_ id: Int) -> (min: Int, max: Int, def: Int) {
        var lo: Int32 = 0, hi: Int32 = 0, def: Int32 = 0
        h.paramInfo(h.ctx, Int32(id), &lo, &hi, &def)
        return (Int(lo), Int(hi), Int(def))
    }
    func format(_ id: Int, _ value: Int) -> String { str(h.formatValue(h.ctx, Int32(id), Int32(value))) }
    func name(_ id: Int) -> String { str(h.paramName(h.ctx, Int32(id))) }
    func displayName(_ id: Int) -> String { str(h.displayName(h.ctx, Int32(id))) }
    var patchName: String { str(h.patchName(h.ctx)) }
    func setPatchName(_ name: String) { name.withCString { h.setPatchName(h.ctx, $0) } }
    var patchSerial: Int { Int(h.patchSerial(h.ctx)) }
    var patchText: String { str(h.patchText(h.ctx)) }
    func loadPatchFile(_ path: String) -> Bool { path.withCString { h.loadPatchFile(h.ctx, $0) } != 0 }
    func savePatchFile(_ path: String) -> Bool { path.withCString { h.savePatchFile(h.ctx, $0) } != 0 }
    func libraryJSON() -> String { str(h.libraryJSON(h.ctx)) }
    func rescanLibrary() { h.rescanLibrary(h.ctx) }
    func startLibraryScan() { h.startLibraryScan(h.ctx) }
    var libraryRevision: Int { Int(h.libraryRevision(h.ctx)) }
    var libraryScanning: Bool { h.libraryScanning(h.ctx) != 0 }
    func libraryNotice() -> String { str(h.libraryNotice(h.ctx)) }
    var libraryRoot: String { str(h.libraryRoot(h.ctx)) }
    func loadLibraryPatch(bank: Int, slot: Int) -> Bool { h.loadLibraryPatch(h.ctx, Int32(bank), Int32(slot)) != 0 }
    func writeLibraryPatch(bank: Int, slot: Int, name: String) -> Bool {
        name.withCString { h.writeLibraryPatch(h.ctx, Int32(bank), Int32(slot), $0) } != 0
    }
    func slotPath(bank: Int, slot: Int) -> String { str(h.slotPath(h.ctx, Int32(bank), Int32(slot))) }
    func newBank(_ name: String) -> Int { Int(name.withCString { h.newBank(h.ctx, $0) }) }
    func movePatch(bank: Int, from: Int, to: Int) -> Bool { h.movePatch(h.ctx, Int32(bank), Int32(from), Int32(to)) != 0 }
    func copyPatch(bank: Int, slot: Int, to: Int) -> Int { Int(h.copyPatch(h.ctx, Int32(bank), Int32(slot), Int32(to))) }
    func deleteBank(_ bank: Int) -> Bool { h.deleteBank(h.ctx, Int32(bank)) != 0 }
    func exportBank(_ bank: Int, to dir: String) -> String { str(dir.withCString { h.exportBank(h.ctx, Int32(bank), $0) }) }
    func importItems(_ paths: String) -> [String] {
        str(paths.withCString { h.importItems(h.ctx, $0) }).split(separator: "\n").map(String.init)
    }
    func midiCC(_ id: Int) -> Int { Int(h.midiCC(h.ctx, Int32(id))) }
    func setMidiCC(_ id: Int, _ cc: Int) { h.setMidiCC(h.ctx, Int32(id), Int32(cc)) }
    var lastCC: Int { Int(h.lastCC(h.ctx)) }
    func initializePatch() { h.initializePatch(h.ctx) }
    var voiceStates: UInt64 { h.voiceStates(h.ctx) }
    func allNotesOff() { h.allNotesOff(h.ctx) }
    var themeText: String { str(h.themeText(h.ctx)) }
    func setThemeText(_ text: String) { text.withCString { h.setThemeText(h.ctx, $0) } }
    var programChange: Bool {
        get { h.programChange(h.ctx) != 0 }
        set { h.setProgramChange(h.ctx, newValue ? 1 : 0) }
    }
    func loadedProgram() -> (bank: Int, slot: Int)? {
        var bank: Int32 = -1, slot: Int32 = -1
        h.loadedProgram(h.ctx, &bank, &slot)
        return bank >= 0 && slot >= 0 ? (Int(bank), Int(slot)) : nil
    }
}
