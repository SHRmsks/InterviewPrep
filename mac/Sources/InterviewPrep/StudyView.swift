import SwiftUI

struct StudyView: View {
    @Environment(AppModel.self) private var model
    @Bindable var session: StudySession

    var body: some View {
        VStack(spacing: 14) {
            topBar
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    switch session.phase {
                    case .finished: finished
                    case .front, .back: if let card = session.card { cardBody(card) }
                    }
                }
                .frame(maxWidth: 760)
                .frame(maxWidth: .infinity)
            }
        }
        .padding(20)
        .navigationTitle(session.isMock ? "Mock interview" : "Study")
    }

    // MARK: Top bar

    private var topBar: some View {
        HStack(spacing: 12) {
            Text("\(min(session.index + 1, session.items.count))/\(session.items.count)")
                .font(Theme.mono)
                .foregroundStyle(Theme.ink3)
            Meter(value: Double(session.index) / Double(max(1, session.items.count)))
            if session.phase != .finished, let card = session.card, card.isProblem || session.isMock {
                StudyClock(session: session, card: card)
            }
            Button(session.isMock ? "End mock" : "Exit") { model.exitSession() }
                .keyboardShortcut(.cancelAction)
        }
        .frame(maxWidth: 760)
        .frame(maxWidth: .infinity)
    }

    // MARK: Card

    @ViewBuilder
    private func cardBody(_ card: Card) -> some View {
        if !session.isMock, card.steps > 0, !(card.rail ?? []).isEmpty {
            Rail(card: card).padding(.leading, 2)
        }
        if let moved = session.moved {
            HStack(spacing: 10) {
                Text(moved).foregroundStyle(Theme.ink2)
                if session.undoable != nil {
                    Button("Undo") { model.undoLast() }
                        .buttonStyle(.link)
                        .keyboardShortcut("z", modifiers: .command)
                }
            }
            .font(.system(size: 13))
        }

        VStack(alignment: .leading, spacing: 14) {
            front(card)
            if session.phase == .back {
                Divider()
                back(card)
                    .transition(.asymmetric(insertion: .opacity.combined(with: .offset(y: -6)), removal: .opacity))
            }
        }
        .frame(maxWidth: .infinity, minHeight: 240, alignment: .topLeading)
        .sheetStyle(padding: 24)

        if session.phase == .front {
            if card.isProblem { attemptRow }
            revealButton(card)
        } else {
            gradeButtons
        }
    }

    private var attemptRow: some View {
        HStack(spacing: 10) {
            Button("Paste attempt") { model.pasteAttempt() }
                .keyboardShortcut("v", modifiers: .command)
            Text(session.attemptNote ?? "⌘V, before you run your code")
                .font(Theme.label)
                .foregroundStyle(session.attemptNote == nil ? Theme.ink3 : Theme.ink2)
        }
    }

    @ViewBuilder
    private func front(_ card: Card) -> some View {
        if card.isProblem {
            if !session.isMock {
                HStack(spacing: 10) {
                    SlotBadge(slot: card.slot)
                    Text(card.level ?? "")
                    if let box = card.timebox { Text("\(box) min box") }
                    if card.premium == true { Text("Premium") }
                }
                .font(Theme.mono)
                .foregroundStyle(Theme.ink3)
            }
            (Text("\(card.number ?? 0)  ").font(.system(size: 15, design: .monospaced)).foregroundColor(Theme.ink3)
                + Text(card.title).font(.system(size: 24, weight: .semibold)))
                .textSelection(.enabled)
            if let url = card.url.flatMap(URL.init(string:)) {
                Link("Open on LeetCode ↗", destination: url).fontWeight(.semibold)
            }
            if session.phase == .front && !session.isMock {
                if session.hinted {
                    Text((card.patterns ?? []).map(\.name).joined(separator: ", "))
                        .font(.system(size: 13))
                        .foregroundStyle(Theme.ink2)
                } else {
                    Button("Show the pattern (counts as a hint)") { session.hinted = true }
                        .buttonStyle(.link)
                        .font(.system(size: 13))
                }
            }
        } else {
            HStack(spacing: 10) {
                Text(card.deckTitle ?? card.deck)
                SlotBadge(slot: card.slot)
            }
            .font(Theme.mono)
            .foregroundStyle(Theme.ink3)
            Text(styledText(card.title, codeSize: 19))
                .font(.system(size: 21, weight: .semibold))
                .textSelection(.enabled)
            if let code = card.qcode, !code.isEmpty { CodeBlock(code: code) }
            if card.isDrill {
                CodeEditor(text: $session.typed)
                    .frame(height: session.phase == .back ? 120 : 260)
                    .background(Theme.paper)
                    .clipShape(RoundedRectangle(cornerRadius: 8))
                    .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(Theme.line))
                    .disabled(session.phase == .back)
            }
        }
    }

    @ViewBuilder
    private func back(_ card: Card) -> some View {
        VStack(alignment: .leading, spacing: 12) {
            if let patterns = card.patterns, !patterns.isEmpty {
                FlowLayout {
                    ForEach(patterns, id: \.self) { pattern in
                        Text(pattern.name)
                            .font(.system(size: 12.5))
                            .foregroundStyle(Theme.ink2)
                            .padding(.horizontal, 9)
                            .padding(.vertical, 2)
                            .overlay(RoundedRectangle(cornerRadius: 10).strokeBorder(Theme.line))
                    }
                }
            }
            if card.isProblem {
                labeled("Key idea", card.idea)
                labeled("Watch out for", card.pitfall)
                if let time = card.time, !time.isEmpty {
                    Text("Time \(time) · Space \(card.space ?? "")").font(.system(size: 13, design: .monospaced))
                }
            } else {
                Text(styledText(card.answer ?? "")).font(.system(size: 15)).textSelection(.enabled)
                if let diff = session.diff { DiffView(diff: diff) }
                if let code = card.code, !code.isEmpty {
                    sectionLabel(card.isDrill ? "Template" : "Code")
                    CodeBlock(code: code)
                }
            }
            if let why = card.why, !why.isEmpty, card.slot != "review" {
                Text(styledText(why)).font(.system(size: 13)).foregroundStyle(Theme.ink2)
            }
        }
    }

    @ViewBuilder
    private func labeled(_ title: String, _ text: String?) -> some View {
        if let text, !text.isEmpty {
            VStack(alignment: .leading, spacing: 3) {
                sectionLabel(title)
                Text(styledText(text)).font(.system(size: 15)).textSelection(.enabled)
            }
        }
    }

    private func sectionLabel(_ text: String) -> some View {
        Text(text.uppercased()).font(Theme.label).foregroundStyle(Theme.ink3)
    }

    private func revealButton(_ card: Card) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Group {
                if card.isDrill {
                    Button { model.reveal() } label: { revealLabel("Check my code") }
                        .keyboardShortcut(.return, modifiers: .command)
                } else {
                    Button { withAnimation(.easeOut(duration: 0.18)) { model.reveal() } } label: {
                        revealLabel(card.isProblem ? "I'm done, show notes" : "Show answer")
                    }
                    .keyboardShortcut(.space, modifiers: [])
                }
            }
            .buttonStyle(.borderedProminent)
            .tint(Theme.ink)
            .controlSize(.large)

            HStack(spacing: 14) {
                Text(card.isDrill ? "⌘ Return to check" : "Space to show")
                    .font(Theme.label)
                    .foregroundStyle(Theme.ink3)
                if card.isProblem {
                    Button("Never show this one") { model.hideCurrent() }
                        .buttonStyle(.link)
                        .font(.system(size: 12))
                }
            }
        }
    }

    private func revealLabel(_ text: String) -> some View {
        Text(text).fontWeight(.semibold).frame(maxWidth: .infinity)
    }

    private var gradeButtons: some View {
        HStack(spacing: 10) {
            GradeButton(session: session, grade: "b", name: "Blue", note: "Clean, no help", key: "1")
            GradeButton(session: session, grade: "y", name: "Yellow", note: "Small hint or a slip", key: "2")
            GradeButton(session: session, grade: "r", name: "Red", note: "Wrong or no idea", key: "3")
        }
    }

    // MARK: Finished

    private var finished: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(session.isMock ? "Mock finished." : session.items.count > 1 ? "Queue cleared." : "Done.")
                .font(.system(size: 24, weight: .semibold))
            if session.items.count > 1 {
                Text("\(session.items.count) graded. Everything has its next check.")
                    .foregroundStyle(Theme.ink3)
            }
            if let moved = session.moved {
                HStack(spacing: 10) {
                    Text(moved).foregroundStyle(Theme.ink2)
                    if session.undoable != nil {
                        Button("Undo") { model.undoLast() }
                            .buttonStyle(.link)
                            .keyboardShortcut("z", modifiers: .command)
                    }
                }
            }
            Button("Back to Today") { model.exitSession() }
                .buttonStyle(.borderedProminent)
                .tint(Theme.ink)
                .keyboardShortcut(.defaultAction)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .sheetStyle(padding: 24)
    }
}

