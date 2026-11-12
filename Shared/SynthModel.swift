import Combine
import Foundation

/// What the editor needs from a plugin wrapper (`HooksSynthHost` over a
/// `SynthEditorCore`: the VST3 controller on mac, the AUv3 instance on
/// iPad). Values are NATIVE Synth1 integers (see EngineCore/Params.hpp);
/// every call happens on the main thread.
public protocol SynthHost: AnyObject {
    func value(_ id: Int) -> Int
    func beginEdit(_ id: Int)
    func setValue(_ id: Int, _ value: Int)
    func endEdit(_ id: Int)
    func range(_ id: Int) -> (min: Int, max: Int, def: Int)
    func format(_ id: Int, _ value: Int) -> String
    /// Long parameter name ("filter freq"), for readouts.
    func name(_ id: Int) -> String
    /// Name for the LCD readout: the effect knobs say what they do in the
    /// current effect type ("dist amount"), others = `name`.
    func displayName(_ id: Int) -> String
    var patchName: String { get }
    func setPatchName(_ name: String)
    var patchSerial: Int { get }
    /// The current sound as .sy1 text.
    var patchText: String { get }
    func loadPatchFile(_ path: String) -> Bool
    func savePatchFile(_ path: String) -> Bool
    // Bank library (EngineCore/PatchLibrary.hpp).
    func libraryJSON() -> String
    /// Also extracts zips put into the library (then trashed).
    func rescanLibrary()
    /// `rescanLibrary` on a worker thread; reading `libraryRevision`
    /// adopts the finished scan (big libraries: the browser opens at once).
    func startLibraryScan()
    /// Bumps whenever the bank list changes, 0 = never scanned.
    var libraryRevision: Int { get }
    var libraryScanning: Bool { get }
    /// banks streamed into the browser so far, their patches, banks expected
    var libraryScanProgress: (banks: Int, patches: Int, total: Int) { get }
    /// "X.zip imported and moved to Trash" since the last call, "" = none.
    func libraryNotice() -> String
    var libraryRoot: String { get }
    func loadLibraryPatch(bank: Int, slot: Int) -> Bool
    func writeLibraryPatch(bank: Int, slot: Int, name: String) -> Bool
    /// The .sy1 file of a slot ("" = empty).
    func slotPath(bank: Int, slot: Int) -> String
    func newBank(_ name: String) -> Int
    /// Browser drag and drop: slot `from` -> `to` in a bank (swap if taken).
    func movePatch(bank: Int, from: Int, to: Int) -> Bool
    /// Browser drag onto a bank: a copy into its first free slot; returns
    /// that slot, -1 = the bank is full, -2 = failed.
    func copyPatch(bank: Int, slot: Int, to: Int) -> Int
    /// Browser > Delete bank: its folder to the Trash (mac) / deleted (iPad).
    func deleteBank(_ bank: Int) -> Bool
    /// Browser > Export bank: copies the bank as a new folder into `dir`;
    /// returns that folder, "" = failed.
    func exportBank(_ bank: Int, to dir: String) -> String
    /// Copies into the library and rescans; newline-separated paths in.
    func importItems(_ paths: String) -> ImportOutcome
    /// Categories, favourites, tags (EngineCore/PatchCategories.hpp).
    /// Records are keyed by the patch's bytes, so the marks follow a patch
    /// through renames, imports and a new setup.
    /// `category` = index into `categoryNames`, -1 = back to the name rule.
    func setPatchCategory(bank: Int, slot: Int, category: Int)
    func setPatchFavourite(bank: Int, slot: Int, on: Bool)
    func addPatchTag(bank: Int, slot: Int, tag: String)
    func removePatchTag(bank: Int, slot: Int, tag: String)
    /// A patch's colour: the .sy1's own `color=` line, so it travels with
    /// the file. Rewrites it; false = refused (factory bank) or failed.
    @discardableResult
    func setPatchColor(bank: Int, slot: Int, color: String) -> Bool
    /// The whole store as JSON -> `path`, true = written.
    func exportTags(_ path: String) -> Bool
    /// Merges a store file into ours (marks we already have win, so a new
    /// setup never loses its own work), then rescans. -1 = unreadable.
    func importTags(_ path: String) -> Int
    /// Tags in use, one per line, "" = none yet.
    func tagVocabulary() -> String
    /// Patches whose file bytes are identical, across every bank:
    /// "groups=N" then one line per group of "bank,slot" tokens.
    /// "" = the library is still loading.
    func findDuplicates() -> String
    /// MIDI learn: plugin-global table (0 = not learned) and the latest
    /// "a CC arrived" token ((counter << 7) | cc, 0 = none yet).
    func midiCC(_ id: Int) -> Int
    func setMidiCC(_ id: Int, _ cc: Int)
    var lastCC: Int { get }
    /// Loads Synth2's INIT patch (also the knobs' double-click values,
    /// `range(id).def`).
    func initializePatch()
    /// Voice activity: 2 bits per voice (bits 2i, 2i + 1): 0 idle,
    /// 1 releasing, 2 held (note on, no note off yet).
    var voiceStates: UInt64 { get }
    /// The library program the sound came from (bank index into
    /// `libraryJSON`), nil after Init / a patch file / an unknown session.
    func loadedProgram() -> (bank: Int, slot: Int)?
    /// Menu > All notes off: cut every voice now.
    func allNotesOff()
    /// This instance's palette (`ThemePalette.stateText`), session state,
    /// "" = never set. Re-read when `patchSerial` bumps (session restore).
    var themeText: String { get }
    func setThemeText(_ text: String)
    /// Menu > Program change: MIDI program changes played (true) or
    /// ignored; a per-user preference, saved by the wrapper.
    var programChange: Bool { get set }
}

/// The voice grid's feed, apart from SynthModel so a 30 Hz change only
/// redraws the grid, not the whole panel.
final class VoiceActivity: ObservableObject {
    @Published fileprivate(set) var states: UInt64 = 0
}

