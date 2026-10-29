#!/usr/bin/env bash
# Cross-compile the Windows binaries (version.dll, DSCoop.dll, dscoop_testclient.exe) from Linux.
# Needs: apt install mingw-w64 cmake ninja-build libspdlog-dev libfmt-dev nlohmann-json3-dev
# and an ImGui source tree (IMGUI_DIR, default: downloaded via FetchContent if network allows).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAGE="${STAGE:-$ROOT/build/stage-headers}"
mkdir -p "$STAGE/include"
# Header-only copies outside /usr/include so the cross compiler never sees host glibc headers.
for d in spdlog fmt nlohmann; do
  [ -d "/usr/include/$d" ] && rm -rf "$STAGE/include/$d" && cp -r "/usr/include/$d" "$STAGE/include/"
done
EXTRA=()
[ -n "${IMGUI_DIR:-}" ] && EXTRA+=("-DIMGUI_DIR=$IMGUI_DIR")
cmake -S "$ROOT" -B "$ROOT/build/mingw-cross" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchain-mingw-w64.cmake" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}" \
  -DDSCOOP_VENDORED_DEPS=ON -DDSCOOP_BUILD_TESTS=OFF \
  -DSPDLOG_DIR="$STAGE/include" -DFMT_DIR="$STAGE/include" -DNLOHMANN_JSON_DIR="$STAGE/include" \
  "${EXTRA[@]}"
cmake --build "$ROOT/build/mingw-cross"
echo
echo "Artifacts:"
find "$ROOT/build/mingw-cross/bin" -type f \( -name '*.dll' -o -name '*.exe' \) -printf '  %p (%s bytes)\n'
