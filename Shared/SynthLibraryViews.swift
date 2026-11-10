import SwiftUI
import UniformTypeIdentifiers
#if os(macOS)
import AppKit
#endif

private let sy1Type = UTType(filenameExtension: "sy1") ?? .data

/// Synth1's bottom strip: the program LCD (patch on line 1, bank on line 2,
/// transient value / MIDI learn messages over the bank), write / menu.
struct BottomBar: View {
    @ObservedObject var model: SynthModel
    @Environment(\.theme) private var theme

    var body: some View {
        ZStack(alignment: .topLeading) {
            // Synth1's "vol": master volume, not part of the patch (session only).
            KnobView(model: model, id: SynthModel.volumeID, label: "vol", size: 26).place(0, 12, 38, 46)
            // write / menu end at the Wheel / MIDI section's right edge (x 542 on the panel)
            lcd.place(44, 0, 432, 70)
            // write: opens the write dialog; a 2nd click (dialog open) writes
            // like its Write / Overwrite button; Alt-click writes straight
            // back to the loaded program (Synth1's quick save).
            PanelButton(title: "write", width: 50, height: 26, fill: Theme.red) {
                if model.showBrowser && model.writing {
                    model.writeCommits += 1
                } else if !(optionHeld() && model.writeLoadedProgram()) {
                    model.openBrowser(write: true)
                }
            }
                .place(486, 4, 50, 26)
            mainMenu.place(486, 38, 50, 26)
            if !model.learnActive {
                // Spans the Voice section above (x 548..764 on the panel),
                // centred on the gap between write and menu.
                VoiceGrid(activity: model.voices, polyphony: model.values[94])
                    .place(758 - VoiceGrid.size.width, 34 - VoiceGrid.size.height / 2, VoiceGrid.size.width, VoiceGrid.size.height)
            } else {
                Button(action: { model.exitLearn() }) {
                    Text("Exit MIDI Learn").font(.system(size: 12, weight: .semibold))
                        .foregroundColor(Color(red: 0.95, green: 0.87, blue: 0.87))
                        .frame(width: 200, height: 28)
                        .background(RoundedRectangle(cornerRadius: 4).fill(Theme.red))
                        .overlay(RoundedRectangle(cornerRadius: 4).strokeBorder(Color.black.opacity(0.6), lineWidth: 1))
                }
                .buttonStyle(.plain)
                .place(540, 4, 200, 28)
                Text("Click a control, then move a controller knob.\nDouble-click a control to clear its CC.")
                    .font(.system(size: 9.5)).foregroundColor(theme.dimInk).fixedSize()
                    .place(540, 36, 210, 30)
            }
        }
    }

    private var programPrefix: String {
        model.currentProgram.map { String(format: "%03d:", $0 + 1) } ?? "---:"
    }

    private var bankLine: String {
        guard let b = model.loadedBank ?? model.currentBank, b < model.banks.count else {
            return "click here to browse banks"
        }
        return String(format: "%02d:%@", b, model.banks[b].name)
    }

    private var lcd: some View {
        ZStack(alignment: .topLeading) {
            // Synth1: a click on the display opens the patch browser.
            RoundedRectangle(cornerRadius: 4)
                .fill(LinearGradient(colors: [theme.lcdTop, theme.lcdBottom], startPoint: .top, endPoint: .bottom))
                .overlay(RoundedRectangle(cornerRadius: 4).strokeBorder(Color.black, lineWidth: 1.5))
                .onTapGesture { toggleBrowser() }
            Text(programPrefix + model.patchName)
                .font(.system(size: 17, weight: .semibold, design: .monospaced))
                .foregroundColor(Theme.lcdText)
                .lineLimit(1)
                .frame(maxWidth: .infinity, alignment: .leading)
                .contentShape(Rectangle())
                .onTapGesture { toggleBrowser() }
                .place(10, 6, 328, 26)
            HStack(spacing: 4) {
                PanelButton(title: "◀", width: 36, height: 20) { model.step(-1) }
                PanelButton(title: "▶", width: 36, height: 20) { model.step(1) }
            }
            .place(348, 9, 76, 20)
            Text(model.readout ?? bankLine)
                .font(.system(size: 13, weight: .medium, design: .monospaced))
                .foregroundColor(model.readout == nil ? Theme.lcdText.opacity(0.85) : theme.displayText)
                .lineLimit(1)
                .frame(maxWidth: .infinity, alignment: .leading)
                .contentShape(Rectangle())
                .onTapGesture { toggleBrowser() }
                .place(10, 38, 412, 24)
        }
    }

