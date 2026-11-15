#!/bin/bash
# Builds dist/InterviewPrep.app, universal (Apple Silicon and Intel), ad-hoc signed.
# Run with: make mac
set -euo pipefail
cd "$(dirname "$0")/.."

APP=dist/InterviewPrep.app

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

swift build -c release --arch arm64 --arch x86_64 2>&1 | grep -E 'error:|Build complete|Compiling InterviewPrep' | grep -v 'SwiftCompile normal' || true
BIN="$(swift build -c release --arch arm64 --arch x86_64 --show-bin-path)/InterviewPrep"
test -f "$BIN"

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
 