/// One flag, tolerant of both `true`/`false` and `0`/`1`. The engine emits
/// booleans; documents written by older builds held numbers, and a decode
/// failure there used to blank the entire browser.
private struct LooseBit: Decodable {
    let value: Bool

    init(from decoder: Decoder) throws {
        let c = try decoder.singleValueContainer()
        if let flag = try? c.decode(Bool.self) {
            value = flag
        } else {
            value = try c.decode(Int.self) != 0
        }
    }
}

/// One bank = a folder of the library, 128 program slots ("" = empty),
/// Synth1's numbering.
struct PatchBankInfo: Decodable, Identifiable, Equatable {
    let name: String
    let source: String
    let slots: [String]
    /// The factory bank (plugin bundle): no write / move / delete.
    let readOnly: Bool
    /// Type, per slot (index into `LibraryInfo.categoryNames`). Derived
    /// from the patch name, or whatever the user set.
    let categories: [Int]
    let favourites: [Bool]
    /// User tags per slot, already normalised / sorted by the engine.
    let tags: [[String]]
    /// Per slot, the .sy1's own `color=` line ("default" = untinted).
    let colors: [String]
    var id: String { source }

    private enum CodingKeys: String, CodingKey { case name, source, slots, readOnly, cats, favs, tags, colors }
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        name = try c.decode(String.self, forKey: .name)
        source = try c.decode(String.self, forKey: .source)
        slots = try c.decode([String].self, forKey: .slots)
        readOnly = try c.decodeIfPresent(Bool.self, forKey: .readOnly) ?? false
        // Parallel to `slots`, so they index alike; a missing array (an
        // older document) simply means "Unclassified, unstarred, no tags".
        categories = try c.decodeIfPresent([Int].self, forKey: .cats) ?? []
        favourites = try c.decodeIfPresent([LooseBit].self, forKey: .favs)?.map(\.value) ?? []
        tags = try c.decodeIfPresent([[String]].self, forKey: .tags) ?? []
        colors = try c.decodeIfPresent([String].self, forKey: .colors) ?? []
    }

    var count: Int { slots.filter { !$0.isEmpty }.count }

    func category(_ slot: Int) -> Int { slot < categories.count ? categories[slot] : 0 }
    func isFavourite(_ slot: Int) -> Bool { slot < favourites.count && favourites[slot] }
    func tags(_ slot: Int) -> [String] { slot < tags.count ? tags[slot] : [] }
    func color(_ slot: Int) -> String { slot < colors.count ? colors[slot] : "default" }
}

private struct LibraryInfo: Decodable {
    let root: String
    let categoryNames: [String]
    let tagVocab: [String]
    let banks: [PatchBankInfo]

    private enum CodingKeys: String, CodingKey { case root, catNames, tagVocab, banks }
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        root = try c.decodeIfPresent(String.self, forKey: .root) ?? ""
        categoryNames = try c.decodeIfPresent([String].self, forKey: .catNames) ?? []
        tagVocab = try c.decodeIfPresent([String].self, forKey: .tagVocab) ?? []
        banks = try c.decode([PatchBankInfo].self, forKey: .banks)
    }

    /// Name for a `cats` value ("Unclassified" when out of range).
    func categoryName(_ category: Int) -> String {
        category >= 0 && category < categoryNames.count ? categoryNames[category] : "Unclassified"
    }
}

/// File dialogs the panel presents (NSOpenPanel / NSSavePanel on mac, the
/// system file importer / exporter on iPad). `importFolder`: pick bank
/// folders, `importBanks`: pick files (zips / .sy1) - two panels, because
/// one panel taking folders and files hits neither well; `exportBank`:
/// pick the folder that receives the bank (its id).
enum FileAction: Equatable {
    case importBanks, importFolder, loadPatch, savePatch
    case exportBank(String)
    /// The portable categories / tags file (categories.json).
    case exportTags, importTags
}

/// What one Import… answered (EngineCore/PatchLibrary.hpp `ImportResult`,
/// via the hook's newline payload): the folders created with their patch
/// counts, a total, and why anything was left out.
public struct ImportOutcome: Equatable {
    public struct Added: Equatable {
        public let path: String
        public let patches: Int
    }
    public var added: [Added] = []
    public var patches = 0
    public var refused: [String] = []
    public var isEmpty: Bool { added.isEmpty }

    /// Decodes `patches=N` / `+<n> <folder>` / `!<path>: <reason>` lines.
    /// A bare list of folders (an older host) still reads as "added".
    public init(payload: String) {
        for line in payload.split(separator: "\n", omittingEmptySubsequences: true) {
            if line.hasPrefix("patches=") {
                patches = Int(line.dropFirst(8)) ?? 0
            } else if line.hasPrefix("+") {
                let rest = line.dropFirst()
                let count = Int(rest.prefix { $0.isNumber }) ?? 0
                let path = rest.drop { $0.isNumber || $0 == " " }
                if !path.isEmpty { added.append(Added(path: String(path), patches: count)) }
            } else if line.hasPrefix("!") {
                refused.append(String(line.dropFirst()))
            } else if !line.isEmpty {
                added.append(Added(path: String(line), patches: 0))
            }
        }
        if patches == 0 { patches = added.reduce(0) { $0 + $1.patches } }
    }
}

/// One set of patches whose file BYTES are identical
/// (`PatchManager::FindDuplicates`): only where each member sits, since
/// names and colours come from the bank list, so a group stays valid
/// across a rescan.
public struct DuplicateGroup: Equatable, Identifiable {
    public struct Member: Equatable, Hashable {
        public let bank: Int
        public let slot: Int
    }

    public var members: [Member]

    public var id: String {
        members.map { "\($0.bank),\($0.slot)" }.joined(separator: " ")
    }

