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
cp "${root_dir}/cmake/Info.plist" "${app_dir}/Contents/Info.plist"
hdiutil create -volname "pmxer" -srcfolder "${app_dir}" -ov -format UDZO "pmxer-${version}-macos-${architecture}.dmg"
