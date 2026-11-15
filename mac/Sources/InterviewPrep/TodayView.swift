import SwiftUI

struct TodayView: View {
    @Environment(AppModel.self) private var model
    @State private var standing: Standing?

    /// Whether the panel under the progress bars lists every pattern. Remembered between launches.
    static let showsEveryPatternKey = "todayShowsEveryPattern"

    var body: some View {
        ScrollView {
            if let today = model.today {
                VStack(alignment: .leading, spacing: 16) {
                    HStack(spacing: 0) {
                        Progress(title: "Problems", counts: today.code.counts)
                        Divider()
                        Progress(title: "Cards", counts: today.cards.counts)
                    }
                    .background(Theme.sheet)
                    .clipShape(RoundedRectangle(cornerRadius: 10))
                    .overlay(RoundedRectangle(cornerRadius: 10).strokeBorder(Theme.line))

                    if let standing { StandingPanel(standing: standing) }

                    if model.reviewWaiting > 0 || model.reviewRunning || model.hasLastReview {
                        ReviewBar()
                    }

                    HStack(alignment: .top, spacing: 16) {
                        LaneView(title: "Problems", key: "code", lane: today.code)
                        LaneView(title: "Cards", key: "cards", lane: today.cards)
                    }

                    HStack(spacing: 14) {
                        Text("Mock interview").font(.system(size: 16, weight: .bold))
                        Text("Two problems you have never seen, 45 minutes, no labels.")
                            .foregroundStyle(Theme.ink2)
                        Spacer()
                        Button("Start a mock") { model.startMock() }
                    }
                    .sheetStyle()
                }
                .padding(20)
                .frame(maxWidth: 1040)
                .frame(maxWidth: .infinity)
            }
        }
        .navigationTitle("Today")
        .task(id: model.dataVersion) { standing = model.send(["cmd": "standing"], as: Standing.self) }
        // Here rather than on the root view: a review that finishes while you're
        // working on a problem waits until you're back, instead of interrupting.
        .sheet(isPresented: Binding(
            get: { model.reviewText != nil },
            set: { if !$0 { model.reviewText = nil } }
        )) {
            ReviewSheet(text: model.reviewText ?? "")
        }
    }
}

private struct Progress: View {
    let title: String
    let counts: Counts

    private var segments: [(String, String, Int)] {
        [("retired", "Gone", counts.retired), ("blue", "Blue", counts.blue),
         ("yellow", "Yellow", counts.yellow), ("red", "Red", counts.red)]
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("\(title), \(counts.started) started of \(counts.started + counts.new)".uppercased())
                .font(Theme.label)
                .foregroundStyle(Theme.ink3)
            bar
            HStack(spacing: 14) {
                ForEach(segments, id: \.0) { segment in
                    HStack(spacing: 6) {
                        TrackDot(track: segment.0)
                        Text(segment.1).foregroundStyle(Theme.ink2)
                        Text("\(segment.2)").font(Theme.mono)
                    }
                    .font(.system(size: 12.5))
                }
            }
        }
        .padding(16)
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private var bar: some View {
        GeometryReader { geometry in
            let total = max(1, counts.started)
            HStack(spacing: 2) {
                ForEach(segments.filter { $0.2 > 0 }, id: \.0) { segment in
                    Rectangle()
                        .fill(Theme.color(track: segment.0).opacity(segment.0 == "retired" ? 0.5 : 1))
                        .frame(width: max(3, geometry.size.width * CGFloat(segment.2) / CGFloat(total) - 2))
                }
            }
        }
        .frame(height: 12)
        .background(Theme.line2)
        .clipShape(Capsule())
    }
}

