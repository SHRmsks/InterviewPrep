import Foundation

// What the core sends back. Field names match the JSON from core/engine.cpp.

struct Envelope: Decodable {
    let ok: Bool
    let error: String?
    let dirty: Bool?
}

struct Counts: Decodable {
    let new, red, yellow, blue, retired, due: Int
    var started: Int { red + yellow + blue + retired }
}

struct Lane: Decodable {
    let items: [Card]
    let done, total, overflow: Int
    let counts: Counts
}

struct Today: Decodable {
    let date: String
    let day: Int
    let code: Lane
    let cards: Lane
}

struct PatternRef: Decodable, Hashable {
    let slug, name: String
}

struct HistoryEntry: Decodable, Hashable {
    let date, grade: String
}

/// One check on a card's way out, and its gap, such as "10-20d".
struct RailStop: Decodable, Hashable {
    let track, gap: String
}

struct Card: Decodable, Identifiable, Hashable {
    let id, kind, title, deck, track: String
    let step, steps, attempts: Int
    let position, next: String
    var dueIn: Int?
    var doneToday: String?
    var rail: [RailStop]?
    var railAt: Int?

    // Problems
    var number: Int?
    var level: String?
    var premium: Bool?
    var url: String?

    // Queue position
    var slot: String?
    var why: String?

    // Full detail, from the "card" command
    var timebox: Int?
    var idea, pitfall, time, space: String?
    var qcode, answer, code, deckTitle: String?
    var patterns: [PatternRef]?
    var history: [HistoryEntry]?

    var isProblem: Bool { kind == "problem" }
    var isDrill: Bool { kind == "drill" }
}

struct CardReply: Decodable { let card: Card }
struct ItemsReply: Decodable { let items: [Card] }
struct MockReply: Decodable { let minutes: Int; let items: [Card] }
struct LibraryReply: Decodable { let total: Int; let rows: [Card] }

struct GradeReply: Decodable {
    let before, after, next: String
}

struct AttemptReply: Decodable {
    let stem, ext, note: String
    let block: String   // what to append to the day's file; empty when nothing is new
}

struct DiffOp: Decodable, Hashable {
    let s: Int
    let t: String
}

struct DiffReply: Decodable {
    let score: Double
    let suggest: String
    let ops: [DiffOp]
}

struct Rules: Decodable {
    let judgementsFile, oldJudgementsFile, lastReviewFile: String
}

struct OverviewReply: Decodable {
    let pending, batchSize: Int

    enum CodingKeys: String, CodingKey {
        case pending
        case batchSize = "batch_size"
    }
}

struct DeckInfo: Decodable, Identifiable {
    let slug, title: String
    let cards, seen, retired, due: Int
    let off: Bool
    var id: String { slug }
}

struct PatternInfo: Decodable, Identifiable {
    let slug, name: String
    let algo: Bool
    var id: String { slug }
}

struct CompanyInfo: Decodable, Identifiable {
    let slug, name: String
    var id: String { slug }
}

struct Facets: Decodable {
    let patterns: [PatternInfo]
    let decks: [DeckInfo]
    let companies: [CompanyInfo]
}

struct Evidence: Decodable {
    let n: Double
    let rate: Double?
}

struct PatternRow: Decodable, Identifiable {
    let slug, name: String
    let attempts, seen, total: Int
    let mastery: Double
    let cold, review, drill, flash: Evidence
    let memorizing: Bool
    var id: String { slug }
}

/// One thing your judged code shows: a flaw, or the thinking side, and its share of all the signs.
struct NeedRow: Decodable, Identifiable {
    let slug, label: String
    let share: Double
    var count: Int?
    var id: String { slug }
}

struct NoteRow: Decodable, Identifiable {
    let kind, text: String
    var id: String { text }
}

struct Insights: Decodable {
    let patterns: [PatternRow]
    let needs: [NeedRow]
    let notes: [NoteRow]
    let graded: Int
}

struct AppSettings: Decodable {
    var codeCap, codeNew, codeReviews, cardCap, cardNew: Int
    var clock, company: String
    var premium: Bool
    var decksOff: [String]
}

struct SettingsReply: Decodable { let settings: AppSettings }