    /// Decodes "groups=N" + one line of "bank,slot" tokens per group. A
    /// malformed token drops its whole group rather than half of one.
    public static func groups(payload: String) -> [DuplicateGroup] {
        var out: [DuplicateGroup] = []
        for line in payload.split(separator: "\n", omittingEmptySubsequences: true) {
            if line.hasPrefix("groups=") { continue }
            let tokens = line.split(whereSeparator: { $0 == " " || $0 == "," })
            guard tokens.count >= 4, tokens.count % 2 == 0 else { continue }
            var members: [Member] = []
            var malformed = false
            for i in stride(from: 0, to: tokens.count, by: 2) {
                guard let bank = Int(tokens[i]), let slot = Int(tokens[i + 1]),
                      bank >= 0, slot >= 0 else {
                    malformed = true
                    break
                }
                members.append(Member(bank: bank, slot: slot))
            }
            if !malformed { out.append(DuplicateGroup(members: members)) }
        }
        return out
    }
}

/// Editor state mirrored from the host by a 30 Hz poll (host automation,
/// session restore and patch loads all show up without extra plumbing).
final class SynthModel: ObservableObject {
    /// The patch's parameters (0..98, Synth1's indices).
    static let paramCount = 99
    /// Master volume ("vol" left of the LCD): a host parameter right after
    /// the patch, not part of it (EngineCore/Params.hpp kVolumeParamId).
    static let volumeID = 99
    /// Everything polled into `values`: the patch + the volume.
    static let hostParamCount = 100

    @Published private(set) var values: [Int]
    @Published private(set) var patchName: String
    @Published private(set) var banks: [PatchBankInfo] = []
    @Published private(set) var libraryRoot = ""
    /// Category names the engine's `cats` values index into.
    @Published private(set) var categoryNames: [String] = []
    /// Every tag in use, sorted (tag dialog's suggestions).
    @Published private(set) var tagVocabulary: [String] = []
    /// Browser search text (patch / bank name, category, tags).
    @Published var librarySearch = ""
    /// Browser filter: a category index, `favouritesOnly` or nil = all.
    @Published var categoryFilter: Int?
    @Published var favouritesOnly = false

    // MARK: - browser filter

    var hasActiveFilter: Bool {
        categoryFilter != nil || favouritesOnly
            || !librarySearch.trimmingCharacters(in: .whitespaces).isEmpty
    }

    func clearLibraryFilter() {
        categoryFilter = nil
        favouritesOnly = false
        librarySearch = ""
    }

    /// Patches of a given type (decides which chips are worth showing).
    func categoryCount(_ category: Int) -> Int {
        banks.reduce(0) { total, bank in
            total + bank.slots.indices.reduce(0) { inner, slot in
                inner + (!bank.slots[slot].isEmpty && bank.category(slot) == category ? 1 : 0)
            }
        }
    }

    /// A slot passes search text + type + star. The bank's name takes part
    /// in the text match, so "chill" finds a whole bank's patches too.
    func slotMatches(bank: Int, slot: Int) -> Bool {
        guard bank >= 0, bank < banks.count, slot >= 0, slot < banks[bank].slots.count,
              !banks[bank].slots[slot].isEmpty else { return false }
        let info = banks[bank]
        if favouritesOnly && !info.isFavourite(slot) { return false }
        if let category = categoryFilter, info.category(slot) != category { return false }
        let needle = librarySearch.trimmingCharacters(in: .whitespaces).lowercased()
        if needle.isEmpty { return true }
        if info.slots[slot].lowercased().contains(needle) { return true }
        if info.name.lowercased().contains(needle) { return true }
        if categoryName(info.category(slot)).lowercased().contains(needle) { return true }
        return info.tags(slot).contains { $0.lowercased().contains(needle) }
    }