/// Your two ratings, and where new problems come from next. Every number and
/// sentence is the core's ("standing"): the shares are the picker's own, with
/// nothing held, so they say how often a pattern comes up and not which
/// patterns are waiting in the queue below.
private struct StandingPanel: View {
    let standing: Standing
    @AppStorage(TodayView.showsEveryPatternKey) private var showsAll = false

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(alignment: .top, spacing: 0) {
                ratings
                Divider()
                comesNext
            }
            if showsAll && !standing.pull.isEmpty {
                Divider()
                table
            }
        }
        .background(Theme.sheet)
        .clipShape(RoundedRectangle(cornerRadius: 10))
        .overlay(RoundedRectangle(cornerRadius: 10).strokeBorder(Theme.line))
    }

    private var ratings: some View {
        let rating = standing.rating
        return VStack(alignment: .leading, spacing: 10) {
            heading(rating.tries == 0 ? "Your ratings" : "Your ratings, from \(rating.tries) first tries")
            if rating.tries == 0 {
                Text("Grade a new problem and your ratings show up here.").foregroundStyle(Theme.ink3)
            } else {
                HStack(alignment: .top, spacing: 30) {
                    figure("\(rating.you)", "±\(rating.spread)", "For the idea")
                    figure("\(rating.code)", nil, "For working code")
                }
                Text("New problems aim \(rating.aimAbove) above your idea rating on their pattern. "
                    + "A pattern whose working code sits below \(rating.code) comes up more.")
                    .font(.system(size: 12.5))
                    .foregroundStyle(Theme.ink2)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .padding(16)
        .frame(width: 330, alignment: .leading)
    }

    /// The short version: two lines, each a share of the new problems and where it goes.
    private var comesNext: some View {
        let ground = standing.newGround
        let tried = !standing.pull.isEmpty
        return VStack(alignment: .leading, spacing: 8) {
            HStack(alignment: .firstTextBaseline) {
                heading("Where new problems come from")
                Spacer()
                if tried {
                    Button(showsAll ? "Hide details" : "Details") { showsAll.toggle() }
                        .buttonStyle(.link)
                        .font(.system(size: 12.5))
                }
            }
            // Until a pattern has been tried there is nothing to pull from, so every new problem is new ground.
            line(tried ? "\(percent(ground.share)) go to \(ground.goesTo)." : "Every new problem goes to a new pattern for now.",
                 ground.left > 0 ? "\(ground.left) left, most asked first: " + firstFew(ground.patterns.map(\.name)) : nil)
            if tried {
                line("\(percent(1 - ground.share)) come by pull from the patterns you have tried.",
                     "Highest now: " + firstFew(standing.pull.map { "\($0.name) \(percent($0.share))" }))
            }
            if standing.waiting > 0 && standing.waiting >= standing.newADay {
                Text("\(standing.waiting) new problems are waiting in your queue. Fresh ones come once fewer than "
                    + "\(standing.newADay) wait, or when you pull more.")
                    .foregroundStyle(Theme.ink3)
            }
        }
        .font(.system(size: 13))
        .fixedSize(horizontal: false, vertical: true)
        .padding(16)
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    /// Every pattern you have tried, and the two rules in full.
    private var table: some View {
        let most = max(standing.pull.map(\.share).max() ?? 0, 0.0001)
        return VStack(alignment: .leading, spacing: 12) {
            Grid(alignment: .leading, horizontalSpacing: 18, verticalSpacing: 7) {
                GridRow {
                    ForEach(["Pattern", "Share of the pull", "Idea", "Code", "Weak", "Recent"], id: \.self) { title in
                        Text(title.uppercased()).font(Theme.label).foregroundStyle(Theme.ink3)
                    }
                }
                ForEach(standing.pull) { row in
                    Divider().overlay(Theme.line2)
                    GridRow {
                        // The picker's own sentence for this pattern shows when the pointer rests on its name.
                        Text(row.name).fontWeight(.medium).lineLimit(1).help(row.why)
                        HStack(spacing: 7) {
                            Meter(value: row.share / most).frame(width: 110)
                            /*
                            Kept at its own width: the grid measures a row before it settles
                            the columns, and "13%" squeezed there wraps to three lines and
                            leaves the row that tall.
                            */
                            Text(percent(row.share)).font(Theme.mono).foregroundStyle(Theme.ink2).fixedSize()
                        }
                        // String(...) because a number put straight into Text gets a thousands comma.
                        Text(String(row.idea)).font(Theme.mono).foregroundStyle(Theme.ink2)
                        Text(String(row.code)).font(Theme.mono).foregroundStyle(Theme.ink2)
                        Text(times(row.weak)).font(Theme.mono)
                            .foregroundStyle(row.weak > 1 ? Theme.redText : Theme.ink3)
                        Text(row.recent < 1 ? times(row.recent) : "–").font(Theme.mono).foregroundStyle(Theme.ink3)
                    }
                }
            }
            VStack(alignment: .leading, spacing: 5) {
                Text(standing.newGround.rule)
                Text(standing.pullRule)
            }
            .font(.system(size: 12.5))
            .foregroundStyle(Theme.ink3)
            .fixedSize(horizontal: false, vertical: true)
        }
        .font(.system(size: 14))
        .padding(16)
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    /// A sentence, then what it leads to in a quieter color.
    private func line(_ lead: String, _ rest: String?) -> Text {
        guard let rest else { return Text(lead) }
        return Text(lead + " ") + Text(rest).foregroundStyle(Theme.ink2)
    }

    private func heading(_ text: String) -> some View {
        Text(text.uppercased()).font(Theme.label).foregroundStyle(Theme.ink3)
    }

    /// A rating in large digits, what it may be off by, and what it is for.
    private func figure(_ value: String, _ margin: String?, _ caption: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack(alignment: .firstTextBaseline, spacing: 5) {
                Text(value).font(.system(size: 26, weight: .bold)).monospacedDigit()
                if let margin { Text(margin).font(Theme.mono).foregroundStyle(Theme.ink3) }
            }
            Text(caption).font(.system(size: 12.5)).foregroundStyle(Theme.ink2)
        }
    }

    /// The first three of a list. A dot between them, because names such as "DP, linear" have commas of their own.
    private func firstFew(_ all: [String]) -> String { all.prefix(3).joined(separator: " · ") }

    private func percent(_ share: Double) -> String {
        share > 0 && share < 0.005 ? "<1%" : "\(Int((share * 100).rounded()))%"
    }

    /// "2.2×", or "0.25×" when one decimal would round a small factor away.
    private func times(_ factor: Double) -> String {
        String(format: factor < 0.45 ? "%.2f×" : "%.1f×", factor)
    }
}

private struct LaneView: View {
    @Environment(AppModel.self) private var model
    let title: String
    let key: String
    let lane: Lane

    private var left: Int { lane.items.filter { $0.doneToday == nil }.count }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 10) {
                Text(title).font(.system(size: 18, weight: .bold))
                Text("\(lane.done)/\(lane.total)").font(Theme.mono).foregroundStyle(Theme.ink3)
                Spacer()
                Button(left > 0 ? "Start \(left)" : "Done") { model.start(lane: key) }
                    .buttonStyle(.borderedProminent)
                    .tint(Theme.ink)
                    .disabled(left == 0)
                    .keyboardShortcut(key == "code" ? "p" : "k", modifiers: .command)
            }
            if lane.items.isEmpty {
                Text("Nothing queued.").foregroundStyle(Theme.ink3)
            } else {
                VStack(spacing: 0) {
                    ForEach(lane.items) { item in
                        if item.id != lane.items.first?.id { Divider().overlay(Theme.line2) }
                        QueueRow(item: item)
                    }
                }
            }
            HStack {
                if lane.overflow > 0 {
                    Text("\(lane.overflow) rolled to tomorrow")
                        .font(.system(size: 12.5))
                        .foregroundStyle(Theme.ink3)
                }
                Spacer()
                Button("Pull more") { model.pullMore(lane: key) }
            }
        }
        .sheetStyle()
        .frame(maxWidth: .infinity, alignment: .topLeading)
    }
}

