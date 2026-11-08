import CCore
import Foundation

/// The C++ core, wrapped. Every rule lives on the other side of this file: Swift
/// sends JSON commands and shows what comes back, exactly as the website does.
enum Core {
    static func loadResources(_ json: String) -> Int {
        Int(interviewprep_load_resources(json))
    }

    static func loadState(_ json: String) -> Bool {
        interviewprep_load_state(json) == 0
    }

    static func saveState() -> String {
        String(cString: interviewprep_save_state())
    }

    static func lastError() -> String {
        String(cString: interviewprep_last_error())
    }

    static func call(_ request: [String: Any]) -> Data {
        guard let body = try? JSONSerialization.data(withJSONObject: request),
              let text = String(data: body, encoding: .utf8) else {
            return Data(#"{"ok":false,"error":"Could not encode the request."}"#.utf8)
        }
        return Data(String(cString: interviewprep_call(text)).utf8)
    }
}

/// Local wall-clock time, in the form the core expects.
func nowString() -> String {
    let formatter = DateFormatter()
    formatter.locale = Locale(identifier: "en_US_POSIX")
    formatter.dateFormat = "yyyy-MM-dd'T'HH:mm"
    return formatter.string(from: Date())
}

/// The same progress file the command-line tool uses, so the two stay in step.
enum Storage {
    static let directory: URL = {
        if let custom = ProcessInfo.processInfo.environment["INTERVIEWPREP_DATA_DIR"] {
            return URL(fileURLWithPath: custom, isDirectory: true)
        }
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        let folder = support.appendingPathComponent("InterviewPrep", isDirectory: true)
        // The app was called Coldstart until November 2026. Its folder moves over
        // once, whole, the first time the app or the command-line tool looks for it.
        if !FileManager.default.fileExists(atPath: folder.path) {
            try? FileManager.default.moveItem(at: support.appendingPathComponent("Coldstart", isDirectory: true), to: folder)
        }
        return folder
    }()

    static var stateFile: URL { directory.appendingPathComponent("state.json") }
    static var backupFile: URL { directory.appendingPathComponent("state.backup.json") }

    /// The resources file. `swift run` has no app bundle, so it falls back to the
    /// project folder.
    static var resourcesFile: URL? {
        if let bundled = Bundle.main.url(forResource: "resources", withExtension: "json") { return bundled }
        let development = URL(fileURLWithPath: "data/resources.json")
        return FileManager.default.fileExists(atPath: development.path) ? development : nil
    }

    /// The core names these, so the folder layout stays in one place.
    static func text(at relative: String) -> String? {
        try? String(contentsOf: directory.appendingPathComponent(relative), encoding: .utf8)
    }

    static func save(_ text: String, at relative: String) throws {
        let url = directory.appendingPathComponent(relative)
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try text.write(to: url, atomically: true, encoding: .utf8)
    }

    /// `read()` can't say this by itself: it also returns nil for a file that is
    /// there but can't be decoded.
    static var hasState: Bool { FileManager.default.fileExists(atPath: stateFile.path) }

    static func read() -> String? {
        try? String(contentsOf: stateFile, encoding: .utf8)
    }

    static func readBackup() -> String? {
        try? String(contentsOf: backupFile, encoding: .utf8)
    }

    /// Moves a progress file the app can't read out of the way, so nothing saves
    /// over it. Returns the name it has now, or nil if it couldn't be moved.
    static func setAsideState() -> String? {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyy-MM-dd'T'HH-mm-ss"
        let name = "state.unreadable-\(formatter.string(from: Date())).json"
        do {
            try FileManager.default.moveItem(at: stateFile, to: directory.appendingPathComponent(name))
            return name
        } catch {
            return nil
        }
    }

    static func write(_ text: String) throws {
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        try text.write(to: stateFile, atomically: true, encoding: .utf8)
    }

    /// One copy from the start of the session, in case something goes wrong.
    static func backUp() {
        guard let text = read() else { return }
        try? text.write(to: backupFile, atomically: true, encoding: .utf8)
    }

    /// Saves a new file and never replaces one: a second save in the same minute
    /// gets its own number. The core chooses the name; see `attempt` in the engine.
    /// A file's text, or nil when there is no such file. A file that exists but
    /// can't be read throws, so a paste is never numbered as if the day were empty.
    static func existingText(at relative: String) throws -> String? {
        let url = directory.appendingPathComponent(relative)
        guard FileManager.default.fileExists(atPath: url.path) else { return nil }
        return try String(contentsOf: url, encoding: .utf8)
    }

    /// Adds to the end of a file, creating it and its folder first if needed.
    static func append(_ text: String, at relative: String) throws {
        let url = directory.appendingPathComponent(relative)
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        if !FileManager.default.fileExists(atPath: url.path) {
            try Data(text.utf8).write(to: url, options: .withoutOverwriting)
            return
        }
        let handle = try FileHandle(forWritingTo: url)
        defer { try? handle.close() }
        try handle.seekToEnd()
        try handle.write(contentsOf: Data(text.utf8))
    }
}
