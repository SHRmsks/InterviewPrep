// swift-tools-version:5.9
// The Mac app: SwiftUI on top of the same C++ core the website compiles to
// WebAssembly. Build it with `make mac`, which also wraps it into a .app.
import PackageDescription

let package = Package(
    name: "InterviewPrep",
    platforms: [.macOS(.v14)],
    targets: [
        .target(
            name: "CCore",
            path: "core",
            publicHeadersPath: ".",
            // The core says `using namespace std;` and calls move() without std::,
            // which clang warns about on every call. This turns just that warning off.
            cxxSettings: [.unsafeFlags(["-Wno-unqualified-std-cast-call"])]
        ),
        .executableTarget(
            name: "InterviewPrep",
            dependencies: ["CCore"],
            path: "mac/Sources/InterviewPrep"
        ),
    ],
    cxxLanguageStandard: .cxx17
)