    /// Patches of a bank that pass the filter (0 dims the row while one is on).
    func matchCount(bank: Int) -> Int {
        guard bank >= 0, bank < banks.count else { return 0 }
        guard hasActiveFilter else { return banks[bank].count }
        return banks[bank].slots.indices.reduce(0) { $0 + (slotMatches(bank: bank, slot: $1) ? 1 : 0) }
    }
    /// A background rescan is running (the browser says "loading…").
    @Published private(set) var libraryLoading = false
    /// How much of the library the running (or last) scan has delivered:
    /// banks streamed in, their patches, banks the scan expects. The browser
    /// shows a bar and keeps the streamed banks usable meanwhile.
    @Published private(set) var libraryScan = (banks: 0, patches: 0, total: 0)
    /// Fraction 0...1 for the progress bar (1 when no scan is running).
    var libraryScanFraction: Double {
        let total = max(libraryScan.total, libraryScan.banks, 1)
        return libraryLoading ? min(1, Double(libraryScan.banks) / Double(total)) : 1
    }
    /// Last Import… result, shown in the browser header for a few seconds
    /// (the LCD is behind the browser).
    @Published private(set) var importNotice: String?
    private var importNoticeHide: Date?
    /// Why the last `loadLibrary` could not read the engine's JSON. A
    /// decode failure is a bug, not an empty library: say so instead of
    /// leaving the browser looking like it has no banks.
    @Published private(set) var libraryError: String?
    /// `host.libraryRevision` of `banks`.
    private var libraryRevision = 0
    /// Bank shown in the browser / LCD, and the loaded program slot.
    @Published var currentBank: Int?
    @Published var currentProgram: Int?
    /// Bank of the loaded program (by identity: indices move on rescans).
    @Published private(set) var loadedBankID: String?
    var loadedBank: Int? { loadedBankID.flatMap { id in banks.firstIndex { $0.id == id } } }
    /// Patch browser over the panel; `writing` = Synth1's write dialog.
    @Published var showBrowser = false
    @Published var writing = false
    /// Bumped by the bottom bar's write button while the write dialog is
    /// open: the dialog commits (same as its Write / Overwrite button).
    @Published var writeCommits = 0
    /// Browser > Delete bank…: the bank (id) awaiting the in-browser
    /// confirmation, nil = none.
    @Published var bankToDelete: String?
    /// Browser > Edit tags…: the slot whose category / star / tags the
    /// in-browser dialog edits, nil = none.
    @Published var tagEdit: (bank: Int, slot: Int)?
    /// Browser > Find duplicates…: the groups reported, nil = dialog
    /// closed. `[]` = the dialog is open showing "none found".
    @Published var duplicateGroups: [DuplicateGroup]?
    @Published var fileAction: FileAction?
    /// The in-panel Themes dialog (menu > Themes…).
    @Published var showThemes = false
    /// This instance's palette: kept in the host session, never in a patch;
    /// the last one picked seeds new instances (`ThemeStore`).
    @Published private(set) var palette: ThemePalette
    /// Bumps on every whole-theme change (Load, session restore), so the
    /// colour editor re-reads the selected colour.
    @Published private(set) var themeRevision = 0
    /// The "Synth2" logo's menu (UI sizes + themes), mac VST3 / AUv2 only.
    @Published var showLogoMenu = false
    /// Effect type pick list (a click on its display; Synth1's 2 x 5 menu).
    @Published var showEffectMenu = false
    /// Menu > Program change (checkmark): MIDI program changes played.
    @Published var programChange = false {
        didSet {
            guard programChange != oldValue else { return }
            host.programChange = programChange
            flash(programChange ? "program change: on" : "program change: off")
        }
    }
    /// Menu > Check for updates: the dialog's state, nil = closed.
    @Published var update: UpdateCheck.State?
    private var updateRequest = 0
    /// Asks the host for this editor width (the height follows the aspect);
    /// nil = the host owns no resize (iPad AUv3, snapshots): no logo menu.
    var requestEditorWidth: ((Double) -> Void)?
    /// iPad: where the AU keeps this instance's UI mode (host session); nil
    /// = no such mode (mac, snapshots): no toggle, the panel always fits.
    var uiWidthModeStorage: (get: () -> Bool, set: (Bool) -> Void)? {
        didSet { uiWidthMode = uiWidthModeStorage?.get() ?? false }
    }
    /// UI width mode: the width leads and the panel scrolls vertically
    /// (false = height mode, the whole panel fits the view).
    @Published private(set) var uiWidthMode = false

    /// Transient LCD message (last edited value, MIDI learn feedback).
    @Published private(set) var readout: String?
    @Published private(set) var learnActive = false
    @Published private(set) var learnSelection: Int?
    @Published private(set) var learnTable: [Int]

    let host: SynthHost
    let voices = VoiceActivity()
    private var timer: Timer?
    private var serial = -1
    private var ccToken = 0
    private var readoutHide: Date?
    private var wheelEnds: [Int: Date] = [:]

    init(host: SynthHost) {
        self.host = host
        values = (0..<Self.hostParamCount).map { host.value($0) }
        learnTable = (0..<Self.paramCount).map { host.midiCC($0) }
        patchName = host.patchName
        programChange = host.programChange
        serial = host.patchSerial
        palette = ThemePalette(stateText: host.themeText) ?? ThemeStore.shared.startPalette()
        host.setThemeText(palette.stateText)
        // Tracked from the start, so entering learn mode never consumes a CC
        // that arrived before.
        ccToken = host.lastCC
        // Never a blocking scan here (big libraries): an editor reopen finds
        // the list in place, a first open gets it from the background scan.
        if host.libraryRevision > 0 {
            loadLibrary()
            syncProgram()
        }
        refreshLibrary()
        let t = Timer(timeInterval: 1.0 / 30.0, repeats: true) { [weak self] _ in self?.poll() }
        RunLoop.main.add(t, forMode: .common)
        timer = t
    }

    func invalidate() {
        exitLearn()
        for id in wheelEnds.keys { host.endEdit(id) }
        wheelEnds.removeAll()
        timer?.invalidate()
        timer = nil
    }

    private func poll() {
        var fresh = values
        var changed = false
        for id in 0..<Self.hostParamCount {
            let v = host.value(id)
            if v != fresh[id] {
                fresh[id] = v
                changed = true
            }
        }
        if changed { values = fresh }
        let s = host.patchSerial
        if s != serial {
            serial = s
            patchName = host.patchName
            syncProgram()
            syncTheme()
        }
        let token = host.lastCC
        if token != ccToken {
            ccToken = token
            if token != 0 { incomingCC(token & 0x7F) }
        }
        if host.libraryRevision != libraryRevision { // a background scan landed
            let first = libraryRevision == 0
            loadLibrary()
            syncProgram(showBank: first) // a rescan keeps the bank being browsed
        }
        let loading = host.libraryScanning
        if loading != libraryLoading { libraryLoading = loading }
        if loading { libraryScan = host.libraryScanProgress }
        let voiceStates = host.voiceStates
        if voiceStates != voices.states { voices.states = voiceStates }
        let now = Date()
        for (id, end) in wheelEnds where end <= now {
            wheelEnds[id] = nil
            host.endEdit(id)
        }
        if let hide = readoutHide, hide <= now {
            readoutHide = nil
            readout = nil
        }
        if let hide = importNoticeHide, hide <= now {
            importNoticeHide = nil
            importNotice = nil
        }
        // a session restore may bring the other mode
        if let mode = uiWidthModeStorage?.get(), mode != uiWidthMode { uiWidthMode = mode }
    }

    func toggleUIWidthMode() {
        guard let storage = uiWidthModeStorage else { return }
        uiWidthMode.toggle()
        storage.set(uiWidthMode)
    }

    // MARK: - theme (per instance)

    func applyTheme(_ p: ThemePalette) {
        palette = p
        themeRevision += 1
        themeChanged()
    }