    private func toggleBrowser() {
        if model.showBrowser { model.closeBrowser() } else { model.openBrowser() }
    }

    private var mainMenu: some View {
        Menu {
            // Library chores (Import…, library folder, Refresh) live in the
            // browser's header; write = the red button.
            Button("Initialize patch") { model.initializePatch() }
            Divider()
            Button("Load patch file…") { model.fileAction = .loadPatch }
            Button("Save patch file…") { model.fileAction = .savePatch }
            Divider()
            Button(model.learnActive ? "Exit MIDI Learn" : "MIDI Learn") { model.toggleLearn() }
            Toggle("Program change", isOn: $model.programChange)
            Button("All notes off") { model.allNotesOff() }
            Divider()
            Button("Themes…") { model.showThemes = true }
            #if os(macOS)
            if UpdateCheck.enabled {
                Divider()
                Button("Check for updates…") { model.checkForUpdates() }
            }
            #endif
        } label: {
            // .button + .plain: the label is drawn as-is (centred), not
            // re-laid out by the borderless menu style.
            Text("menu").font(.system(size: 11, weight: .semibold)).foregroundColor(Theme.buttonText)
                .frame(width: 50, height: 26)
                .background(ButtonFace())
                .contentShape(Rectangle())
        }
        .menuStyle(.button)
        .buttonStyle(.plain)
        .menuIndicator(.hidden)
        .frame(width: 50, height: 26)
    }
}

/// Synth1's voice activity squares: 2 x 16 cells = the 32 voices, black =
/// available (within the polyphony), white = held, gray = releasing, empty
/// = past the polyphony.
struct VoiceGrid: View {
    @ObservedObject var activity: VoiceActivity
    @Environment(\.theme) private var theme
    let polyphony: Int
    /// Exactly the Voice section's width (216 incl. the outer lines).
    static let columns = 16, rows = 2, cell: CGFloat = 215.0 / 16
    static let size = CGSize(width: cell * CGFloat(columns) + 1, height: cell * CGFloat(rows) + 1)

    var body: some View {
        let states = activity.states, poly = polyphony
        Canvas { ctx, _ in
            let c = Self.cell
            for i in 0..<Self.columns * Self.rows {
                let rect = CGRect(x: 0.5 + CGFloat(i % Self.columns) * c, y: 0.5 + CGFloat(i / Self.columns) * c, width: c, height: c)
                let s = states >> (2 * UInt64(i)) & 3
                // inset 1 pt: a 2 pt gap (the line + half a point each side) between filled cells
                if let fill = s == 2 ? Color.white : s == 1 ? Color(white: 0.55) : i < poly ? Color.black : nil {
                    ctx.fill(Path(rect.insetBy(dx: 1, dy: 1)), with: .color(fill))
                }
            }
            var lines = Path()
            for x in 0...Self.columns {
                lines.move(to: CGPoint(x: 0.5 + CGFloat(x) * c, y: 0))
                lines.addLine(to: CGPoint(x: 0.5 + CGFloat(x) * c, y: Self.size.height))
            }
            for y in 0...Self.rows {
                lines.move(to: CGPoint(x: 0, y: 0.5 + CGFloat(y) * c))
                lines.addLine(to: CGPoint(x: Self.size.width, y: 0.5 + CGFloat(y) * c))
            }
            ctx.stroke(lines, with: .color(theme.ink.opacity(0.12)), lineWidth: 1)
        }
        .frame(width: Self.size.width, height: Self.size.height)
        .allowsHitTesting(false)
    }
}

