import SwiftUI

/// One review, run on its own: Claude Code with the InterviewPrep tools and nothing
/// else, so it can't read other files or run anything. It uses your Claude plan,
/// and it only ever starts because you pressed the button.
enum Reviewer {
    struct Failure: LocalizedError {
        let message: String
        var errorDescription: String? { message }
    }

    /// Claude Code has no working login of its own on this Mac. That login is
    /// separate from the Claude app's, so the app may still be signed in.
    struct SignInNeeded: Error {}

    private struct Reply: Decodable {
        let result: String?
        let isError: Bool?
        let apiErrorStatus: Int?

        enum CodingKeys: String, CodingKey {
            case result
            case isError = "is_error"
            case apiErrorStatus = "api_error_status"
        }
    }

    private static let prompt = "Review what's waiting, even if it's fewer than ten. Write your summary as short "
        + "paragraphs and bullet lists, with no tables or headings."
    private static let chatPrompt = "Review what's waiting in InterviewPrep, even if it's fewer than ten."
    private static let tools = ["profile_read", "topics_list", "batch_pending", "judgements_search",
                                "judgement_write", "judgement_drop", "batch_commit"]
    private static let timeLimit: TimeInterval = 15 * 60

    /// Sonnet caught the same planted bugs as Fable in live tests, at about a
    /// tenth of the cost: $0.12 against $1.10 for a review of five problems.
    private static let model = "sonnet"

    /// A new Code chat in the Claude app with the request already typed. It runs on
    /// the app's own sign-in and whichever model is picked there. Claude never sends
    /// a link's text by itself, so nothing is spent until you press send. The chat
    /// needs the server connected once, as the README shows.
    static var chatLink: URL? {
        var link = URLComponents(string: "claude://code/new")
        link?.queryItems = [URLQueryItem(name: "q", value: chatPrompt)]
        return link?.url
    }

    /// Returns what the model has to say. Runs for a minute or two, off the main
    /// thread. If it stops early, whatever it judged is already saved and the rest
    /// stays waiting, so nothing needs undoing.
    static func run() async throws -> String {
        guard let claude = claudeExecutable() else {
            throw Failure(message: "Claude Code isn't installed on this Mac, so the review can't start.")
        }
        guard let server = serverExecutable(), let resources = Storage.resourcesFile else {
            throw Failure(message: "The coach server is missing from the app.")
        }
        let config: [String: Any] = ["mcpServers": ["interviewprep": [
            "type": "stdio", "command": server.path,
            "args": ["--state", Storage.stateFile.path, "--resources", resources.standardizedFileURL.path, "mcp"],
        ]]]

        let process = Process()
        process.executableURL = claude
        process.arguments = [
            "-p", prompt,
            "--model", model,
            "--mcp-config", String(decoding: try JSONSerialization.data(withJSONObject: config), as: UTF8.self),
            "--strict-mcp-config",
            "--tools", "",
            "--allowedTools", tools.map { "mcp__interviewprep__\($0)" }.joined(separator: ","),
            "--output-format", "json",
            "--no-session-persistence",
            "--setting-sources", "",
        ]
        process.currentDirectoryURL = Storage.directory
        process.standardInput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        let output = Pipe()
        process.standardOutput = output
        try process.run()

        let watchdog = DispatchWorkItem { if process.isRunning { process.terminate() } }
        DispatchQueue.global().asyncAfter(deadline: .now() + timeLimit, execute: watchdog)
        let data = await Task.detached { () -> Data in
            let data = output.fileHandleForReading.readDataToEndOfFile()
            process.waitUntilExit()
            return data
        }.value
        watchdog.cancel()

        guard let reply = try? JSONDecoder().decode(Reply.self, from: data), let text = reply.result else {
            throw Failure(message: "Claude Code stopped before the review finished.")
        }
        // `claude auth status` says logged in even when the token is dead, so only
        // a real request can tell. A refused one costs nothing.
        if reply.apiErrorStatus == 401 || text.hasPrefix("Not logged in") { throw SignInNeeded() }
        if reply.isError == true { throw Failure(message: text) }
        return text
    }