private struct GradeButton: View {
    @Environment(AppModel.self) private var model
    let session: StudySession
    let grade, name, note, key: String
    @State private var hovering = false

    private var suggested: Bool { session.diff?.suggest == grade }

    var body: some View {
        Button { model.pick(grade: grade) } label: {
            VStack(alignment: .leading, spacing: 1) {
                HStack {
                    Text(name).font(.system(size: 15, weight: .semibold))
                    Spacer()
                    Text(key)
                        .font(.system(size: 11, design: .monospaced))
                        .foregroundStyle(Theme.ink2)
                        .padding(.horizontal, 4)
                        .overlay(RoundedRectangle(cornerRadius: 4).strokeBorder(Theme.line))
                }
                Text(note).font(.system(size: 12.5)).foregroundStyle(Theme.ink3)
            }
            .padding(.horizontal, 14)
            .padding(.vertical, 11)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(hovering || suggested ? Theme.softColor(grade: grade) : Theme.sheet)
            .overlay(alignment: .bottom) {
                Rectangle().fill(Theme.color(grade: grade)).frame(height: 3)
            }
            .clipShape(RoundedRectangle(cornerRadius: 9))
            .overlay(RoundedRectangle(cornerRadius: 9)
                .strokeBorder(hovering || suggested ? Theme.color(grade: grade) : Theme.line))
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .keyboardShortcut(KeyEquivalent(Character(key)), modifiers: [])
        .onHover { hovering = $0 }
    }
}

private struct DiffView: View {
    let diff: DiffReply

