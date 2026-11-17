#!/bin/bash
# Builds dist/InterviewPrep.app, universal (Apple Silicon and Intel), ad-hoc signed.
# Run with: make mac
set -euo pipefail
cd "$(dirname "$0")/.."

APP=dist/InterviewPrep.app

# Everything below comes with Apple's Command Line Tools; the full Xcode app isn't
# needed. Check for them up front rather than failing halfway through.
if ! xcode-select -p >/dev/null 2>&1; then
  echo "The Command Line Tools are missing. A window will offer to install them;"
  echo "run make mac again once that finishes."
  xcode-select --install 2>/dev/null || true
  exit 1
fi
missing=""
for tool in swift clang++ sips iconutil codesign lipo strip; do
  command -v "$tool" >/dev/null || missing="$missing $tool"
done
if [ -n "$missing" ]; then
  echo "Missing:$missing. Reinstall the Command Line Tools, then try again:"
  echo "  sudo rm -rf \"$(xcode-select -p)\" && xcode-select --install"
  exit 1
fi
# A half-updated install has a swift that can't read Package.swift.
if ! swift package dump-package >/dev/null 2>&1; then
  echo "Swift can't read Package.swift, which usually means the Command Line Tools"
  echo "are half-updated. Reinstall them, then try again:"
  echo "  sudo rm -rf \"$(xcode-select -p)\" && xcode-select --install"
  exit 1
fi

# The icon only needs drawing once.
if [ ! -f mac/InterviewPrep.icns ]; then
  work=$(mktemp -d)
  mkdir "$work/InterviewPrep.iconset"
  swift scripts/make_icon.swift "$work/icon.png"
  for s in 16 32 128 256 512; do
    sips -z $s $s "$work/icon.png" --out "$work/InterviewPrep.iconset/icon_${s}x${s}.png" >/dev/null
    sips -z $((s * 2)) $((s * 2)) "$work/icon.png" --out "$work/InterviewPrep.iconset/icon_${s}x${s}@2x.png" >/dev/null
  done
  iconutil -c icns "$work/InterviewPrep.iconset" -o mac/InterviewPrep.icns
  rm -rf "$work"
fi

# One architecture at a time, then glued together: asking swift for both at once
# hands the build to Xcode's build system, which the Command Line Tools don't have.
slices=""
for arch in arm64 x86_64; do
  if ! log=$(swift build -c release --arch $arch 2>&1); then
    echo "$log" | grep -E 'error:' || echo "$log"
    exit 1
  fi
  echo "$arch: $(echo "$log" | grep 'Build complete' || echo built)"
  slice="$(swift build -c release --arch $arch --show-bin-path)/InterviewPrep"
  test -f "$slice"
  slices="$slices $slice"
done
BIN=$(mktemp -d)/InterviewPrep
lipo -create $slices -output "$BIN"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/InterviewPrep"
strip -x "$APP/Contents/MacOS/InterviewPrep" 2>/dev/null || true
cp mac/Info.plist "$APP/Contents/Info.plist"
cp mac/InterviewPrep.icns "$APP/Contents/Resources/InterviewPrep.icns"
cp data/resources.json "$APP/Contents/Resources/resources.json"

# The app carries the command-line tool, which is also the coach's MCP server. It
# can't sit beside the app's own binary: Macs ignore case in file names by
# default, so interviewprep and InterviewPrep would be one file.
mkdir -p "$APP/Contents/Helpers"
clang++ -std=c++17 -O2 -Wno-unqualified-std-cast-call -arch arm64 -arch x86_64 -mmacosx-version-min=14.0 \
    $(ls core/*.cpp | grep -v capi.cpp) cli/main.cpp cli/mcp.cpp -o "$APP/Contents/Helpers/interviewprep"
strip -x "$APP/Contents/Helpers/interviewprep" 2>/dev/null || true

# Ad-hoc signature: runs on this Mac. See scripts/notarize.sh for other Macs.
codesign --force --sign - "$APP/Contents/Helpers/interviewprep"
codesign --force --sign - "$APP"
echo "$APP, $(du -sh "$APP" | cut -f1), $(lipo -archs "$APP/Contents/MacOS/InterviewPrep")"
 