#!/usr/bin/env bash
set -euo pipefail

source_file=${1:?shader source is required}
output_dir=${2:?output directory is required}
work_dir=${RUNNER_TEMP:-/tmp}/pmxer-shader-toolchain
prefix_dir=$work_dir/prefix

mkdir -p "$work_dir" "$prefix_dir" "$output_dir"

clone_at() {
  local repository=$1
  local revision=$2
  local destination=$3
  if [[ ! -d "$destination/.git" ]]; then
    git clone --filter=blob:none --no-checkout "$repository" "$destination"
  fi
  git -C "$destination" fetch --depth 1 origin "$revision"
  git -C "$destination" checkout --detach FETCH_HEAD
}

clone_at https://github.com/libsdl-org/SDL.git \
  f6864924f76e1a0b4abaefc76ae2ed22b1a8916e "$work_dir/window"
cmake -S "$work_dir/window" -B "$work_dir/window-build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix_dir" \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
  -DSDL_UNIX_CONSOLE_BUILD=ON
cmake --build "$work_dir/window-build" --target install

clone_at https://github.com/KhronosGroup/SPIRV-Cross.git \
  83fa691cb8606ca4b3af7f13bfcbedd5668f2a3a "$work_dir/translator"
cmake -S "$work_dir/translator" -B "$work_dir/translator-build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix_dir" \
  -DSPIRV_CROSS_SHARED=OFF -DSPIRV_CROSS_STATIC=ON -DSPIRV_CROSS_CLI=OFF \
  -DSPIRV_CROSS_ENABLE_TESTS=OFF
cmake --build "$work_dir/translator-build" --target install

compiler_archive=$work_dir/compiler.tar.gz
compiler_url=https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2607/linux_dxc_2026_07_29.x86_x64.tar.gz
if [[ ! -f "$compiler_archive" ]]; then
  curl --fail --location --retry 3 "$compiler_url" --output "$compiler_archive"
fi
echo '55665c87824051ed4774ff3280a79ccbbb7d39243b9736ca5e98222134112d54  '"$compiler_archive" | sha256sum --check --status
mkdir -p "$work_dir/compiler"
tar -xf "$compiler_archive" -C "$work_dir/compiler"

clone_at https://github.com/libsdl-org/SDL_shadercross.git \
  1ff05bec573988a98ef9e0260b4da44f512b8367 "$work_dir/offline-compiler"
cmake -S "$work_dir/offline-compiler" -B "$work_dir/offline-compiler-build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$prefix_dir" \
  -DDirectXShaderCompiler_ROOT="$work_dir/compiler" \
  -DSDLSHADERCROSS_DXC=ON -DSDLSHADERCROSS_VENDORED=OFF \
  -DSDLSHADERCROSS_SHARED=OFF -DSDLSHADERCROSS_STATIC=ON \
  -DSDLSHADERCROSS_SPIRVCROSS_SHARED=OFF -DSDLSHADERCROSS_CLI=ON \
  -DSDLSHADERCROSS_CLI_STATIC=ON -DSDLSHADERCROSS_TESTS=OFF -DSDLSHADERCROSS_INSTALL=OFF
cmake --build "$work_dir/offline-compiler-build" --target shadercross

tool=$(find "$work_dir/offline-compiler-build" -type f -name shadercross -perm -111 -print -quit)
test -n "$tool"
export LD_LIBRARY_PATH="$work_dir/compiler/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
for stage in vertex fragment; do
  entry=mainVS
  suffix=vert
  if [[ "$stage" == fragment ]]; then
    entry=mainPS
    suffix=frag
  fi
  "$tool" "$source_file" -s HLSL -d SPIRV -t "$stage" -e "$entry" -o "$output_dir/model.$suffix.spv"
  "$tool" "$source_file" -s HLSL -d DXIL -t "$stage" -e "$entry" -o "$output_dir/model.$suffix.dxil"
  "$tool" "$source_file" -s HLSL -d MSL -t "$stage" -e "$entry" -o "$output_dir/model.$suffix.msl"
done