    func setThemeColor(_ c: ThemeColor, _ rgb: RGB) {
        guard palette[c] != rgb else { return }
        palette.colors[c] = rgb
        themeChanged()
    }

    private func themeChanged() {
        host.setThemeText(palette.stateText)
        ThemeStore.shared.remember(palette)
    }

    /// A session restore may bring another palette; one without (older
    /// session) keeps the current one, now pinned to the session.
    private func syncTheme() {
        if let p = ThemePalette(stateText: host.themeText) {
            if p != palette {
                palette = p
                themeRevision += 1
            }
        } else {
            host.setThemeText(palette.stateText)
        }
    }

    // MARK: - edits

    func begin(_ id: Int) { host.beginEdit(id) }
    func set(_ id: Int, _ value: Int) {
        let r = host.range(id)
        let v = min(max(value, r.min), r.max)
        guard v != values[id] else { return }
        values[id] = v
        host.setValue(id, v)
        show(id)
    }
    func end(_ id: Int) { host.endEdit(id) }
    func setOnce(_ id: Int, _ value: Int) {
        begin(id)
        set(id, value)
        end(id)
    }

    /// Next / previous choice, wrapping (Synth1's type / dst buttons).
    func cycle(_ id: Int, _ delta: Int, gesture: Bool = true) {
        let r = host.range(id)
        let n = r.max - r.min + 1
        let v = r.min + ((values[id] - r.min + delta) % n + n) % n
        if gesture { setOnce(id, v) } else { set(id, v) }
    }

    /// Mouse wheel: one host gesture per burst (ends 0.4 s after the last
    /// tick), so a scroll is one automation / undo step.
    func wheel(_ id: Int, steps: Int) {
        guard steps != 0 else { return }
        if wheelEnds[id] == nil { host.beginEdit(id) }
        wheelEnds[id] = Date().addingTimeInterval(0.4)
        set(id, values[id] + steps)
    }

    func show(_ id: Int) { flash("\(host.displayName(id)): \(host.format(id, values[id]))") }

    func flash(_ text: String, seconds: Double = 1.5) {
        readout = text
        readoutHide = Date().addingTimeInterval(seconds)
    }

    // MARK: - MIDI learn (Oxo Simple's behaviour)

    func toggleLearn() {
        if learnActive {
            exitLearn()
        } else {
            showBrowser = false
            learnTable = (0..<Self.paramCount).map { host.midiCC($0) }
            learnActive = true
            learnSelection = nil
        }
    }

    func exitLearn() {
        learnActive = false
        learnSelection = nil
    }

    func learnSelect(_ id: Int) {
        guard learnActive else { return }
        learnSelection = id
    }

    func learnUnlearn(_ id: Int) {
        guard learnActive, learnTable[id] != 0 else { return }
        host.setMidiCC(id, 0)
        learnTable[id] = 0
        flash("MIDI LEARN: \(host.name(id)) cleared")
    }

    /// A CC arrived (any channel): assign it to the armed control while
    /// learn mode is on; re-learning replaces the previous CC.
    private func incomingCC(_ cc: Int) {
        guard learnActive, let id = learnSelection, (1...127).contains(cc), learnTable[id] != cc else { return }
        host.setMidiCC(id, cc)
        learnTable[id] = cc
        flash("MIDI LEARN: \(host.name(id)) = CC \(cc)")
    }

    func rename(_ name: String) {
        host.setPatchName(name)
        patchName = name
    }

    // MARK: - library

    func loadLibrary(rescan: Bool = false) {
        let keep = currentBank.flatMap { $0 < banks.count ? banks[$0] : nil }
        if rescan { host.rescanLibrary() }
        libraryRevision = host.libraryRevision // before the JSON: a scan adopted by this read is in it
        guard let data = host.libraryJSON().data(using: .utf8) else { return }
        let info: LibraryInfo
        do {
            info = try JSONDecoder().decode(LibraryInfo.self, from: data)
        } catch {
            // Never let this be a silent no-op: a decode failure here used
            // to leave `banks` empty and look exactly like an empty library.
            libraryError = Self.describe(error: error)
            flash("LIBRARY: \(libraryError!)", seconds: 10)
            return
        }
        libraryError = nil
        banks = info.banks
        libraryRoot = info.root
        categoryNames = info.categoryNames
        tagVocabulary = info.tagVocab
        let notice = host.libraryNotice()
        if !notice.isEmpty { flash(notice, seconds: 10) }
        // A streamed list holds only the banks scanned so far, so the bank on
        // screen can be missing because its turn has not come yet. Keep
        // waiting for it (nothing selected meanwhile) rather than jumping to
        // bank 0 and back.
        if let keep, let i = banks.firstIndex(where: { $0.id == keep.id }) {
            currentBank = i
        } else if keep != nil, libraryLoading {
            currentBank = nil // not streamed in yet
        } else if currentBank.map({ $0 >= banks.count }) ?? true {
            currentBank = banks.isEmpty ? nil : 0
            currentProgram = nil
        }
    }

    /// A late answer to a closed (or superseded) check is dropped.
    func checkForUpdates() {
        updateRequest += 1
        let request = updateRequest
        update = .checking
        UpdateCheck.run { [weak self] state in
            guard let self, request == self.updateRequest, self.update != nil else { return }
            self.update = state
        }
    }

    /// Rescan in the background (Refresh, browser open, editor open): the
    /// current list stays usable meanwhile, the poll swaps the new one in.
    func refreshLibrary() {
        host.startLibraryScan()
        libraryLoading = host.libraryScanning
        libraryScan = host.libraryScanProgress
    }

    /// Rescans on every open: banks edited in Finder show up by themselves.
    /// Opens at once (big libraries take a while to scan).
    func openBrowser(write: Bool = false) {
        refreshLibrary()
        if let b = loadedBank { currentBank = b }
        showThemes = false
        writing = write
        showBrowser = true
    }

