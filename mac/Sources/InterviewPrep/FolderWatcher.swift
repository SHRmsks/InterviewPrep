import CoreServices
import Foundation

/// Tells the app, on the main thread, that something in a folder changed, at
/// any depth. The system's file events see both ways a file here gets written:
/// the progress file is swapped for a new one, and the coach's log is added to
/// in place. A watch on one open file would miss the first of those, because
/// it keeps following the file that was swapped out.
///
/// It only says "look again". What changed, if anything, is for the caller to
/// read: the app's own saves arrive here too.
final class FolderWatcher {
    private var stream: FSEventStreamRef?
    private let changed: () -> Void

    /// Nil when the system won't watch the folder. The app still works then,
    /// and looks again whenever you come back to it.
    init?(folder: URL, changed: @escaping () -> Void) {
        self.changed = changed
        // A new user has no data folder yet, and a folder that isn't there can't be watched.
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)

        var context = FSEventStreamContext(version: 0, info: Unmanaged.passUnretained(self).toOpaque(),
                                           retain: nil, release: nil, copyDescription: nil)
        let callback: FSEventStreamCallback = { _, info, _, _, _, _ in
            guard let info else { return }
            Unmanaged<FolderWatcher>.fromOpaque(info).takeUnretainedValue().changed()
        }
        // The first event comes at once; the rest of a burst is held for this long and sent together.
        let burst: CFTimeInterval = 0.3
        guard let stream = FSEventStreamCreate(
            nil, callback, &context, [folder.path] as CFArray,
            FSEventStreamEventId(kFSEventStreamEventIdSinceNow), burst,
            FSEventStreamCreateFlags(kFSEventStreamCreateFlagNoDefer)) else { return nil }
        FSEventStreamSetDispatchQueue(stream, .main)
        guard FSEventStreamStart(stream) else {
            FSEventStreamInvalidate(stream)
            FSEventStreamRelease(stream)
            return nil
        }
        self.stream = stream
    }

    deinit {
        guard let stream else { return }
        FSEventStreamStop(stream)
        FSEventStreamInvalidate(stream)
        FSEventStreamRelease(stream)
    }
}
