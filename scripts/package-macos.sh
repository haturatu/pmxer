#!/usr/bin/env bash
set -euo pipefail

source_dir=${1:?source directory is required}
version=${2:?version is required}
architecture=${3:?architecture is required}
stage_dir=${4:?stage directory is required}
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
app_dir="${stage_dir}/pmxer.app"
mkdir -p "${app_dir}/Contents/MacOS" "${app_dir}/Contents/Resources"
cmake --install "${source_dir}" --prefix "${stage_dir}/install"
cp "${stage_dir}/install/bin/pmxer" "${app_dir}/Contents/MacOS/pmxer"
cp -R "${stage_dir}/install/share/pmxer/assets" "${app_dir}/Contents/Resources/"
if [[ -d "${stage_dir}/install/share/pmxer/shaders" ]]; then
  cp -R "${stage_dir}/install/share/pmxer/shaders" "${app_dir}/Contents/Resources/"
fi
cp "${root_dir}/cmake/Info.plist" "${app_dir}/Contents/Info.plist"
if [[ -n "${PMXER_MACOS_SIGNING_IDENTITY:-}" ]]; then
  codesign --deep --force --options runtime --sign "${PMXER_MACOS_SIGNING_IDENTITY}" "${app_dir}"
fi
hdiutil create -volname "pmxer" -srcfolder "${app_dir}" -ov -format UDZO "pmxer-${version}-macos-${architecture}.dmg"
if [[ -n "${PMXER_APPLE_ID:-}" || -n "${PMXER_APPLE_TEAM_ID:-}" || -n "${PMXER_APPLE_PASSWORD:-}" ]]; then
  : "${PMXER_APPLE_ID:?PMXER_APPLE_ID is required for notarization}"
  : "${PMXER_APPLE_TEAM_ID:?PMXER_APPLE_TEAM_ID is required for notarization}"
  : "${PMXER_APPLE_PASSWORD:?PMXER_APPLE_PASSWORD is required for notarization}"
  xcrun notarytool submit "pmxer-${version}-macos-${architecture}.dmg" \
    --apple-id "${PMXER_APPLE_ID}" --team-id "${PMXER_APPLE_TEAM_ID}" \
    --password "${PMXER_APPLE_PASSWORD}" --wait
  xcrun stapler staple "pmxer-${version}-macos-${architecture}.dmg"
fi
