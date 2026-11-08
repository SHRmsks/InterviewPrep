#!/bin/bash
# Builds dist/InterviewPrep.dmg with the usual drag-to-Applications layout.
# Run with: make dmg
set -euo pipefail
cd "$(dirname "$0")/.."

work=$(mktemp -d)
cp -R dist/InterviewPrep.app "$work/"
ln -s /Applications "$work/Applications"
rm -f dist/InterviewPrep.dmg
hdiutil create -volname InterviewPrep -srcfolder "$work" -ov -format UDZO -quiet dist/InterviewPrep.dmg
rm -rf "$work"
echo "dist/InterviewPrep.dmg, $(du -sh dist/InterviewPrep.dmg | cut -f1)"