/// The patch browser over the panel (bank list + 128-slot grid, a click
/// loads and the browser stays open for auditioning, a double click loads
/// and closes, drag and drop moves / swaps patches) and, with
/// `model.writing`, Synth1's write dialog: pick a bank (or make a new one)
/// and a slot, name the patch, Write (an occupied slot says "Overwrite").
/// Banks are the folders of the library: Import… copies into it, the rest
/// (rename, share) happens in Finder (right click > Show in Finder); right
/// click (mac) / long press (iPad) > Delete removes a patch file, > Delete
/// bank… a bank after a confirmation (mac: to the Trash, iPad: deleted).
struct PatchBrowserView: View {
    @ObservedObject var model: SynthModel
    @Environment(\.theme) private var theme
    @State private var target: Int?
    @State private var name = ""
    @State private var newBankName = ""
    @State private var naming = false
    /// Last slot click, for the double click (a single click must load at
    /// once for auditioning, so no count: 2 gesture that would delay it).
    @State private var lastClick: (bank: Int, slot: Int, time: Date)?
    /// Slot under a drag (drop highlight).
    @State private var dropTarget: Int?
    /// Bank row under a drag (copy highlight).
    @State private var dropBank: Int?
    /// Write dialog on the proposed "User" bank the library lacks (an extra
    /// row; the write creates the folder).
    @State private var newUserBank = false
    private static let dragPrefix = SlotDrop.prefix
    private static let newUserBankRow = -1 // its scroll id (real rows: bank indices)

    private var bank: PatchBankInfo? {
        model.currentBank.flatMap { $0 < model.banks.count ? model.banks[$0] : nil }
    }