    /// The usual install spots, then the copy the Claude desktop app keeps in a
    /// folder per version, newest first.
    private static func claudeExecutable() -> URL? {
        let files = FileManager.default
        let home = files.homeDirectoryForCurrentUser
        let usual = [home.appendingPathComponent(".claude/local/claude"),
                     URL(fileURLWithPath: "/opt/homebrew/bin/claude"),
                     URL(fileURLWithPath: "/usr/local/bin/claude")]
        if let found = usual.first(where: { files.isExecutableFile(atPath: $0.path) }) { return found }

        let root = home.appendingPathComponent("Library/Application Support/Claude/claude-code")
        let versions = (try? files.contentsOfDirectory(atPath: root.path)) ?? []
        for version in versions.sorted(by: { $0.compare($1, options: .numeric) == .orderedDescending }) {
            let folder = root.appendingPathComponent(version)
            for build in (try? files.contentsOfDirectory(atPath: folder.path)) ?? [] {
                let candidate = folder.appendingPathComponent("\(build)/claude.app/Contents/MacOS/claude")
                if files.isExecutableFile(atPath: candidate.path) { return candidate }
            }
        }
        return nil
    }

    /// The server inside the app. `swift run` has no bundle, so it falls back to
    /// the project's build folder.
    private static func serverExecutable() -> URL? {
        let candidates = [Bundle.main.bundleURL.appendingPathComponent("Contents/Helpers/interviewprep"),
                          URL(fileURLWithPath: "build/interviewprep").standardizedFileURL]
        return candidates.first { FileManager.default.isExecutableFile(atPath: $0.path) }
    }
}

/// The coach's line on Today. Counting what's waiting costs nothing: nothing goes
/// to a model until you press the button.
struct ReviewBar: View {
    @Environment(AppModel.self) private var model

    private var status: String {
        if model.reviewRunning { return "Reading your code. This takes a minute or two." }
        if model.reviewInClaude { return "Claude Code couldn't sign in, so this opened in Claude. Press send there." }
        let waiting = model.reviewWaiting, batch = model.reviewBatch
        if waiting == 0 { return "Nothing waiting for review." }
        if waiting > batch { return "\(waiting) problems waiting. One review takes \(batch)." }
        if waiting == batch { return "\(batch) problems are ready." }
        return "\(waiting) of \(batch) ready. You can review early."
    }

    var body: some View {
        HStack(spacing: 14) {
            Text("Coach").font(.system(size: 16, weight: .bold))
            Text(status).foregroundStyle(Theme.ink2)
            Spacer()
            if model.reviewRunning { ProgressView().controlSize(.small) }
            if model.hasLastReview { Button("Last review") { model.openLastReview() } }
            if model.reviewWaiting >= model.reviewBatch {
                reviewButton.buttonStyle(.borderedProminent).tint(Theme.ink)
            } else {
                reviewButton
            }
        }
        .sheetStyle()
    }

    private var reviewButton: some View {
        Button("Review now") { model.startReview() }
            .disabled(model.reviewRunning || model.reviewWaiting == 0)
            .help("Runs Claude on your saved code. It uses your Claude plan.")
    }
}

struct ReviewSheet: View {
    @Environment(AppModel.self) private var model
    let text: String

    private var rendered: AttributedString {
        let options = AttributedString.MarkdownParsingOptions(interpretedSyntax: .inlineOnlyPreservingWhitespace)
        return (try? AttributedString(markdown: text, options: options)) ?? AttributedString(text)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Your review").font(.system(size: 20, weight: .bold))
            ScrollView {
                Text(rendered)
                    .font(.system(size: 14))
                    .lineSpacing(3)
                    .textSelection(.enabled)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
            HStack {
                Spacer()
                Button("Done") { model.reviewText = nil }
                    .buttonStyle(.borderedProminent)
                    .tint(Theme.ink)
                    .keyboardShortcut(.defaultAction)
            }
        }
        .padding(24)
        .frame(width: 640, height: 520)
    }
}
