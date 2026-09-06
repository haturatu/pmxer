#!/usr/bin/env bash
set -euo pipefail

source_dir=${1:?source directory is required}
stage_dir=${2:?stage directory is required}
version=${3:?version is required}
mkdir -p "${stage_dir}"
cmake --install "${source_dir}" --prefix "${stage_dir}"
tar --zstd -cf "pmxer-${version}-linux-x86_64.tar.zst" -C "${stage_dir}" .
if command -v appimagetool >/dev/null 2>&1; then
  mkdir -p "${stage_dir}/AppDir/usr/bin"
  cp "${stage_dir}/bin/pmxer" "${stage_dir}/AppDir/usr/bin/pmxer"
  appimagetool "${stage_dir}/AppDir" "pmxer-${version}-linux-x86_64.AppImage"
fi