    var body: some View {
        VStack(spacing: 0) {
            header
            HStack(spacing: 0) {
                bankList.frame(width: 190).frame(maxHeight: .infinity)
                Rectangle().fill(theme.edge).frame(width: 1)
                // iOS: a ScrollView takes its ideal width otherwise
                slotGrid.frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .background(theme.panel)
        .overlay(Rectangle().strokeBorder(theme.edge, lineWidth: 1.5))
        .overlay {
            if let id = model.bankToDelete, let bank = model.banks.first(where: { $0.id == id }) {
                DeleteBankDialog(model: model, bank: bank)
            }
        }
        .shadow(color: .black.opacity(0.4), radius: 6)
        .onAppear {
            name = model.patchName
            leaveReadOnlyBank()
            pickTarget()
        }
        .onChange(of: model.currentBank) { _, _ in pickTarget() }
        .onChange(of: model.writing) { _, _ in
            name = model.patchName
            newUserBank = false
            leaveReadOnlyBank()
            pickTarget()
        }
        // a scan landing while the dialog is open (opened before the list
        // arrived, or a User bank appeared)
        .onChange(of: model.banks.map(\.id)) { _, _ in
            let was = newUserBank
            leaveReadOnlyBank()
            if was != newUserBank { pickTarget() }
        }
        .onChange(of: model.writeCommits) { _, _ in commitWrite() }
    }

    // MARK: header

    private var header: some View {
        HStack(spacing: 6) {
            Text(model.writing ? "Write patch" : "Banks")
                .font(.system(size: 12, weight: .semibold)).foregroundColor(theme.headerText)
            if model.libraryLoading && !model.banks.isEmpty { // the list shown may still change
                Text("loading…").font(.system(size: 11)).foregroundColor(theme.headerText.opacity(0.6))
            }
            Spacer(minLength: 6)
            if naming {
                field("new bank name", $newBankName, width: 180) { createBank() }
                button("Create") { createBank() }
                button("✕", width: 24) { naming = false }
            } else if model.writing {
                field("patch name", $name, width: 220) { commitWrite() }
                writeButton
                button("New bank…", width: 72) { startNaming() }
                button("Cancel") { model.closeBrowser() }
            } else {
                #if os(macOS)
                button("Import…") { model.fileAction = .importBanks }
                button("Open library folder", width: 120) { Finder.open(model.host.libraryRoot) }
                #else
                // two pickers: the iOS one can't offer folders and files at once
                button("Import folder…", width: 96) { model.fileAction = .importFolder }
                button("Import files…", width: 90) { model.fileAction = .importBanks }
                #endif
                button("Refresh") { model.refreshLibrary() }
                PanelButton(title: "Close", width: 60, height: 20, icon: "xmark") { model.closeBrowser() }
            }
        }
        .padding(.horizontal, 8)
        .frame(height: 28)
        .background(theme.header)
    }

    private var writeButton: some View {
        let bank = newUserBank ? nil : self.bank
        let occupied = target.flatMap { t in bank.map { !$0.slots[t].isEmpty } } ?? false
        let label = target.map { occupied ? String(format: "Overwrite %03d", $0 + 1) : String(format: "Write to %03d", $0 + 1) }
            ?? "Write"
        let off = target == nil || (!newUserBank && (bank == nil || bank?.readOnly == true))
        return PanelButton(title: label, width: 110, height: 20, fill: occupied ? Theme.red : theme.button) { commitWrite() }
            .disabled(off)
            .opacity(off ? 0.5 : 1)
    }

    private func button(_ title: String, width: CGFloat = 60, action: @escaping () -> Void) -> some View {
        PanelButton(title: title, width: width, height: 20, action: action)
    }

    private func field(_ prompt: String, _ text: Binding<String>, width: CGFloat, submit: @escaping () -> Void) -> some View {
        TextField(prompt, text: text)
            .textFieldStyle(.plain)
            .font(.system(size: 12, design: .monospaced))
            .foregroundColor(Theme.lcdText)
            .padding(.horizontal, 6)
            .frame(width: width, height: 20)
            .background(RoundedRectangle(cornerRadius: 3).fill(theme.displayFill))
            .onSubmit(submit)
    }

    // MARK: banks

    private var bankList: some View {
        ScrollViewReader { proxy in
            ScrollView {
                VStack(alignment: .leading, spacing: 1) {
                    ForEach(model.banks.indices, id: \.self) { b in bankRow(b) }
                    if model.writing && !model.banks.isEmpty && model.userBank == nil { newUserBankRow }
                    if model.banks.isEmpty {
                        Text(model.libraryLoading ? "Loading…"
                             : "No banks. Import… copies bank folders, zip banks or .sy1 patches into the library.")
                            .font(.system(size: 10)).foregroundColor(Theme.lcdText.opacity(0.7)).padding(8)
                    }
                }
                .padding(.top, 4)
            }
            .scrollDeadZone()
            // Keyboard steps roll into other banks: keep the bank in view.
            .onChange(of: model.currentBank) { _, b in if let b { proxy.scrollTo(b) } }
            .onChange(of: newUserBank) { _, on in if on { proxy.scrollTo(Self.newUserBankRow) } }
            .onAppear {
                if newUserBank { proxy.scrollTo(Self.newUserBankRow) } else if let b = model.currentBank { proxy.scrollTo(b) }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading) // also with no rows (loading)
        .background(theme.lcdBottom)
    }

    private func bankLabel(_ label: String, count: String, selected: Bool) -> some View {
        HStack(spacing: 4) {
            Text(label).lineLimit(1)
                .oxoTruncationTooltip(label, size: 11, monospaced: true, optional: false)
            Spacer(minLength: 2)
            Text(count).opacity(0.6)
        }
        .font(.system(size: 11, design: .monospaced))
        .foregroundColor(selected ? theme.lcdBottom : Theme.lcdText)
        .padding(.horizontal, 6).padding(.vertical, 3)
        .background(selected ? theme.displayText : Color.clear)
    }

    /// The write dialog's "User" bank when the library has none: listed
    /// last, no number (it has no index yet), made by the write.
    private var newUserBankRow: some View {
        bankLabel("--:" + SynthModel.userBankName, count: "new", selected: newUserBank)
            .id(Self.newUserBankRow)
            .contentShape(Rectangle())
            .onTapGesture {
                newUserBank = true
                pickTarget()
            }
    }

    private func bankRow(_ b: Int) -> some View {
        let info = model.banks[b]
        let selected = !newUserBank && b == model.currentBank
        let locked = model.writing && info.readOnly // the write dialog can't pick it
        return bankLabel(String(format: "%02d:", b) + info.name, count: "\(info.count)", selected: selected)
        .overlay(Rectangle().strokeBorder(theme.displayText, lineWidth: dropBank == b ? 1.5 : 0))
        .opacity(locked ? 0.35 : 1)
        .contentShape(Rectangle())
        .onTapGesture {
            guard !locked else { return }
            newUserBank = false
            model.currentBank = b
            pickTarget()
        }
        // a patch dragged onto a bank: copied into its first free slot
        .onDrop(of: SlotDrop.types, delegate: BankDrop(bank: b, enabled: !info.readOnly && !model.writing,
                                                       isFull: info.count >= SynthModel.bankSlots, target: $dropBank,
                                                       full: { model.flash(SynthModel.bankFullMessage, seconds: 3) }) {
            model.copyPatch(bank: $0, slot: $1, to: b)
        })
        .libraryMenu(enabled: !locked, export: { model.fileAction = .exportBank(info.id) }, deleteTitle: "Delete bank…",
                     delete: info.readOnly ? nil : { model.bankToDelete = info.id }) {
            info.source
        }
    }

    // MARK: slots

    private var slotGrid: some View {
        ScrollViewReader { proxy in
            ScrollView {
                if model.writing && newUserBank { // the bank to be made: all slots free
                    slotColumns(SynthModel.bankSlots) { s in
                        slotFace(s, "", selected: target == s)
                            .contentShape(Rectangle())
                            .onTapGesture { target = s }
                    }
                } else if let b = model.currentBank, b < model.banks.count {
                    slotColumns(model.banks[b].slots.count) { s in slotCell(b, s) }
                }
            }
            .scrollDeadZone()
            .onChange(of: [model.currentBank, model.currentProgram]) { _, _ in scrollToLoaded(proxy) }
            .onAppear { scrollToLoaded(proxy) }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(theme.lcdBottom)
    }

    private func scrollToLoaded(_ proxy: ScrollViewProxy) {
        if let p = model.currentProgram, model.loadedBank == model.currentBank { proxy.scrollTo(p) }
    }

    /// 4 columns filled top to bottom (1-32, 33-64, ...); ids = slot
    /// numbers, so the keyboard selection can scroll in.
    private func slotColumns<Cell: View>(_ count: Int, @ViewBuilder cell: @escaping (Int) -> Cell) -> some View {
        let rows = SynthModel.browserRows(count)
        return LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 2), count: 4), spacing: 2) {
            ForEach((0..<rows * 4).map { $0 % 4 * rows + $0 / 4 }, id: \.self) { s in
                if s < count { cell(s) } else { Color.clear }
            }
        }
        .padding(6)
    }

    private func slotFace(_ s: Int, _ patch: String, selected: Bool) -> some View {
        Text(String(format: "%03d:", s + 1) + patch)
            .font(.system(size: 11, design: .monospaced))
            .foregroundColor(selected ? theme.lcdBottom : patch.isEmpty ? Theme.lcdText.opacity(0.3) : Theme.lcdText)
            .lineLimit(1)
            // a cut name shows whole (not a help tip: always on)
            .oxoTruncationTooltip(String(format: "%03d:", s + 1) + patch, size: 11, monospaced: true, optional: false)
            .padding(.horizontal, 5).padding(.vertical, 3)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(RoundedRectangle(cornerRadius: 2).fill(selected ? theme.displayText : theme.display))
    }

    private func slotCell(_ b: Int, _ s: Int) -> some View {
        let patch = model.banks[b].slots[s]
        let readOnly = model.banks[b].readOnly
        let selected = model.writing ? target == s : (b == model.loadedBank && s == model.currentProgram)
        return slotFace(s, patch, selected: selected)
            .overlay(RoundedRectangle(cornerRadius: 2).strokeBorder(theme.displayText, lineWidth: dropTarget == s ? 1.5 : 0))
            .contentShape(Rectangle())
            .onTapGesture {
                if model.writing {
                    target = s
                } else if !patch.isEmpty {
                    // double click = load + close the browser
                    if let c = lastClick, c.bank == b, c.slot == s, Date().timeIntervalSince(c.time) < 0.4 {
                        lastClick = nil
                        return model.closeBrowser()
                    }
                    lastClick = (b, s, Date())
                    model.load(bank: b, slot: s)
                }
            }
            .libraryMenu(enabled: !patch.isEmpty, delete: readOnly ? nil : { model.deleteSlot(bank: b, slot: s) }) {
                model.host.slotPath(bank: b, slot: s)
            }
            // drag a patch onto another slot: move there (swap if taken); onto
            // a bank: copy (factory patches can only be copied)
            .slotDraggable(patch.isEmpty ? nil : Self.dragPrefix + "\(b):\(s)",
                           label: String(format: "%03d:", s + 1) + patch, fill: theme.display)
            .onDrop(of: SlotDrop.types, delegate: SlotDrop(bank: b, slot: s, enabled: !readOnly, target: $dropTarget) { from in
                model.movePatch(bank: b, from: from, to: s)
            })
    }

    // MARK: actions

    /// Write dialog default: the loaded slot when writing back into its bank,
    /// else the bank's first empty slot.
    private func pickTarget() {
        guard model.writing else { return target = nil }
        if newUserBank { return target = 0 }
        guard let b = model.currentBank, b < model.banks.count else { return target = nil }
        if b == model.loadedBank, let p = model.currentProgram { return target = p }
        target = model.banks[b].slots.firstIndex(where: \.isEmpty) ?? 0
    }

    /// The write dialog never targets the read-only factory bank: out of it
    /// (or out of no bank) it opens on the "User" bank, proposed even when
    /// the library has none (the write makes it). A writable bank in view
    /// stays (the loaded one: written back).
    private func leaveReadOnlyBank() {
        guard model.writing, !model.banks.isEmpty else { return }
        if newUserBank { // keep the proposal until a real User bank shows up
            if let u = model.userBank {
                newUserBank = false
                model.currentBank = u
            }
            return
        }
        if let b = model.currentBank, b < model.banks.count, !model.banks[b].readOnly { return }
        if let u = model.userBank { model.currentBank = u } else { newUserBank = true }
    }

    private func commitWrite() {
        guard let t = target else { return }
        if newUserBank {
            if model.write(newBank: SynthModel.userBankName, slot: t, name: name) { model.closeBrowser() }
            return
        }
        guard let b = model.currentBank, b < model.banks.count, !model.banks[b].readOnly else { return }
        if model.write(bank: b, slot: t, name: name) { model.closeBrowser() }
    }

    private func startNaming() {
        newBankName = ""
        naming = true
    }

    private func createBank() {
        newUserBank = false
        model.newBank(newBankName)
        naming = false
        pickTarget()
    }
}

/// Drop on a browser slot: proposes a MOVE within the bank (no "+" copy
/// badge on the cursor), highlights the slot. Never in the read-only
/// factory bank.
private struct SlotDrop: DropDelegate {
    static let prefix = "synth2-slot:"
    static let types: [UTType] = [.plainText, .utf8PlainText, .text]
    let bank: Int, slot: Int, enabled: Bool
    @Binding var target: Int?
    let move: (Int) -> Void

