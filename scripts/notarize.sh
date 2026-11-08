#!/bin/bash
# Signs and notarizes the app so it opens cleanly on other people's Macs.
#
# This needs an Apple Developer ID ($99 a year), which this project does not have
# yet. Set these, then run it after `make dmg`:
#
#   export DEVELOPER_ID="Developer ID Application: Your Name (TEAMID)"
#   export APPLE_ID="you@example.com"
#   export TEAM_ID="TEAMID"
#   export APP_PASSWORD="app-specific password from appleid.apple.com"
#
set -euo pipefail
cd "$(dirname "$0")/.."
: "${DEVELOPER_ID:?Set DEVELOPER_ID}" "${APPLE_ID:?Set APPLE_ID}" "${TEAM_ID:?Set TEAM_ID}" "${APP_PASSWORD:?Set APP_PASSWORD}"

codesign --force --options runtime --timestamp --sign "$DEVELOPER_ID" dist/InterviewPrep.app/Contents/Helpers/interviewprep
codesign --force --options runtime --timestamp --sign "$DEVELOPER_ID" dist/InterviewPrep.app
./scripts/build_dmg.sh
codesign --force --timestamp --sign "$DEVELOPER_ID" dist/InterviewPrep.dmg
xcrun notarytool submit dist/InterviewPrep.dmg --apple-id "$APPLE_ID" --team-id "$TEAM_ID" --password "$APP_PASSWORD" --wait
xcrun stapler staple dist/InterviewPrep.dmg
echo "Notarized. dist/InterviewPrep.dmg opens on any Mac."
