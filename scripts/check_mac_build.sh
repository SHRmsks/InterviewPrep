#!/bin/bash
# Checks that this Mac can run `make mac`, and says how to get what is missing.
# Run with: make mac-check    (`make mac` runs it first)
#
# What the Mac build needs:
#
#   Xcode, selected       swift, clang++, the macOS SDK and codesign. The Command
#                         Line Tools alone are not enough: the app is built for
#                         both Apple Silicon and Intel, and swift build hands
#                         that to Xcode's own build system.
#   data/resources.json   comes with the repository. Only when it is older than
#                         the files it is built from does `make mac` rebuild it,
#                         and that takes python3 and data/raw/, which
#                         scripts/get_data.sh downloads.
#
# Nothing else is downloaded: the Swift package has no outside dependencies.
#
# Every check runs, so one pass lists everything that is missing. It builds
# nothing and changes nothing. Exits 1 when something is missing.
set -u
cd "$(dirname "$0")/.."

missing=0

ok() { printf '  ok       %s\n' "$1"; }

# A heading, then the lines under it: what to do about it.
report() {
  printf '  %-8s %s\n' "$1" "$2"
  shift 2
  for line in "$@"; do printf '           %s\n' "$line"; done
}

miss() {
  missing=$((missing + 1))
  report MISSING "$@"
}

note() { report note "$@"; }

finish() {
  echo
  if [ "$missing" -eq 0 ]; then
    echo "Ready: \`make mac\` can build."
    exit 0
  fi
  if [ "$missing" -eq 1 ]; then
    echo "1 thing is missing, so nothing was built."
  else
    echo "$missing things are missing, so nothing was built."
  fi
  exit 1
}

# Whether version $1, such as 6.4.2 or 27, is at least $2.$3.
at_least() {
  local major=${1%%.*} minor=0
  if [ "$major" != "$1" ]; then
    minor=${1#*.}
    minor=${minor%%.*}
  fi
  [ "$major" -gt "$2" ] 2>/dev/null && return 0
  [ "$major" -eq "$2" ] 2>/dev/null && [ "$minor" -ge "$3" ] 2>/dev/null
}

# The first line of a tool's complaint that says what went wrong.
complaint() { grep -m1 -i -E 'error|fatal|license' "$1" 2>/dev/null || head -1 "$1"; }

echo "What \`make mac\` needs on this Mac:"
echo

# ── A Mac ─────────────────────────────────────────────────────────────────
if [ "$(uname -s)" != Darwin ]; then
  miss "a Mac" "The Mac app builds only on macOS. \`make cli\` and \`make web\` build anywhere."
  finish
fi

# What the project itself asks for, read from its files so this stays true when they change.
needs_swift=$(sed -n '1s/.*swift-tools-version:[ ]*\([0-9][0-9.]*\).*/\1/p' Package.swift 2>/dev/null)
needs_macos=$(sed -n 's/.*\.macOS(\.v\([0-9][0-9]*\)).*/\1/p' Package.swift 2>/dev/null | head -1)
if [ -z "$needs_swift" ] || [ -z "$needs_macos" ] || [ ! -f mac/Info.plist ] || [ ! -x scripts/build_mac.sh ]; then
  miss "the project's own files" \
       "Package.swift, mac/Info.plist or scripts/build_mac.sh is missing or changed shape." \
       "\`git status\` shows what differs from the repository."
  finish
fi

# ── Xcode ─────────────────────────────────────────────────────────────────
# Asked before any tool is run: with no developer tools at all, swift and
# clang++ are stand-ins that open Apple's install window instead of answering.
developer=$(xcode-select -p 2>/dev/null)
if [ -z "$developer" ] || [ ! -d "$developer" ]; then
  miss "Xcode" \
       "No developer tools are set up on this Mac." \
       "Install Xcode from the App Store, open it once so it finishes setting up, then run this again."
  finish
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Whether swift build can make one program for both kinds of Mac here, tried on a package of one line.
builds_for_both() {
  mkdir -p "$work/both/Sources/both" || return 1
  printf '// swift-tools-version:5.9\nimport PackageDescription\nlet package = Package(name: "both", targets: [.executableTarget(name: "both")])\n' \
    > "$work/both/Package.swift"
  printf 'print("ok")\n' > "$work/both/Sources/both/main.swift"
  swift build --package-path "$work/both" --arch arm64 --arch x86_64 > "$work/both.log" 2>&1
}

xcode=$(xcodebuild -version 2>/dev/null | sed -n 's/^Xcode //p' | head -1)
if [ -n "$xcode" ]; then
  ok "Xcode $xcode, at $developer"
elif builds_for_both; then
  # Not so with the tools of 2026, but nothing here should call a thing missing that isn't.
  ok "the developer tools at $developer build for both Apple Silicon and Intel without Xcode"
else
  # The Command Line Tools are selected. Xcode may still be on the disk.
  installed=""
  for app in /Applications/Xcode*.app; do
    if [ -d "$app/Contents/Developer" ]; then
      installed="$app/Contents/Developer"
      break
    fi
  done
  if [ -n "$installed" ]; then
    miss "Xcode, selected" \
         "Xcode is on this Mac, but the Command Line Tools are the ones in use" \
         "($developer), and they can't build for both Apple Silicon and Intel." \
         "Either select Xcode, which asks for your password:" \
         "    sudo xcode-select -s \"$installed\"" \
         "or use it for this build only:" \
         "    DEVELOPER_DIR=\"$installed\" make mac"
  else
    miss "Xcode" \
         "Only the Command Line Tools are here ($developer)." \
         "They can't build for both Apple Silicon and Intel: swift build stops at a missing \"xcbuild\"." \
         "Install Xcode from the App Store, open it once so it finishes setting up, then run this again."
  fi
fi

# ── Swift, the macOS SDK, C++ ─────────────────────────────────────────────
swift --version > "$work/swift.log" 2>&1
swift_version=$(sed -n 's/.*Swift version \([0-9][0-9.]*\).*/\1/p' "$work/swift.log" | head -1)
if [ -z "$swift_version" ]; then
  miss "a Swift compiler that runs" "swift --version said: $(complaint "$work/swift.log")"
elif at_least "$swift_version" "${needs_swift%%.*}" "${needs_swift#*.}"; then
  ok "Swift $swift_version (Package.swift asks for $needs_swift or newer)"
else
  miss "Swift $needs_swift or newer" \
       "This one is $swift_version. A newer Swift comes with a newer Xcode: update Xcode in the App Store."
fi

sdk=$(xcrun --sdk macosx --show-sdk-version 2>"$work/sdk.log")
if [ -z "$sdk" ]; then
  miss "the macOS SDK" \
       "xcrun said: $(complaint "$work/sdk.log")" \
       "Opening Xcode once lets it finish installing its parts."
elif at_least "$sdk" "$needs_macos" 0; then
  ok "macOS SDK $sdk (the app is built for macOS $needs_macos or newer)"
else
  miss "the macOS $needs_macos SDK or newer" \
       "This one is $sdk. A newer SDK comes with a newer Xcode: update Xcode in the App Store."
fi

# The command-line tool inside the app is compiled like this, and then signed.
printf '#include <string_view>\nint main() { std::string_view text = "ok"; return text.size() == 2 ? 0 : 1; }\n' > "$work/check.cpp"
if clang++ -std=c++17 -arch arm64 -arch x86_64 "-mmacosx-version-min=$needs_macos.0" "$work/check.cpp" -o "$work/check" \
     > "$work/clang.log" 2>&1; then
  case "$(lipo -archs "$work/check" 2>/dev/null)" in
    *arm64*x86_64* | *x86_64*arm64*)
      if codesign --force --sign - "$work/check" > "$work/codesign.log" 2>&1; then
        ok "clang++ builds C++17 for Apple Silicon and Intel, and codesign signs the result"
      else
        miss "codesign" "It couldn't sign a test program: $(complaint "$work/codesign.log")"
      fi
      ;;
    *)
      miss "a C++ compiler for both Apple Silicon and Intel" "clang++ built a test program for one kind of Mac only."
      ;;
  esac