    private var text: AttributedString {
        var result = AttributedString()
        for (index, op) in diff.ops.enumerated() {
            var piece = AttributedString(op.t)
            if op.s == 1 {
                piece.foregroundColor = Theme.blueText
                piece.backgroundColor = Theme.blueSoft
            } else if op.s == 2 {
                piece.foregroundColor = Theme.redText
                piece.backgroundColor = Theme.redSoft
                piece.strikethroughStyle = .single
            }
            result += piece
            if index + 1 < diff.ops.count { result += AttributedString(" ") }
        }
        return result
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 5) {
            Text("YOUR CODE AGAINST THE TEMPLATE, \(Int((diff.score * 100).rounded()))% MATCH")
                .font(Theme.label)
                .foregroundStyle(Theme.ink3)
            Text(text)
                .font(.system(size: 13, design: .monospaced))
                .textSelection(.enabled)
                .padding(12)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(Theme.paper)
                .clipShape(RoundedRectangle(cornerRadius: 8))
            Text("Blue is what you left out. Red is what you added.")
                .font(.system(size: 12))
                .foregroundStyle(Theme.ink3)
        }
    }
}

private struct StudyClock: View {
    let session: StudySession
    let card: Card

    var body: some View {
        TimelineView(.periodic(from: .now, by: 1)) { context in
            let reading = reading(at: context.date)
            Text(reading.text)
                .font(.system(size: 15, weight: .semibold, design: .monospaced))
                .foregroundStyle(reading.over ? Theme.redText : Theme.ink)
        }
    }

    private func reading(at now: Date) -> (text: String, over: Bool) {
        if let ends = session.mockEnds {
            let left = ends.timeIntervalSince(now)
            return (format(left), left <= 0)
        }
        let spent = now.timeIntervalSince(session.started)
        return (format(spent), spent > Double((card.timebox ?? 25) * 60))
    }

    private func format(_ seconds: Double) -> String {
        let total = max(0, Int(seconds))
        return String(format: "%02d:%02d", total / 60, total % 60)
    }
}
