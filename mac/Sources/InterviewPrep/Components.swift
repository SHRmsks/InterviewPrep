import AppKit
import SwiftUI

struct TrackDot: View {
    let track: String

    var body: some View {
        ZStack {
            if track == "new" {
                Circle().strokeBorder(Theme.untried, lineWidth: 1.5)
            } else if track == "retired" {
                Circle().strokeBorder(Theme.blue, lineWidth: 1.5).padding(-3)
                Circle().fill(Theme.blue)
            } else {
                Circle().fill(Theme.color(track: track))
            }
        }
        .frame(width: 10, height: 10)
        .accessibilityLabel(track)
    }
}

/// How far a card is through its track's expires, as small dots beside it in a list.
struct Pips: View {
    let card: Card

    var body: some View {
        HStack(spacing: 3) {
            ForEach(0..<card.steps, id: \.self) { index in
                Capsule()
                    .fill(index <= card.step ? Theme.color(track: card.track) : Theme.line)
                    .opacity(index == card.step ? 0.4 : 1)
                    .frame(width: 12, height: 4)
            }
        }
        .help(card.position)
    }
}

/// Where this card is on its way out, as the core lays out its checks.
struct Rail: View {
    let card: Card

    private struct Stop: Identifiable {
        let id: Int
        let track: String
        let gap: String
    }

    private var stops: [Stop] {
        (card.rail ?? []).enumerated().map { Stop(id: $0.offset, track: $0.element.track, gap: $0.element.gap) }
    }

    private var here: Int { min(card.railAt ?? 0, max(0, stops.count - 1)) }

    var body: some View {
        HStack(alignment: .top, spacing: 0) {
            ForEach(stops) { stop in
                VStack(spacing: 5) {
                    ZStack {
                        if stop.id > 0 {
                            Rectangle()
                                .fill(stop.id <= here ? Theme.color(track: stop.track) : Theme.line)
                                .frame(height: 2)
                                .offset(x: -23)
                                .frame(width: 46)
                        }
                        marker(stop)
                    }
                    Text(stop.gap)
                        .font(.system(size: 11, weight: .medium, design: .monospaced))
                        .foregroundStyle(stop.id == here ? Theme.ink : Theme.ink3)
                }
                .frame(width: 46)
            }
            VStack(spacing: 5) {
                ZStack {
                    Rectangle().fill(Theme.line).frame(height: 2).offset(x: -23).frame(width: 46)
                    RoundedRectangle(cornerRadius: 2).strokeBorder(Theme.line, lineWidth: 2)
                        .background(RoundedRectangle(cornerRadius: 2).fill(Theme.sheet))
                        .frame(width: 12, height: 12)
                }
                Text("gone")
                    .font(.system(size: 11, weight: .medium, design: .monospaced))
                    .foregroundStyle(Theme.ink3)
            }
            .frame(width: 46)
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(card.position)
    }

    @ViewBuilder
    private func marker(_ stop: Stop) -> some View {
        let color = Theme.color(track: stop.track)
        if stop.id < here {
            Circle().fill(color).frame(width: 12, height: 12)
        } else if stop.id == here {
            Circle().fill(Theme.sheet).frame(width: 12, height: 12)
                .overlay(Circle().strokeBorder(color, lineWidth: 2).padding(-3))
        } else {
            Circle().strokeBorder(Theme.line, lineWidth: 2)
                .background(Circle().fill(Theme.sheet))
                .frame(width: 12, height: 12)
        }
    }
}

struct SlotBadge: View {
    let slot: String?

    private var label: String? {
        switch slot {
        case "sibling": return "Sibling"
        case "weak": return "Weak spot"
        case "explore": return "New ground"
        case "new-pattern": return "New pattern"
        case "new-topic": return "New topic"
        case "practice": return "Practice"
        case "coach": return "Coach"
        case "wildcard": return "Wildcard"
        case "new": return "New"
        default: return nil
        }
    }

    private var color: Color {
        switch slot {
        case "sibling", "new-pattern", "new-topic": return Theme.blueText
        case "weak": return Theme.redText
        case "wildcard", "coach", "practice": return Theme.ink
        default: return Theme.ink3
        }
    }

    var body: some View {
        if let label {
            Text(label)
                .font(.system(size: 10.5, weight: .medium, design: .monospaced))
                .foregroundStyle(color)
                .padding(.horizontal, 5)
                .padding(.vertical, 1)
                .overlay(RoundedRectangle(cornerRadius: 4).strokeBorder(color.opacity(0.7)))
        }
    }
}

struct Meter: View {
    let value: Double
    var color: Color = Theme.ink2

    var body: some View {
        GeometryReader { geometry in
            ZStack(alignment: .leading) {
                Capsule().fill(Theme.line)
                Capsule().fill(color).frame(width: geometry.size.width * min(1, max(0, value)))
            }
        }
        .frame(height: 4)
    }
}

struct CodeBlock: View {
    let code: String

    var body: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            Text(code)
                .font(.system(size: 13, design: .monospaced))
                .foregroundStyle(Theme.ink)
                .textSelection(.enabled)
                .padding(12)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
        .background(Theme.paper)
        .clipShape(RoundedRectangle(cornerRadius: 8))
        .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(Theme.line2))
    }
}

/// A plain code editor. SwiftUI's TextEditor applies smart quotes and dashes on
/// macOS, which silently corrupts typed code, so this turns all of that off.
struct CodeEditor: NSViewRepresentable {
    @Binding var text: String

    func makeCoordinator() -> Coordinator { Coordinator(self) }

    func makeNSView(context: Context) -> NSScrollView {
        let scroll = NSTextView.scrollableTextView()
        guard let view = scroll.documentView as? NSTextView else { return scroll }
        view.delegate = context.coordinator
        view.font = NSFont.monospacedSystemFont(ofSize: 13.5, weight: .regular)
        view.isRichText = false
        view.allowsUndo = true
        view.isAutomaticQuoteSubstitutionEnabled = false
        view.isAutomaticDashSubstitutionEnabled = false
        view.isAutomaticTextReplacementEnabled = false
        view.isAutomaticSpellingCorrectionEnabled = false
        view.isContinuousSpellCheckingEnabled = false
        view.isGrammarCheckingEnabled = false
        view.smartInsertDeleteEnabled = false
        view.textContainerInset = NSSize(width: 8, height: 10)
        view.drawsBackground = false
        view.string = text
        scroll.drawsBackground = false
        DispatchQueue.main.async { view.window?.makeFirstResponder(view) }
        return scroll
    }

    func updateNSView(_ scroll: NSScrollView, context: Context) {
        guard let view = scroll.documentView as? NSTextView, view.string != text else { return }
        view.string = text
    }

    final class Coordinator: NSObject, NSTextViewDelegate {
        let parent: CodeEditor

        init(_ parent: CodeEditor) { self.parent = parent }

        func textDidChange(_ notification: Notification) {
            guard let view = notification.object as? NSTextView else { return }
            parent.text = view.string
        }

        func textView(_ textView: NSTextView, doCommandBy selector: Selector) -> Bool {
            if selector == #selector(NSResponder.insertTab(_:)) {
                textView.insertText("    ", replacementRange: textView.selectedRange())
                return true
            }
            return false
        }
    }
}
