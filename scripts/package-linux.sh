#!/usr/bin/env bash
set -euo pipefail

source_dir=${1:?source directory is required}
stage_dir=${2:?stage directory is required}
version=${3:?version is required}
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "${stage_dir}"
cmake --install "${source_dir}" --prefix "${stage_dir}"
tar --zstd -cf "pmxer-${version}-linux-x86_64.tar.zst" -C "${stage_dir}" .
if command -v appimagetool >/dev/null 2>&1; then
    mkdir -p "${stage_dir}/AppDir/usr/bin"
    cp "${stage_dir}/bin/pmxer" "${stage_dir}/AppDir/usr/bin/pmxer"
    mkdir -p "${stage_dir}/AppDir/usr/share/applications" "${stage_dir}/AppDir/usr/share/icons/hicolor/256x256/apps"
    cp -R "${stage_dir}/share/pmxer" "${stage_dir}/AppDir/usr/share/"
    cp "${root_dir}/packaging/AppRun" "${stage_dir}/AppDir/AppRun"
    cp "${root_dir}/packaging/pmxer.desktop" "${stage_dir}/AppDir/usr/share/applications/pmxer.desktop"
    cp "${root_dir}/packaging/pmxer.svg" "${stage_dir}/AppDir/pmxer.svg"
    cp "${root_dir}/packaging/pmxer.svg" "${stage_dir}/AppDir/usr/share/icons/hicolor/256x256/apps/pmxer.svg"
    chmod +x "${stage_dir}/AppDir/AppRun"
    appimagetool "${stage_dir}/AppDir" "pmxer-${version}-linux-x86_64.AppImage"
fi
