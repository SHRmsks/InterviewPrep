import AppKit
import SwiftUI
import UniformTypeIdentifiers

@main
struct InterviewPrepApp: App {
    @State private var model = AppModel()

    var body: some Scene {
        WindowGroup("InterviewPrep", id: "main") {
            RootView()
                .environment(model)
                .frame(minWidth: 820, minHeight: 540)
                #if DEBUG
                .task { DebugDriver.runIfRequested(model: model) }
                #endif
        }
        .defaultSize(width: 1100, height: 760)
        .commands {
            CommandGroup(replacing: .newItem) {}
            CommandMenu("Study") {
                Button("Start Problems") { model.start(lane: "code") }
                Button("Start Cards") { model.start(lane: "cards") }
                Button("Pull One More Problem") { model.pullMore(lane: "code") }
                Button("Start a Mock Interview") { model.startMock() }
            }
        }

        Settings {
            SettingsView().environment(model)
        }
    }
}

struct RootView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        @Bindable var model = model
        NavigationSplitView {
            List(selection: Binding(
                get: { model.section },
                set: { value in
                    if let value {
                        if model.session != nil { model.exitSession() }
                        model.section = value
                    }
                }
            )) {
                ForEach(AppSection.allCases) { section in
                    Label(section.rawValue, systemImage: section.symbol)
                        .tag(section)
                        .badge(section == .today ? model.leftToday : 0)
                }
            }
            .navigationSplitViewColumnWidth(min: 170, ideal: 190, max: 240)
            .safeAreaInset(edge: .bottom, spacing: 0) { SettingsButton() }
        } detail: {
            Group {
                if let session = model.session {
                    StudyView(session: session)
                } else {
                    switch model.section {
                    case .today: TodayView()
                    case .decks: DecksView()
                    case .library: LibraryView()
                    case .insights: InsightsView()
                    }
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(Theme.paper)
        }
        .alert("Something went wrong", isPresented: Binding(
            get: { model.errorText != nil },
            set: { if !$0 { model.errorText = nil } }
        )) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(model.errorText ?? "")
        }
    }
}

/// Settings open in their own window, the usual Mac way, which otherwise only the
/// menu bar and ⌘, reach. This keeps a way in on screen, at the foot of the sidebar.
private struct SettingsButton: View {
    var body: some View {
        VStack(spacing: 0) {
            Divider()
            SettingsLink {
                Label("Settings", systemImage: "gearshape")
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.horizontal, 18)
                    .padding(.vertical, 10)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .help("Daily limits, target company, which days count, and export or import. ⌘, opens it too.")
        }
    }
}

struct SettingsView: View {
    @Environment(AppModel.self) private var model
    @State private var settings: AppSettings?

    var body: some View {
        Form {
            if let settings {
                Section("Daily queue") {
                    number("Problems a day", settings.codeCap, 1...50, key: "codeCap")
                    number("New problems a day", settings.codeNew, 0...20, key: "codeNew")
                    number("Reviews a day", settings.codeReviews, 0...50, key: "codeReviews")
                    number("Cards a day", settings.cardCap, 1...200, key: "cardCap")
                    number("New cards a day", settings.cardNew, 0...100, key: "cardNew")
                }
                Section("Recommendations") {
                    Picker("Target company", selection: Binding(
                        get: { settings.company },
                        set: { update(["company": $0]) }
                    )) {
                        Text("Any company").tag("")
                        ForEach(model.facets?.companies ?? []) { Text($0.name).tag($0.slug) }
                    }
                    Toggle("Recommend LeetCode Premium problems", isOn: Binding(
                        get: { settings.premium },
                        set: { update(["premium": $0]) }
                    ))
                }
                Section("Schedule") {
                    Picker("Days that count", selection: Binding(
                        get: { settings.clock },
                        set: { update(["clock": $0]) }
                    )) {
                        Text("Only days I study").tag("study")
                        Text("Every calendar day").tag("calendar")
                    }
                }
                Section("Your progress") {
                    HStack {
                        Button("Export…") { exportProgress() }
                        Button("Replace from File…") { importProgress() }
                    }
                }
            }
        }
        .formStyle(.grouped)
        // Tall enough for every section. Without a height the window opened at
        // about 450 points and hid export and import below the fold.
        .frame(width: 440, height: 560)
        .onAppear { settings = model.settings() }
    }

    private func number(_ title: String, _ value: Int, _ range: ClosedRange<Int>, key: String) -> some View {
        Stepper(value: Binding(get: { value }, set: { update([key: $0]) }), in: range) {
            HStack {
                Text(title)
                Spacer()
                Text("\(value)").monospacedDigit()
            }
        }
    }

    private func update(_ change: [String: Any]) {
        model.apply(settings: change)
        settings = model.settings()
    }

    private func exportProgress() {
        let panel = NSSavePanel()
        panel.nameFieldStringValue = "interviewprep-progress.json"
        panel.allowedContentTypes = [.json]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            try model.exportState().write(to: url, atomically: true, encoding: .utf8)
        } catch {
            model.errorText = "Couldn't write that file: \(error.localizedDescription)"
        }
    }

    private func importProgress() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.json]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        guard let text = try? String(contentsOf: url, encoding: .utf8) else {
            model.errorText = "Couldn't read that file."
            return
        }
        if model.importState(text) { settings = model.settings() }
    }
}