    func validateDrop(info: DropInfo) -> Bool { enabled }
    func dropEntered(info: DropInfo) { target = slot }
    func dropExited(info: DropInfo) { if target == slot { target = nil } }
    func dropUpdated(info: DropInfo) -> DropProposal? { DropProposal(operation: .move) }

    func performDrop(info: DropInfo) -> Bool {
        target = nil
        let (b, s) = (bank, slot)
        return Self.read(info) { from in
            if from.bank == b && from.slot != s { move(from.slot) }
        }
    }

    /// The dragged slot ("synth2-slot:bank:slot" payload), on the main thread.
    static func read(_ info: DropInfo, _ then: @escaping ((bank: Int, slot: Int)) -> Void) -> Bool {
        guard let provider = info.itemProviders(for: types).first else { return false }
        _ = provider.loadObject(ofClass: NSString.self) { object, _ in
            guard let item = object as? String, item.hasPrefix(prefix) else { return }
            let parts = item.dropFirst(prefix.count).split(separator: ":").compactMap { Int($0) }
            guard parts.count == 2 else { return }
            DispatchQueue.main.async { then((parts[0], parts[1])) }
        }
        return true
    }
}

/// Drop on a bank row: proposes a COPY ("+" on the cursor) into the bank's
/// first free slot; the browser stays on the bank shown. Not on the
/// read-only factory bank. A FULL bank refuses it: forbidden cursor, no
/// highlight, `full` (the LCD says why) on entering.
private struct BankDrop: DropDelegate {
    let bank: Int, enabled: Bool, isFull: Bool
    @Binding var target: Int?
    let full: () -> Void
    let copy: (_ bank: Int, _ slot: Int) -> Void