    func closeBrowser() {
        showBrowser = false
        writing = false
        bankToDelete = nil
        tagEdit = nil
        duplicateGroups = nil
    }

    /// LCD / browser selection from the host's program (a fresh instance
    /// starts on the library's first patch, sessions restore theirs).
    /// `showBank` false = the browser stays on the bank it shows.
    private func syncProgram(showBank: Bool = true) {
        // (no list yet: asking would scan synchronously; the scan's arrival re-syncs)
        if libraryRevision > 0, let p = host.loadedProgram(), p.bank < banks.count {
            if showBank { currentBank = p.bank }
            currentProgram = p.slot
            loadedBankID = banks[p.bank].id
        } else if libraryLoading, host.loadedProgram() != nil {
            // mid-stream: the program's bank has not streamed in yet; the
            // selection resolved before is still right, and the next
            // revision re-syncs.
        } else {
            currentProgram = nil
            loadedBankID = nil
        }
    }

    func load(bank: Int, slot: Int) {
        guard host.loadLibraryPatch(bank: bank, slot: slot) else { return }
        currentBank = bank
        currentProgram = slot
        loadedBankID = banks[bank].id
        poll()
    }

    /// Next / previous program, skipping empty slots, rolling into the
    /// neighbouring banks at the ends.
    func step(_ delta: Int) {
        if banks.isEmpty { loadLibrary() }
        guard banks.contains(where: { $0.count > 0 }) else { return }
        var b = min(loadedBank ?? currentBank ?? 0, banks.count - 1)
        var p = currentProgram ?? (delta > 0 ? -1 : banks[b].slots.count)
        for _ in 0..<(banks.count * 128 + 1) {
            p += delta
            if p >= banks[b].slots.count {
                b = (b + 1) % banks.count
                p = -1
                continue
            }
            if p < 0 {
                b = (b - 1 + banks.count) % banks.count
                p = banks[b].slots.count
                continue
            }
            if !banks[b].slots[p].isEmpty { return load(bank: b, slot: p) }
        }
    }

    /// Load a random patch, honouring the browser filter ("find undiscovered
    /// sounds"). Reservoir sample over the slots on screen: one pass, no
    /// allocation, empty slots never win.
    func randomPatch() {
        if banks.isEmpty { loadLibrary() }
        var pick: (bank: Int, slot: Int)?
        var seen = 0
        for b in banks.indices {
            for p in banks[b].slots.indices {
                guard !banks[b].slots[p].isEmpty, slotMatches(bank: b, slot: p) else { continue }
                seen += 1
                if Int.random(in: 0..<seen) == 0 { pick = (b, p) }
            }
        }
        guard let pick else {
            flash(hasActiveFilter ? "no patch matches the filter" : "library is empty")
            return
        }
        load(bank: pick.bank, slot: pick.slot)
    }

    /// Browser > Find duplicates…: every set of patches whose file bytes
    /// match, across all banks (`PatchManager::FindDuplicates`). A library
    /// still being scanned answers "" - nothing to report yet, so the scan
    /// hint is shown instead (a partial list would split a group).
    func findDuplicates() {
        guard let groups = scanDuplicates() else {
            flash("still loading the library")
            return
        }
        duplicateGroups = groups
        if groups.isEmpty { flash("no duplicate patches") }
    }

    /// nil = the host still has work to do (returns "" until the scan ends).
    private func scanDuplicates() -> [DuplicateGroup]? {
        let payload = host.findDuplicates()
        guard !payload.isEmpty else { return nil }
        return DuplicateGroup.groups(payload: payload)
    }

    /// Duplicates dialog > one copy's Delete: the slot's own Delete, then
    /// the groups are worked out again (a set that lost all but one member
    /// leaves the list). The readout keeps the "moved to Trash" message.
    func deleteDuplicate(bank: Int, slot: Int) {
        deleteSlot(bank: bank, slot: slot)
        if let groups = scanDuplicates() { duplicateGroups = groups }
    }

    /// Rows of the browser's 4-column slot grid (filled top to bottom).
    static func browserRows(_ count: Int) -> Int { (count + 3) / 4 }

    enum BrowserKey { case up, down, left, right, confirm, escape }

    /// Keyboard while the browser is open (the mac editor routes keys here):
    /// arrows move through the browsed bank's grid and load at once (up /
    /// down = previous / next patch like ◀ ▶, left / right = the neighbouring
    /// column, nearest non-empty slot), Return / Enter / Esc close. Esc also
    /// closes the logo / effect type menus. False = not ours: the host keeps the key.
    func browserKey(_ key: BrowserKey) -> Bool {
        if update != nil, key == .escape || key == .confirm {
            update = nil
            return true
        }
        if showLogoMenu || showEffectMenu, key == .escape {
            showLogoMenu = false
            showEffectMenu = false
            return true
        }
        guard showBrowser else { return false }
        if bankToDelete != nil { // the confirmation: Esc cancels, nothing else reaches the browser
            if key == .escape { bankToDelete = nil }
            return true
        }
        if tagEdit != nil { // Esc closes it; its text field keeps the rest
            if key == .escape { tagEdit = nil }
            return true
        }
        if duplicateGroups != nil { // results stay put until asked to close
            if key == .escape { duplicateGroups = nil }
            return true
        }
        if key == .escape {
            closeBrowser()
            return true
        }
        guard !writing else { return false } // the write dialog's text field keeps them
        if key == .confirm {
            closeBrowser()
            return true
        }
        guard let b = currentBank, b < banks.count else { return true }
        let slots = banks[b].slots
        guard b == loadedBank, let p = currentProgram else {
            if let first = slots.firstIndex(where: { !$0.isEmpty }) { load(bank: b, slot: first) }
            return true
        }
        switch key {
        case .up, .down:
            step(key == .down ? 1 : -1)
        default:
            let rows = Self.browserRows(slots.count)
            let col = p / rows + (key == .right ? 1 : -1)
            guard col >= 0, col * rows < slots.count else { break }
            let wanted = col * rows + p % rows
            let column = (col * rows)..<min((col + 1) * rows, slots.count)
            if let s = column.filter({ !slots[$0].isEmpty }).min(by: { abs($0 - wanted) < abs($1 - wanted) }) {
                load(bank: b, slot: s)
            }
        }
        return true
    }

