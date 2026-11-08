import SwiftUI

struct TodayView: View {
    @Environment(AppModel.self) private var model

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