    func validateDrop(info: DropInfo) -> Bool { enabled }
    func dropEntered(info: DropInfo) {
        if isFull { full() } else { target = bank }
    }
    func dropExited(info: DropInfo) {
        if target == bank { target = nil }
        #if os(macOS)
        if isFull { NSCursor.arrow.set() }
        #endif
    }
    func dropUpdated(info: DropInfo) -> DropProposal? {
        #if os(macOS)
        // .forbidden alone leaves the plain arrow on macOS
        if isFull { NSCursor.operationNotAllowed.set() }
        #endif
        return DropProposal(operation: isFull ? .forbidden : .copy)
    }

    func performDrop(info: DropInfo) -> Bool {
        target = nil
        guard !isFull else { return false }
        return SlotDrop.read(info) { from in copy(from.bank, from.slot) }
    }
}

extension View {
    /// Draggable slot (payload = "synth2-slot:bank:slot"), nothing for an empty one.
    @ViewBuilder
    func slotDraggable(_ payload: String?, label: String, fill: Color) -> some View {
        if let payload {
            draggable(payload) {
                Text(label).font(.system(size: 11, design: .monospaced)).foregroundColor(Theme.lcdText)
                    .padding(.horizontal, 5).padding(.vertical, 3)
                    .background(RoundedRectangle(cornerRadius: 2).fill(fill))
            }
        } else {
            self
        }
    }
}

private func optionHeld() -> Bool {
    #if os(macOS)
    return NSEvent.modifierFlags.contains(.option)
    #else
    return false
    #endif
}

#if os(macOS)
/// Finder actions (mac only: the AUv3 can't open the Files app).
enum Finder {
    static func open(_ path: String) { NSWorkspace.shared.open(URL(fileURLWithPath: path)) }
    static func reveal(_ path: String) { NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: path)]) }
}
#endif