    var programLabel: String {
        guard let b = currentBank, let p = currentProgram, b < banks.count else { return "" }
        return String(format: "%@  %03d", banks[b].name, p + 1)
    }

    /// Synth1 "write": the current sound, renamed, into a bank slot.
    @discardableResult
    func write(bank: Int, slot: Int, name: String) -> Bool {
        guard host.writeLibraryPatch(bank: bank, slot: slot, name: name) else {
            flash("write failed")
            return false
        }
        loadLibrary()
        currentBank = bank
        currentProgram = slot
        loadedBankID = banks[bank].id
        poll()
        flash(String(format: "written to %02d:%@ %03d", bank, banks[bank].name, slot + 1))
        return true
    }

    /// The bank the write dialog proposes for a patch out of a read-only bank
    /// (EngineCore kUserBankName: a new library's first bank).
    static let userBankName = "User"
    /// Slots per bank (EngineCore kBankSlots).
    static let bankSlots = 128
    static let bankFullMessage = "Bank is full (128 patches): cannot add a patch"

    /// The library's own "User" bank, nil when the user deleted it.
    var userBank: Int? {
        banks.firstIndex { !$0.readOnly && $0.name == Self.userBankName }
    }

    /// Write into a bank that does not exist yet (the write dialog's
    /// proposed "User" bank): the folder is made by the write, not before.
    @discardableResult
    func write(newBank bankName: String, slot: Int, name: String) -> Bool {
        let i = host.newBank(bankName)
        loadLibrary()
        guard i >= 0, i < banks.count else {
            flash("write failed")
            return false
        }
        return write(bank: i, slot: slot, name: name)
    }

    /// Alt-click on write: the current sound straight back into the loaded
    /// program (same name), no dialog. False = no loaded program, or it is
    /// in the read-only factory bank.
    func writeLoadedProgram() -> Bool {
        guard let b = loadedBank, let p = currentProgram, !banks[b].readOnly else { return false }
        return write(bank: b, slot: p, name: patchName)
    }

    /// What a delete does here: the mac has a Trash, the iPad deletes.
    #if os(macOS)
    static let deletedWord = "moved to Trash"
    #else
    static let deletedWord = "deleted"
    #endif

    /// Browser right click (mac) / long press (iPad) > Delete: the slot's
    /// .sy1 to the Trash (mac) / deleted (iPad), then a rescan. The sound
    /// playing stays as it is.
    func deleteSlot(bank: Int, slot: Int) {
        let path = host.slotPath(bank: bank, slot: slot)
        guard !path.isEmpty, bank < banks.count else { return }
        let label = String(format: "%03d:%@", slot + 1, banks[bank].slots[slot])
        let url = URL(fileURLWithPath: path)
        do {
            #if os(macOS)
            try FileManager.default.trashItem(at: url, resultingItemURL: nil)
            #else
            try FileManager.default.removeItem(at: url)
            #endif
            flash("\(label) \(Self.deletedWord)")
        } catch {
            flash("delete failed")
        }
        loadLibrary(rescan: true)
    }

    /// The confirmed Delete bank: the folder goes (EngineCore PatchManager::
    /// DeleteBank), the browser shows the bank now at its place.
    func deleteBank(id: String) {
        bankToDelete = nil
        guard let b = banks.firstIndex(where: { $0.id == id }), !banks[b].readOnly else { return }
        let name = banks[b].name
        let ok = host.deleteBank(b)
        loadLibrary()
        if loadedBankID == id { // the sound plays on, out of no bank (the host forgot it too)
            currentProgram = nil
            loadedBankID = nil
        }
        flash(ok ? "bank \(name) \(Self.deletedWord)" : "delete failed")
    }

    /// Browser drag and drop within the shown bank: move, or swap with an
    /// occupied slot (the files are renamed). The loaded program follows.
    func movePatch(bank: Int, from: Int, to: Int) {
        guard from != to, host.movePatch(bank: bank, from: from, to: to) else { return }
        loadLibrary()
        syncProgram(showBank: false)
        flash(String(format: "moved %03d -> %03d", from + 1, to + 1))
    }

    /// Browser: a patch dropped on a bank = a copy in that bank's first free
    /// slot (e.g. building a favorites bank); the browser stays on the bank
    /// shown, the loaded program stays.
    func copyPatch(bank: Int, slot: Int, to: Int) {
        guard bank < banks.count, to < banks.count else { return }
        let target = String(format: "%02d:%@", to, banks[to].name)
        let s = host.copyPatch(bank: bank, slot: slot, to: to)
        loadLibrary()
        switch s {
        case 0...: flash(String(format: "copied to %@ %03d", target, s + 1), seconds: 3)
        case -1: flash(Self.bankFullMessage, seconds: 5)
        default: flash("copy failed")
        }
    }

    /// Browser > Export bank…, once the target folder is picked: a folder
    /// named after the bank's, holding its patches, is created there.
    func exportBank(id: String, to dir: URL) {
        guard let b = banks.firstIndex(where: { $0.id == id }) else { return }
        let made = SynthLibrary.withAccess([dir]) { host.exportBank(b, to: $0) }
        flash(made.isEmpty ? "export failed" : "bank exported as \(URL(fileURLWithPath: made).lastPathComponent)", seconds: 4)
    }

    func newBank(_ name: String) {
        let i = host.newBank(name)
        loadLibrary()
        if i >= 0 { currentBank = i }
    }

