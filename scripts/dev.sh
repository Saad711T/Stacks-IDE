#!/usr/bin/env bash
# Development helper: builds stacksd, starts it, and serves the web UI.
set -euo pipefail

cd "$(dirname "$0")/.."

echo "▶ Building stacksd…"
cmake -S core -B core/build -DCMAKE_BUILD_TYPE=Release
cmake --build core/build --config Release -j

BIN=core/build/stacksd
[ -f "$BIN" ] || BIN=core/build/Release/stacksd.exe

echo "▶ Starting daemon"
"$BIN" "$(pwd)/workspace" &
DAEMON_PID=$!
trap 'kill $DAEMON_PID 2>/dev/null || true' EXIT

echo "▶ Serving web UI on http://localhost:8000"
cd web
python3 -m http.server 8000