extension View {
    /// Browser bank / slot menu: right click (mac) = [Export bank…,] Show in
    /// Finder[, Delete]; long press (iPad) = [Export bank…,] [Delete],
    /// nothing without either.
    @ViewBuilder
    func libraryMenu(enabled: Bool = true, export: (() -> Void)? = nil, deleteTitle: String = "Delete",
                     delete: (() -> Void)?, _ path: @escaping () -> String) -> some View {
        #if os(macOS)
        if enabled {
            contextMenu {
                if let export { Button("Export bank…") { export() } }
                Button("Show in Finder") { Finder.reveal(path()) }
                if let delete {
                    Divider()
                    Button(deleteTitle) { delete() }
                }
            }
        } else {
            self
        }
        #else
        if enabled, export != nil || delete != nil {
            contextMenu {
                if let export { Button("Export bank…", systemImage: "square.and.arrow.up") { export() } }
                if let delete { Button(deleteTitle, systemImage: "trash", role: .destructive) { delete() } }
            }
        } else {
            self
        }
        #endif
    }
}

/// Browser > Delete bank… confirmation, drawn inside the browser like the
/// other in-panel dialogs (the host owns the window). A click outside or
/// Cancel (Esc on mac) keeps the bank.
struct DeleteBankDialog: View {
    @ObservedObject var model: SynthModel
    let bank: PatchBankInfo

    private var detail: String {
        let patches = bank.count == 1 ? "1 patch" : "\(bank.count) patches"
        #if os(macOS)
        return "“\(bank.name)” (\(patches)) will be moved to the Trash."
        #else
        return "“\(bank.name)” (\(patches)) will be deleted. This can't be undone."
        #endif
    }

    var body: some View {
        ZStack {
            Color.black.opacity(0.3).contentShape(Rectangle()).onTapGesture { model.bankToDelete = nil }
            VStack(spacing: 0) {
                Text("Delete bank?").font(.system(size: 11, weight: .semibold)).foregroundColor(Dlg.text)
                    .frame(maxWidth: .infinity).frame(height: 22).background(Dlg.bar)
                VStack(alignment: .leading, spacing: 14) {
                    Text(detail).font(.system(size: 11)).foregroundColor(Dlg.text)
                        .fixedSize(horizontal: false, vertical: true)
                    HStack(spacing: 8) {
                        Spacer()
                        button("Cancel", fill: Dlg.bar) { model.bankToDelete = nil }
                        button("Delete", fill: Theme.red) { model.deleteBank(id: bank.id) }
                    }
                }
                .padding(14)
            }
            .frame(width: 340)
            .background(RoundedRectangle(cornerRadius: 8).fill(Dlg.bg))
            .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(Color.black.opacity(0.7), lineWidth: 1))
            .clipShape(RoundedRectangle(cornerRadius: 8))
            .shadow(color: .black.opacity(0.5), radius: 10, y: 4)
            #if os(macOS)
            .background(WheelShield())
            #endif
        }
    }

    private func button(_ title: String, fill: Color, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(title).font(.system(size: 11)).foregroundColor(Dlg.text)
                .padding(.horizontal, 12).frame(minWidth: 64).frame(height: 22)
                .background(RoundedRectangle(cornerRadius: 3).fill(fill))
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

/// Plain data file for the iPad exporter (.sy1).
struct DataDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.data] }
    var data: Data
    init(data: Data) { self.data = data }
    init(configuration: ReadConfiguration) throws { data = configuration.file.regularFileContents ?? Data() }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper { FileWrapper(regularFileWithContents: data) }
}