    /// Copies bank folders, zip banks (extracted) or loose .sy1 files into
    /// the library; selects the first new bank.
    func importItems(_ urls: [URL]) {
        let out = SynthLibrary.withAccess(urls) { host.importItems($0) }
        loadLibrary()
        if let first = out.added.first?.path,
           let i = banks.firstIndex(where: { $0.source == first || $0.source.hasPrefix(first + "/") }) {
            currentBank = i // a picked container: its first bank
        }
        // An active filter can hide the new patches entirely, so a
        // successful import must not look like a no-op.
        if !out.isEmpty && hasActiveFilter { clearLibraryFilter() }
        let text = Self.describe(out)
        importNotice = text
        importNoticeHide = Date().addingTimeInterval(5)
        flash(text, seconds: 5)
    }

    /// Short "where / what" for a library JSON decode failure, e.g.
    /// `banks[0].favs[3]`. A blank browser with no message hides a
    /// programming error behind "the library is empty".
    static func describe(error: Error) -> String {
        guard let decoding = error as? DecodingError else { return error.localizedDescription }
        func path(_ keys: [CodingKey]) -> String {
            keys.reduce(into: "") { out, key in
                if let i = key.intValue { out += "[\(i)]" } else { out += out.isEmpty ? key.stringValue : ".\(key.stringValue)" }
            }
        }
        func where_(_ ctx: DecodingError.Context) -> String {
            let p = path(ctx.codingPath)
            return p.isEmpty ? "library" : p
        }
        switch decoding {
        case let .keyNotFound(key, ctx):
            let p = where_(ctx)
            return p.isEmpty ? "missing \(key.stringValue)" : "\(p).\(key.stringValue): missing"
        case let .typeMismatch(_, ctx), let .valueNotFound(_, ctx):
            return "\(where_(ctx)): \(ctx.debugDescription)"
        case let .dataCorrupted(ctx):
            return ctx.debugDescription
        @unknown default:
            return error.localizedDescription
        }
    }

    /// "imported 3 patches into Imported" - the browser header shows this
    /// as well, because the LCD is covered by the browser.
    static func describe(_ out: ImportOutcome) -> String {
        if out.isEmpty {
            if let why = out.refused.first { return "nothing imported - \(why)" }
            return "nothing imported"
        }
        let n = out.patches == 1 ? "1 patch" : "\(out.patches) patches"
        var text = out.added.count == 1 && !out.added[0].path.isEmpty
            ? "imported \(n) into \(lastPathComponent(out.added[0].path))"
            : "imported \(n) into \(out.added.count) banks"
        if !out.refused.isEmpty { text += " (\(out.refused.count) skipped)" }
        return text
    }

    private static func lastPathComponent(_ p: String) -> String {
        p.split(separator: "/").last.map(String.init) ?? p
    }

    // MARK: - categories, favourites and tags

    /// A slot's marks in the current bank list (nil = empty / out of range).
    func patchMark(bank: Int, slot: Int) -> (category: Int, favourite: Bool, tags: [String])? {
        guard bank >= 0, bank < banks.count, slot >= 0, slot < banks[bank].slots.count,
              !banks[bank].slots[slot].isEmpty else { return nil }
        return (banks[bank].category(slot), banks[bank].isFavourite(slot), banks[bank].tags(slot))
    }

    func categoryName(_ category: Int) -> String {
        category >= 0 && category < categoryNames.count ? categoryNames[category] : "Unclassified"
    }

    /// `category` = -1 puts the slot back on the name rule.
    func setCategory(bank: Int, slot: Int, _ category: Int) {
        host.setPatchCategory(bank: bank, slot: slot, category: category)
        loadLibrary()
    }

    func toggleFavourite(bank: Int, slot: Int) {
        guard let mark = patchMark(bank: bank, slot: slot) else { return }
        host.setPatchFavourite(bank: bank, slot: slot, on: !mark.favourite)
        loadLibrary()
    }

    func addTag(_ tag: String, bank: Int, slot: Int) {
        let clean = tag.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !clean.isEmpty else { return }
        host.addPatchTag(bank: bank, slot: slot, tag: clean)
        loadLibrary()
    }

    func removeTag(_ tag: String, bank: Int, slot: Int) {
        host.removePatchTag(bank: bank, slot: slot, tag: tag)
        loadLibrary()
    }

    /// A slot's colour from the .sy1 itself ("default" = untinted).
    func patchColor(bank: Int, slot: Int) -> String {
        guard bank >= 0, bank < banks.count, slot >= 0, slot < banks[bank].slots.count else { return "default" }
        return banks[bank].color(slot)
    }

    func setPatchColor(bank: Int, slot: Int, _ color: String) {
        guard host.setPatchColor(bank: bank, slot: slot, color: color) else {
            flash("could not colour this patch")
            return
        }
        loadLibrary()
    }

    /// Writes the whole store to `path` (the portable file to keep).
    func exportTags(_ path: String) -> Bool {
        let ok = host.exportTags(path)
        flash(ok ? "categories and tags saved" : "could not save categories and tags")
        return ok
    }

    /// Merges a store file from another setup: marks we already have win.
    func importTags(_ path: String) {
        let added = host.importTags(path)
        loadLibrary()
        if added < 0 {
            flash("not a Synth2 categories file")
        } else {
            flash(added == 0 ? "no new categories or tags" : "added marks for \(added) patch(es)")
        }
    }

    func allNotesOff() {
        host.allNotesOff()
        flash("all notes off")
    }

    func initializePatch() {
        host.initializePatch()
        currentProgram = nil
        loadedBankID = nil
        poll()
    }

    func loadFile(_ path: String) {
        if host.loadPatchFile(path) {
            currentProgram = nil
            loadedBankID = nil
            poll()
        }
    }

    func saveFile(_ path: String) -> Bool { host.savePatchFile(path) }
}
