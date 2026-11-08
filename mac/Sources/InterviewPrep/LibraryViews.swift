import SwiftUI

// MARK: Decks

struct DecksView: View {
    @Environment(AppModel.self) private var model

    private let columns = [GridItem(.adaptive(minimum: 230), spacing: 14)]

    var body: some View {
        ScrollView {
            LazyVGrid(columns: columns, spacing: 14) {
                ForEach(model.facets?.decks ?? []) { deck in
                    DeckCard(deck: deck)
                }
            }
            .padding(20)
            .frame(maxWidth: 1040)
            .frame(maxWidth: .infinity)
        }
        .navigationTitle("Decks")
        .overlay {
            if model.facets?.decks.isEmpty ?? true {
                ContentUnavailableView("No decks in this build", systemImage: "rectangle.stack")
            }
        }
    }
}

private struct DeckCard: View {
    @Environment(AppModel.self) private var model
    let deck: DeckInfo

    var body: some View {
        VStack(alignment: .leading, spacing: 9) {
            Text(deck.title).font(.system(size: 16, weight: .bold))
            HStack(spacing: 10) {
                Text("\(deck.cards) cards")
                Text("\(deck.seen) seen")
                if deck.due > 0 { Text("\(deck.due) due") }
            }
            .font(Theme.mono)
            .foregroundStyle(Theme.ink3)
            Meter(value: deck.cards > 0 ? Double(deck.seen) / Double(deck.cards) : 0)
            HStack {
                Toggle("In Today", isOn: Binding(get: { !deck.off }, set: { setIncluded($0) }))
                    .toggleStyle(.switch)
                    .controlSize(.mini)
                    .font(.system(size: 12.5))
                    .foregroundStyle(Theme.ink2)
                Spacer()
                Button("Study") { model.study(deck: deck.slug) }
                    .buttonStyle(.borderedProminent)
                    .tint(Theme.ink)
            }
        }
        .sheetStyle(padding: 14)
        .opacity(deck.off ? 0.65 : 1)
    }

    private func setIncluded(_ included: Bool) {
        var off = Set((model.facets?.decks ?? []).filter(\.off).map(\.slug))
        if included { off.remove(deck.slug) } else { off.insert(deck.slug) }
        model.apply(settings: ["decksOff": Array(off)])
    }
}

// MARK: Library

struct LibraryView: View {
    @Environment(AppModel.self) private var model
    @State private var kind = "problem"
    @State private var status = ""
    @State private var pattern = ""
    @State private var mine = false
    @State private var search = ""
    @State private var limit = 100
    @State private var rows: [Card] = []
    @State private var total = 0

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 10) {
                Picker("Kind", selection: $kind) {
                    Text("Problems").tag("problem")
                    Text("Cards").tag("card")
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                .frame(width: 170)

                Toggle("My list", isOn: $mine).toggleStyle(.button)

                Picker("Status", selection: $status) {
                    Text("Any status").tag("")
                    Text("Due now").tag("due")
                    Text("New").tag("new")
                    Text("Red").tag("red")
                    Text("Yellow").tag("yellow")
                    Text("Blue").tag("blue")
                    Text("Gone").tag("retired")
                }
                .labelsHidden()
                .frame(maxWidth: 130)

                Picker("Pattern", selection: $pattern) {
                    Text("Any pattern").tag("")
                    ForEach(model.facets?.patterns ?? []) { Text($0.name).tag($0.slug) }
                }
                .labelsHidden()
                .frame(maxWidth: 210)
                Spacer()
            }
            .padding(.horizontal, 20)
            .padding(.vertical, 12)
            Divider()
            List {
                ForEach(rows) { card in
                    LibraryRow(card: card)
                        .listRowSeparatorTint(Theme.line2)
                }
                if rows.count < total {
                    Button("Show more") { limit = min(500, limit + 100) }
                        .frame(maxWidth: .infinity)
                }
            }
            .listStyle(.plain)
            .scrollContentBackground(.hidden)
            HStack {
                Text("\(rows.count) of \(total)").font(.system(size: 12.5)).foregroundStyle(Theme.ink3)
                Spacer()
            }
            .padding(.horizontal, 20)
            .padding(.vertical, 7)
        }
        .navigationTitle("Library")
        .searchable(text: $search, placement: .toolbar, prompt: "Title or number")
        .task(id: query) { reload() }
        .onChange(of: model.dataVersion) { reload() }
    }

    private var query: String { "\(kind)|\(status)|\(pattern)|\(mine)|\(search)|\(limit)" }

    private func reload() {
        var request: [String: Any] = ["cmd": "library", "kind": kind, "status": status, "q": search,
                                      "limit": limit, "curated": mine]
        if !pattern.isEmpty { request["pattern"] = pattern }
        if let reply = model.send(request, as: LibraryReply.self) {
            rows = reply.rows
            total = reply.total
        }
    }
}