/// Runs `model.fileAction`: NSOpenPanel / NSSavePanel on mac, the system
/// file importer / exporter on iPad (security-scoped URLs).
struct FileActions: ViewModifier {
    @ObservedObject var model: SynthModel
    #if !os(macOS)
    @State private var importing = false
    @State private var importAction: FileAction = .importBanks
    @State private var exporting = false
    @State private var exportDoc = DataDocument(data: Data())
    @State private var exportName = ""
    #endif

    func body(content: Content) -> some View {
        #if os(macOS)
        content.onChange(of: model.fileAction) { _, action in
            guard let action else { return }
            model.fileAction = nil
            DispatchQueue.main.async { run(action) }
        }
        #else
        content
            .onChange(of: model.fileAction) { _, action in
                guard let action else { return }
                model.fileAction = nil
                present(action)
            }
            // .folder ALONE (not .directory, no file types next to it: iOS
            // then navigates into folders) gives the picker its Open button
            // inside a folder; one folder per pick.
            .fileImporter(isPresented: $importing, allowedContentTypes: importTypes,
                          allowsMultipleSelection: importAction == .importBanks) { result in
                guard case let .success(urls) = result else { return }
                if importAction == .loadPatch, let url = urls.first {
                    let scoped = url.startAccessingSecurityScopedResource()
                    model.loadFile(url.path)
                    if scoped { url.stopAccessingSecurityScopedResource() }
                } else if case let .exportBank(id) = importAction {
                    if let dir = urls.first { model.exportBank(id: id, to: dir) }
                } else {
                    model.importItems(urls)
                }
            }
            .fileExporter(isPresented: $exporting, document: exportDoc, contentType: .data,
                          defaultFilename: exportName) { _ in }
        #endif
    }

    #if os(macOS)
    private func run(_ action: FileAction) {
        switch action {
        case .importBanks, .importFolder:
            let panel = NSOpenPanel()
            panel.message = "Import: bank folders, zip banks or .sy1 patches — copied into the library"
            panel.canChooseDirectories = true
            panel.allowsMultipleSelection = true
            panel.allowedContentTypes = [.zip, sy1Type, .folder]
            if panel.runModal() == .OK { model.importItems(panel.urls) }
        case .loadPatch:
            let panel = NSOpenPanel()
            panel.allowedContentTypes = [sy1Type]
            if panel.runModal() == .OK, let url = panel.url { model.loadFile(url.path) }
        case .savePatch:
            let panel = NSSavePanel()
            panel.allowedContentTypes = [sy1Type]
            panel.directoryURL = URL(fileURLWithPath: model.host.libraryRoot)
            panel.nameFieldStringValue = (model.patchName.isEmpty ? "patch" : model.patchName) + ".sy1"
            if panel.runModal() == .OK, let url = panel.url {
                _ = model.saveFile(url.path)
                model.loadLibrary(rescan: true)
            }
        case .exportBank(let id):
            guard let bank = model.banks.first(where: { $0.id == id }) else { return }
            let panel = NSOpenPanel()
            panel.message = "Export bank “\(bank.name)”: a folder holding its patches is created in the folder you choose"
            panel.prompt = "Export"
            panel.canChooseFiles = false
            panel.canChooseDirectories = true
            panel.canCreateDirectories = true
            if panel.runModal() == .OK, let url = panel.url { model.exportBank(id: id, to: url) }
        }
    }
    #else
    private var importTypes: [UTType] {
        switch importAction {
        case .loadPatch: return [sy1Type, .data]
        case .importFolder, .exportBank: return [.folder]
        default: return [.zip, sy1Type, .data]
        }
    }

    private func present(_ action: FileAction) {
        switch action {
        case .importBanks, .importFolder, .loadPatch, .exportBank:
            importAction = action
            importing = true
        case .savePatch:
            exportDoc = DataDocument(data: Data(model.host.patchText.utf8))
            exportName = (model.patchName.isEmpty ? "patch" : model.patchName) + ".sy1"
            exporting = true
        }
    }
    #endif
}
