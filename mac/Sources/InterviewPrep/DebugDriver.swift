#if DEBUG
import AppKit
import Foundation

/// A debug-build test driver. With INTERVIEWPREP_SNAPSHOT_DIR set, it walks the real
/// app through its main flows and saves a PNG of each screen, so the UI can be
/// checked without Screen Recording permission. Not compiled into release builds.
@MainActor
enum DebugDriver {
    static func runIfRequested(model: AppModel) {
        let environment = ProcessInfo.processInfo.environment
        guard let directory = environment["INTERVIEWPREP_SNAPSHOT_DIR"] else { return }
        if environment["INTERVIEWPREP_DARK"] != nil { NSApp.appearance = NSAppearance(named: .darkAqua) }

        Task { @MainActor in
            var log: [String] = []
            @MainActor func shot(_ name: String, of window: NSWindow? = nil) async {
                try? await Task.sleep(for: .milliseconds(700))
                save(name, to: directory, window: window)
                log.append(name)
            }

            await shot("01-today")
            let (settings, outcome) = await clickSettingsButton()
            log.append("settings button: \(outcome)")
            if let settings {
                await shot("01a-settings", of: settings)
                settings.close()
            }
            model.section = .decks
            await shot("02-decks")
            model.section = .library
            await shot("03-library")
            model.section = .insights
            await shot("04-insights")
            model.section = .today

            model.start(lane: "code")
            await shot("05-problem-front")

            let clipboard = NSPasteboard(name: .init("com.interviewprep.debug"))
            model.clipboard = clipboard
            clipboard.clearContents()
            clipboard.setString("int first() {\n  return 1;\n}\n", forType: .string)
            model.pasteAttempt()
            model.pasteAttempt()  // a double press saves once
            await shot("05a-attempt-saved")
            clipboard.clearContents()
            clipboard.setString("int second();\n", forType: .string)
            log.append("key window for shortcut: \(await pressCommandV())")
            await shot("05b-attempt-by-shortcut")

            model.reveal()
            await shot("06-problem-back")
            model.pick(grade: "r")
            await shot("09-after-commit")
            model.exitSession()

            model.start(lane: "cards")
            await shot("10-card-front")
            model.reveal()
            await shot("11-card-back")
            model.pick(grade: "b")
            await shot("12-next-card")
            model.undoLast()
            await shot("13-after-undo")
            model.exitSession()

            if let drill = firstDrill(model) {
                model.open(id: drill)
                await shot("14-drill-front")
                model.session?.typed = "int lo = 0;"
                model.reveal()
                await shot("15-drill-back")
                model.exitSession()
            }

            model.startMock()
            await shot("16-mock")
            model.exitSession()
            await shot("17-today-after")

            model.section = .insights
            await shot("18-insights-after")

            model.section = .today
            await shot("19-review-bar")
            if environment["INTERVIEWPREP_TRY_REVIEW"] != nil {
                var opened: [String] = []
                model.openLink = { url in
                    opened.append(url.absoluteString)
                    return environment["INTERVIEWPREP_LINK_FAILS"] == nil
                }
                model.startReview()
                await shot("20-review-running")
                while model.reviewRunning { try? await Task.sleep(for: .seconds(1)) }
                await shot("21-review-done")
                log.append("review error: \(model.errorText ?? "none")")
                log.append("links opened: \(opened)")
                log.append("waiting in Claude: \(model.reviewInClaude)")
                model.errorText = nil
                model.reviewText = nil
                await shot("22-back-on-today")
            }

            try? log.joined(separator: "\n").write(
                toFile: directory + "/done.txt", atomically: true, encoding: .utf8)
            NSApp.terminate(nil)
        }
    }

    /// ⌘V through the app's normal event path, as the keyboard would send it.
    /// Returns whether the window was key when it went.
    private static func pressCommandV() async -> Bool {
        guard let window = await keyWindow() else { return false }
        for type in [NSEvent.EventType.keyDown, .keyUp] {
            guard let event = NSEvent.keyEvent(
                with: type, location: .zero, modifierFlags: .command,
                timestamp: ProcessInfo.processInfo.systemUptime, windowNumber: window.windowNumber,
                context: nil, characters: "v", charactersIgnoringModifiers: "v",
                isARepeat: false, keyCode: 9) else { continue }
            NSApp.sendEvent(event)
        }
        return window.isKeyWindow
    }

    /// Clicks the sidebar's Settings button with a real mouse event, so the test
    /// goes through the button itself. It can't be looked up by name from in here,
    /// because SwiftUI builds its accessibility tree only for an assistive app.
    /// Returns the window that opened, and what happened, for the log.
    private static func clickSettingsButton() async -> (NSWindow?, String) {
        guard let window = await keyWindow() else { return (nil, "no window to click in") }
        guard let sidebar = splitView(in: window.contentView)?.arrangedSubviews.first else {
            return (nil, "no sidebar found")
        }
        // The button fills the bottom of the sidebar, about 36 points tall.
        let frame = sidebar.convert(sidebar.bounds, to: nil)
        let before = Set(NSApp.windows.filter(\.isVisible).map { ObjectIdentifier($0) })
        for type in [NSEvent.EventType.leftMouseDown, .leftMouseUp] {
            guard let event = NSEvent.mouseEvent(
                with: type, location: NSPoint(x: frame.midX, y: frame.minY + 18), modifierFlags: [],
                timestamp: ProcessInfo.processInfo.systemUptime, windowNumber: window.windowNumber,
                context: nil, eventNumber: 0, clickCount: 1, pressure: 1) else { continue }
            NSApp.sendEvent(event)
        }
        for _ in 0..<40 {
            try? await Task.sleep(for: .milliseconds(50))
            if let opened = NSApp.windows.first(where: { $0.isVisible && !before.contains(ObjectIdentifier($0)) }) {
                return (opened, "opened \"\(opened.title)\"")
            }
        }
        return (nil, "clicked, but no window opened within 2 seconds")
    }

    /// Activation lands on a later turn of the run loop, and an event sent before
    /// then reaches no window, so this waits up to 2 seconds for the window to be key.
    private static func keyWindow() async -> NSWindow? {
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
        guard let window = NSApp.windows.first(where: \.isVisible) else { return nil }
        window.makeKeyAndOrderFront(nil)
        for _ in 0..<40 where !window.isKeyWindow {
            try? await Task.sleep(for: .milliseconds(50))
        }
        return window
    }

    private static func splitView(in view: NSView?) -> NSSplitView? {
        guard let view else { return nil }
        if let split = view as? NSSplitView { return split }
        for child in view.subviews {
            if let found = splitView(in: child) { return found }
        }
        return nil
    }

    private static func firstDrill(_ model: AppModel) -> String? {
        model.send(["cmd": "library", "kind": "card", "deck": "templates", "limit": 1], as: LibraryReply.self)?.rows.first?.id
    }

    private static func save(_ name: String, to directory: String, window chosen: NSWindow? = nil) {
        let window = chosen
            ?? NSApp.windows.first { $0.isVisible && $0.isSheet }
            ?? NSApp.windows.first { $0.isVisible && $0.contentView != nil }
        guard let view = window?.contentView,
              let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { return }
        view.cacheDisplay(in: view.bounds, to: rep)
        guard let png = rep.representation(using: .png, properties: [:]) else { return }
        try? png.write(to: URL(fileURLWithPath: "\(directory)/\(name).png"))
    }
}
#endif