private struct LibraryRow: View {
    @Environment(AppModel.self) private var model
    let card: Card

    private var due: (text: String, now: Bool) {
        switch card.track {
        case "new": return ("New", false)
        case "retired": return ("Gone", false)
        default:
            let days = card.dueIn ?? 0
            if days < 0 { return ("Due, waited \(-days)d", true) }
            if days == 0 { return ("Due", true) }
            return ("In \(days)d", false)
        }
    }

    var body: some View {
        HStack(spacing: 12) {
            TrackDot(track: card.track)
            Button { model.open(id: card.id) } label: {
                title.frame(maxWidth: .infinity, alignment: .leading).contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            if let level = card.level {
                Text(level.uppercased())
                    .font(Theme.label)
                    .foregroundStyle(Theme.ink3)
                    .frame(width: 58, alignment: .leading)
            }
            Text(due.text)
                .font(Theme.mono)
                .foregroundStyle(due.now ? Theme.redText : Theme.ink2)
                .frame(width: 112, alignment: .leading)
            HStack(spacing: 4) {
                ForEach(["b", "y", "r"], id: \.self) { grade in
                    miniButton(grade)
                }
                Group {
                    if card.doneToday != nil {
                        Button { model.quickUndo(id: card.id) } label: { Image(systemName: "arrow.uturn.backward") }
                            .buttonStyle(.plain)
                            .foregroundStyle(Theme.ink3)
                            .help("Undo today's grade")
                    } else {
                        Color.clear
                    }
                }
                .frame(width: 18)
            }
        }
        .padding(.vertical, 2)
    }

    @ViewBuilder
    private var title: some View {
        if let number = card.number {
            HStack(alignment: .firstTextBaseline, spacing: 0) {
                Text("\(number)").font(Theme.mono).foregroundColor(Theme.ink3).frame(width: 42, alignment: .leading)
                Text(card.title).fontWeight(.medium)
                if card.premium == true {
                    Text("Premium").font(Theme.label).foregroundStyle(Theme.ink3).padding(.leading, 8)
                }
            }
        } else {
            Text(styledText(card.title)).fontWeight(.medium).lineLimit(2)
        }
    }

    private func miniButton(_ grade: String) -> some View {
        let on = card.doneToday == grade
        return Button { model.quickGrade(id: card.id, grade: grade) } label: {
            Text(grade.uppercased())
                .font(.system(size: 12, weight: .semibold, design: .monospaced))
                .foregroundStyle(on ? Theme.ink : Theme.ink2)
                .frame(width: 27, height: 24)
                .background(on ? Theme.softColor(grade: grade) : Theme.sheet)
                .overlay(alignment: .bottom) { Rectangle().fill(Theme.color(grade: grade)).frame(height: 2) }
                .clipShape(RoundedRectangle(cornerRadius: 6))
                .overlay(RoundedRectangle(cornerRadius: 6).strokeBorder(on ? Theme.color(grade: grade) : Theme.line))
        }
        .buttonStyle(.plain)
        .accessibilityLabel("Grade \(grade.uppercased())")
    }
}

// MARK: Insights

struct InsightsView: View {
    @Environment(AppModel.self) private var model
    @State private var data: Insights?

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                if let data {
                    if data.graded == 0 {
                        Text("Grade a few problems. Your patterns show up here.")
                            .foregroundStyle(Theme.ink3)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .sheetStyle()
                    } else {
                        content(data)
                    }
                }
            }
            .padding(20)
            .frame(maxWidth: 1040)
            .frame(maxWidth: .infinity)
        }
        .navigationTitle("Insights")
        .task(id: model.dataVersion) { data = model.send(["cmd": "insights"], as: Insights.self) }
    }

    @ViewBuilder
    private func content(_ data: Insights) -> some View {
        if !data.notes.isEmpty {
            VStack(alignment: .leading, spacing: 7) {
                ForEach(data.notes) { note in
                    HStack(alignment: .firstTextBaseline, spacing: 10) {
                        RoundedRectangle(cornerRadius: 2)
                            .fill(note.kind == "memorize" || note.kind == "explain" ? Theme.red : Theme.ink3)
                            .frame(width: 8, height: 8)
                        Text(note.text).font(.system(size: 15))
                    }
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .sheetStyle()
        }

        panel("What your judged code shows most") {
            if data.needs.isEmpty {
                Text("Your coach fills this in after it reviews code you saved before running it.")
                    .foregroundStyle(Theme.ink3)
            } else {
                ForEach(data.needs) { need in
                    VStack(spacing: 3) {
                        HStack {
                            Text(need.label)
                            Spacer()
                            Text("\(Int((need.share * 100).rounded()))%").font(Theme.mono)
                        }
                        Meter(value: need.share, color: Theme.ink)
                    }
                }
            }
        }

        panel("Patterns, pass rate by kind of attempt") {
            Grid(alignment: .leading, horizontalSpacing: 18, verticalSpacing: 7) {
                GridRow {
                    ForEach(["Pattern", "New face", "Review", "Typed", "Cards", "Seen"], id: \.self) { title in
                        Text(title.uppercased()).font(Theme.label).foregroundStyle(Theme.ink3)
                    }
                }
                ForEach(data.patterns) { row in
                    Divider().overlay(Theme.line2)
                    GridRow {
                        HStack(spacing: 6) {
                            Button(row.name) { model.study(pattern: row.slug) }
                                .buttonStyle(.link)
                                .foregroundStyle(Theme.ink)
                                .fontWeight(.medium)
                                .help("Study this pattern's cards")
                            if row.memorizing {
                                Text("Memorizing")
                                    .font(.system(size: 10.5, weight: .medium, design: .monospaced))
                                    .foregroundStyle(Theme.redText)
                                    .padding(.horizontal, 5)
                                    .overlay(RoundedRectangle(cornerRadius: 4).strokeBorder(Theme.redText))
                            }
                        }
                        rate(row.cold)
                        rate(row.review)
                        rate(row.drill)
                        rate(row.flash)
                        Text("\(row.seen)/\(row.total)").font(Theme.mono).foregroundStyle(Theme.ink2)
                    }
                }
            }
        }
    }

    private func panel<Content: View>(_ title: String, @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 9) {
            Text(title.uppercased()).font(Theme.label).foregroundStyle(Theme.ink3)
            content()
        }
        .font(.system(size: 14))
        .frame(maxWidth: .infinity, alignment: .leading)
        .sheetStyle()
    }

    @ViewBuilder
    private func rate(_ evidence: Evidence) -> some View {
        if let value = evidence.rate {
            HStack(spacing: 7) {
                Meter(value: value).frame(width: 40)
                Text("\(Int((value * 100).rounded()))%").font(Theme.mono).foregroundStyle(Theme.ink2)
            }
            .help("From \(String(format: "%.1f", evidence.n)) weighted attempts")
        } else {
            Text("–").foregroundStyle(Theme.ink3)
        }
    }
}
