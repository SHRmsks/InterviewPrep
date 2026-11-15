import AppKit
import Foundation
import Observation

enum AppSection: String, CaseIterable, Identifiable {
    case today = "Today"
    case decks = "Decks"
    case library = "Library"
    case insights = "Insights"

    var id: String { rawValue }

    var symbol: String {
        switch self {
        case .today: return "sun.max"
        case .decks: return "rectangle.stack"
        case .library: return "books.vertical"
        case .insights: return "chart.bar"
        }
    }
}

/// One run through a list of cards.
@Observable
final class StudySession {
    enum Phase { case front, back, finished }

    var items: [Card]
    var index = 0
    var phase: Phase = .front
    var card: Card?
    var typed = ""
    var diff: DiffReply?
    var started = Date()
    var grade: String?
    var moved: String?
    var undoable: String?
    var hinted = false
    var attemptNote: String?   // what the last paste saved on this card
    var pastedCode: String?
    let isMock: Bool
    let mockEnds: Date?

    init(items: [Card], mockMinutes: Int? = nil) {
        self.items = items
        isMock = mockMinutes != nil
        mockEnds = mockMinutes.map { Date().addingTimeInterval(Double($0) * 60) }
    }
}

/// Everything that can change what the core would answer. Each one goes through
/// `AppModel.note`, and one reload follows. No button or view reloads by itself.
enum Change {
    case progress   // a command changed your progress, and the core's reply said so
    case disk       // a file in the data folder may have changed: the coach, or the command-line tool
    case day        // the calendar day rolled over, so today's queue is a new one
}

@Observable
final class AppModel {
    var section: AppSection = .today
    var today: Today?
    var facets: Facets?
    var rules: Rules?
    var session: StudySession?
    var errorText: String?
    var dataVersion = 0        // bumped by every reload, so views that ask the core themselves ask again

    var reviewWaiting = 0      // graded problems with no judgement yet
    var reviewBatch = 10
    var reviewRunning = false
    var hasLastReview = false
    var reviewText: String?    // shown on Today while set
    var reviewInClaude = false // the review opened as a chat, waiting for you to send it

    /// Swapped by the debug driver, so a test run leaves your clipboard alone.
    @ObservationIgnored var clipboard = NSPasteboard.general
    /// Swapped by the debug driver, so a test run opens nothing in the Claude app.
    @ObservationIgnored var openLink: (URL) -> Bool = { NSWorkspace.shared.open($0) }
    @ObservationIgnored private var lastSaved = ""
    @ObservationIgnored private var judgements: (log: String, old: String)?   // as the core last got them
    @ObservationIgnored private var saveBlocked = false
    @ObservationIgnored private let decoder = JSONDecoder()
    @ObservationIgnored private var pending: Set<Change> = []
    @ObservationIgnored private var reloadQueued = false
    @ObservationIgnored private var reloading = false
    @ObservationIgnored private var watcher: FolderWatcher?

    init() {
        load()
        // The coach and the command-line tool write to the same folder, and the
        // watcher sees it as it happens. Coming back to the app looks once more,
        // in case the system dropped an event.
        watcher = FolderWatcher(folder: Storage.directory) { [weak self] in self?.note(.disk) }
        let center = NotificationCenter.default
        center.addObserver(forName: NSApplication.didBecomeActiveNotification, object: nil, queue: .main) { [weak self] _ in
            self?.note(.disk)
        }
        center.addObserver(forName: .NSCalendarDayChanged, object: nil, queue: .main) { [weak self] _ in
            self?.note(.day)
        }
    }

    // MARK: Changes

