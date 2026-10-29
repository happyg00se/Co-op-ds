#!/usr/bin/env bash
# Linux: protocol/session/sigscan unit tests + dscoop_testclient. Needs libenet-dev, libspdlog-dev, nlohmann-json3-dev.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake -S "$ROOT" -B "$ROOT/build/linux" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DDSCOOP_BUILD_LOADER=OFF -DDSCOOP_BUILD_PLUGIN=OFF
cmake --build "$ROOT/build/linux"
(cd "$ROOT/build/linux" && ctest --output-on-failure)
(cd "$ROOT/relay" && python3 -m pytest -q)