else
  miss "a C++17 compiler for both Apple Silicon and Intel" "clang++ said: $(complaint "$work/clang.log")"
fi

# ── The problem list ──────────────────────────────────────────────────────
# `make -q` answers what `make mac` is about to do: 0 when the file is up to date.
if [ -f data/resources.json ] && make -q data/resources.json > /dev/null 2>&1; then
  ok "data/resources.json is up to date, so nothing is rebuilt or downloaded"
else
  if [ -f data/resources.json ]; then
    note "data/resources.json is older than the files it is built from, so \`make mac\` rebuilds it first." \
         "To keep the problem list as it is instead: touch data/resources.json"
  else
    note "data/resources.json is missing, so \`make mac\` builds it first."
  fi
  if python3 -I -c 'import sys' > "$work/python.log" 2>&1; then
    ok "$(python3 -I -c 'import sys; print("python3 " + sys.version.split()[0])'), to rebuild it"
  else
    miss "python3" "It comes with Xcode. python3 said: $(complaint "$work/python.log")"
  fi
  if [ -f data/raw/lc-mini/leetcode_questions_mini.json ] && [ -f data/raw/rating/data.json ] &&
     [ -n "$(ls data/raw/companies 2>/dev/null | head -1)" ]; then
    ok "the LeetCode labels in data/raw/, to rebuild it from"
  else
    miss "the LeetCode labels in data/raw/" \
         "Download them once from GitHub, about 17 MB on disk: scripts/get_data.sh"
  fi
fi

# ── The copy this build replaces ──────────────────────────────────────────
# Not a reason to stop. The build deletes dist/InterviewPrep.app and makes it anew.
# `comm` is each program's own path, with no arguments, so this search can't find itself.
if ps -axww -o comm= | grep -qxF "$PWD/dist/InterviewPrep.app/Contents/MacOS/InterviewPrep"; then
  note "InterviewPrep is running from dist/InterviewPrep.app, the copy this build replaces." \
       "It keeps running the old build until you quit it and open it again."
fi

finish