    /// The one way in for anything that can change what the views show. However
    /// many arrive in one turn of the run loop, one reload follows them.
    func note(_ change: Change) {
        guard !reloading else { return }   // a reload's own commands are not news
        pending.insert(change)
        guard !reloadQueued else { return }
        reloadQueued = true
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            reloadQueued = false
            let changes = pending
            pending = []
            reload(changes)
        }
    }

    /// Brings everything the views show back in step with the core: the queue,
    /// the filters, what waits for the coach, and the version the Today panel,
    /// Insights and the Library ask again on.
    private func reload(_ changes: Set<Change>) {
        reloading = true
        defer { reloading = false }
        let news = takeInFromDisk()
        // The app's own saves reach the watcher too, and those change nothing here.
        guard news || changes != [.disk] else { return }
        today = send(["cmd": "today"], as: Today.self)
        facets = send(["cmd": "facets"], as: Facets.self)
        countWaitingForCoach()
        dataVersion += 1
    }

    // MARK: Loading and saving

    private func load() {
        guard let url = Storage.resourcesFile, let resources = try? String(contentsOf: url, encoding: .utf8) else {
            errorText = "The resources are missing from the app."
            return
        }
        if Core.loadResources(resources) < 0 {
            errorText = "The resources could not be read: \(Core.lastError())"
            return
        }
        if Storage.hasState {
            if let state = Storage.read(), Core.loadState(state) {
                lastSaved = state
                Storage.backUp()
            } else {
                recoverUnreadableState()
            }
        }
        rules = send(["cmd": "rules"], as: Rules.self)
        reload([.progress])   // at once, not queued, so the first frame has its data
    }

    /// A file the app can't read may still be the only copy of your history, so it
    /// is never saved over. It's kept under a new name and the last good backup
    /// takes its place. If it can't even be moved, nothing gets saved at all.
    private func recoverUnreadableState() {
        guard let kept = Storage.setAsideState() else {
            saveBlocked = true
            errorText = "Your progress file can't be read or moved, so nothing will be saved until "
                + "that's fixed. It's at \(Storage.stateFile.path)."
            return
        }
        if let backup = Storage.readBackup(), Core.loadState(backup) {
            lastSaved = backup
            try? Storage.write(backup)
            errorText = "Your progress file couldn't be read, so it was kept as \(kept). The app went "
                + "back to the backup from when it last opened, so newer grades may be missing."
        } else {
            errorText = "Your progress file couldn't be read, so it was kept as \(kept). There's no "
                + "backup to go back to, so the app started empty."
        }
    }

    /// Takes in what others left in the data folder: progress the command-line
    /// tool changed, and the coach's labels. True when either was news.
    private func takeInFromDisk() -> Bool {
        var news = false
        // A progress file that blocked saving can't be read, so it isn't tried again.
        if !saveBlocked, let text = Storage.read(), text != lastSaved, Core.loadState(text) {
            lastSaved = text
            news = true
        }
        if loadJudgements() { news = true }
        return news
    }

    private func persist() {
        // Asking for the state also tells the core its change was taken, so each
        // change is reported once, even while nothing can be written.
        let text = Core.saveState()
        guard !saveBlocked else { return }
        do {
            try Storage.write(text)
            lastSaved = text
        } catch {
            errorText = "Couldn't save your progress: \(error.localizedDescription)"
        }
    }

    // MARK: Talking to the core

    @discardableResult
    func send<T: Decodable>(_ request: [String: Any], as type: T.Type) -> T? {
        var full = request
        full["now"] = nowString()
        let data = Core.call(full)
        guard let envelope = try? decoder.decode(Envelope.self, from: data) else {
            errorText = "The core sent back something unreadable."
            return nil
        }
        if envelope.dirty == true {
            persist()
            note(.progress)
        }
        guard envelope.ok else {
            errorText = envelope.error ?? "That didn't work."
            return nil
        }
        do {
            return try decoder.decode(T.self, from: data)
        } catch {
            errorText = "Couldn't read the core's reply: \(error)"
            return nil
        }
    }

    // MARK: Coach

    /// The coach's labels count toward your patterns and flaws, and its stricter
    /// grade can keep a check going, so the core needs them before it builds a
    /// queue. A review can add to them at any time. The log from before
    /// 2026-11-09 is only ever read. True when the core got new ones.
    private func loadJudgements() -> Bool {
        guard let rules else { return false }
        let log = Storage.text(at: rules.judgementsFile) ?? ""
        let old = Storage.text(at: rules.oldJudgementsFile) ?? ""
        if let judgements, judgements.log == log, judgements.old == old { return false }
        judgements = (log, old)
        send(["cmd": "judgements", "text": log, "old": old], as: Envelope.self)
        return true
    }

    /// Counts what is waiting. This asks the core, not a model, so it costs nothing.
    private func countWaitingForCoach() {
        guard let rules, let judgements else { return }
        let request: [String: Any] = ["cmd": "overview", "log": judgements.log, "old_log": judgements.old]
        guard let overview = send(request, as: OverviewReply.self) else { return }
        if overview.pending != reviewWaiting { reviewInClaude = false }
        reviewWaiting = overview.pending
        reviewBatch = overview.batchSize
        hasLastReview = Storage.text(at: rules.lastReviewFile) != nil
    }

    func startReview() {
        guard !reviewRunning, reviewWaiting > 0 else { return }
        reviewRunning = true
        reviewInClaude = false
        Task { @MainActor in
            do {
                let text = try await Reviewer.run()
                if let rules, (try? Storage.save(text, at: rules.lastReviewFile)) != nil { hasLastReview = true }
                reviewText = text
            } catch is Reviewer.SignInNeeded {
                reviewInClaude = Reviewer.chatLink.map(openLink) ?? false
                if !reviewInClaude {
                    errorText = "Claude Code can't sign in, and the Claude app wouldn't open. "
                        + "Your problems are still waiting."
                }
            } catch {
                errorText = "\(error.localizedDescription) Your problems are still waiting."
            }
            reviewRunning = false
            note(.disk)   // whatever it judged is in the folder by now
        }
    }

    func openLastReview() {
        reviewText = rules.flatMap { Storage.text(at: $0.lastReviewFile) }
    }

    var leftToday: Int {
        guard let today else { return 0 }
        return (today.code.items + today.cards.items).filter { $0.doneToday == nil }.count
    }

    // MARK: Queue

    func start(lane: String) {
        guard let today else { return }
        begin(lane == "code" ? today.code.items : today.cards.items)
    }

    func pullMore(lane: String) {
        send(["cmd": "more", "lane": lane], as: Envelope.self)
    }

    func startMock() {
        let roll = Int(Date().timeIntervalSince1970) % 100_000
        guard let mock = send(["cmd": "mock", "roll": roll], as: MockReply.self), !mock.items.isEmpty else { return }
        let session = StudySession(items: mock.items, mockMinutes: mock.minutes)
        self.session = session
        openCurrent()
    }

    func study(deck: String) {
        guard let reply = send(["cmd": "session", "deck": deck, "n": 20], as: ItemsReply.self) else { return }
        begin(reply.items)
    }

    func study(pattern: String) {
        guard let reply = send(["cmd": "session", "pattern": pattern, "n": 20], as: ItemsReply.self) else { return }
        begin(reply.items)
    }

    func open(id: String) {
        guard let reply = send(["cmd": "card", "id": id], as: CardReply.self) else { return }
        begin([reply.card])
    }

    private func begin(_ items: [Card]) {
        let pending = items.filter { $0.doneToday == nil }
        let list = pending.isEmpty ? items : pending
        guard !list.isEmpty else { return }
        session = StudySession(items: list)
        openCurrent()
    }

    func exitSession() {
        session = nil
    }

    // MARK: Study flow

    private func openCurrent() {
        guard let s = session else { return }
        let item = s.items[s.index]
        guard var card = send(["cmd": "card", "id": item.id], as: CardReply.self)?.card else { return }
        card.slot = item.slot
        card.why = item.why
        s.card = card
        s.phase = .front
        s.typed = ""
        s.diff = nil
        s.grade = nil
        s.hinted = false
        s.attemptNote = nil
        s.pastedCode = nil
        s.started = Date()
    }

    /// Saves what is on the clipboard as your code for the open problem. Only
    /// before the notes are shown, so the file is what you wrote on your own.
    func pasteAttempt() {
        guard let s = session, s.phase == .front, let card = s.card, card.isProblem else { return }
        let code = clipboard.string(forType: .string) ?? ""
        if code == s.pastedCode { return }  // a double press saves once
        let request: [String: Any] = ["cmd": "attempt", "id": card.id, "code": code]
        guard var reply = send(request, as: AttemptReply.self) else { return }
        do {
            // The day's file as it stands, so the core can number this paste. The paste is appended.
            if let existing = try Storage.existingText(at: reply.stem + reply.ext) {
                var again = request
                again["existing"] = existing
                guard let numbered = send(again, as: AttemptReply.self) else { return }
                reply = numbered
            }
            if !reply.block.isEmpty { try Storage.append(reply.block, at: reply.stem + reply.ext) }
            s.pastedCode = code
            s.attemptNote = reply.note
        } catch {
            errorText = "Couldn't save your attempt: \(error.localizedDescription)"
        }
    }

    func reveal() {
        guard let s = session, s.phase == .front, let card = s.card else { return }
        if card.isDrill {
            s.diff = send(["cmd": "diff", "typed": s.typed, "reference": card.code ?? ""], as: DiffReply.self)
        }
        s.phase = .back
    }

    func pick(grade: String) {
        guard let s = session, s.phase == .back else { return }
        s.grade = grade
        commit()
    }

    func commit() {
        guard let s = session, let card = s.card, let grade = s.grade else { return }
        var request: [String: Any] = ["cmd": "grade", "id": card.id, "grade": grade,
                                      "hinted": s.hinted, "mock": s.isMock]
        if card.isProblem { request["secs"] = Int(Date().timeIntervalSince(s.started)) }
        guard let reply = send(request, as: GradeReply.self) else { return }
        s.moved = "\(reply.after). \(reply.next)."
        s.undoable = card.id
        if s.index + 1 >= s.items.count {
            s.phase = .finished
        } else {
            s.index += 1
            openCurrent()
        }
    }

    func undoLast() {
        guard let s = session, let id = s.undoable else { return }
        guard send(["cmd": "undo", "id": id], as: Envelope.self) != nil else { return }
        s.undoable = nil
        s.moved = nil
        if s.phase != .finished { s.index = max(0, s.index - 1) }
        openCurrent()
    }

    func hideCurrent() {
        guard let s = session, let card = s.card else { return }
        send(["cmd": "hide", "id": card.id], as: Envelope.self)
        s.items.remove(at: s.index)
        if s.index >= s.items.count {
            exitSession()
        } else {
            openCurrent()
        }
    }

    // MARK: Settings

    func quickGrade(id: String, grade: String) {
        send(["cmd": "grade", "id": id, "grade": grade], as: Envelope.self)
    }

    func quickUndo(id: String) {
        send(["cmd": "undo", "id": id], as: Envelope.self)
    }

    func settings() -> AppSettings? {
        send(["cmd": "settings"], as: SettingsReply.self)?.settings
    }

    func apply(settings changes: [String: Any]) {
        send(["cmd": "settings", "set": changes], as: SettingsReply.self)
    }

    func exportState() -> String { Core.saveState() }

    func importState(_ text: String) -> Bool {
        guard Core.loadState(text) else {
            errorText = "That file isn't saved progress: \(Core.lastError())"
            return false
        }
        persist()
        note(.progress)   // loaded straight into the core, so no reply reported it
        return true
    }
}