private struct QueueRow: View {
    @Environment(AppModel.self) private var model
    let item: Card

    var body: some View {
        Button {
            model.open(id: item.id)
        } label: {
            HStack(alignment: .firstTextBaseline, spacing: 10) {
                TrackDot(track: item.track).alignmentGuide(.firstTextBaseline) { $0[.bottom] - 1 }
                VStack(alignment: .leading, spacing: 3) {
                    title
                    HStack(spacing: 10) {
                        if let level = item.level {
                            Text(level)
                                .font(Theme.label)
                                .foregroundStyle(level == "Hard" ? Theme.ink : Theme.ink3)
                        }
                        if item.steps > 0 { Pips(card: item) }
                        if item.slot != "review" { SlotBadge(slot: item.slot) }
                    }
                }
                Spacer(minLength: 8)
                if let done = item.doneToday {
                    Text(done.uppercased()).font(Theme.mono).foregroundStyle(Theme.ink3)
                }
            }
            .padding(.vertical, 7)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .opacity(item.doneToday == nil ? 1 : 0.55)
    }

    @ViewBuilder
    private var title: some View {
        if let number = item.number {
            HStack(alignment: .firstTextBaseline, spacing: 0) {
                Text("\(number)").font(Theme.mono).foregroundColor(Theme.ink3).frame(width: 42, alignment: .leading)
                Text(item.title).fontWeight(.medium).multilineTextAlignment(.leading)
            }
        } else {
            Text(styledText(item.title)).fontWeight(.medium).multilineTextAlignment(.leading).lineLimit(3)
        }
    }
}
