#!/usr/bin/env bash
# Local end-to-end smoke test: relay + host + two guests (fake coordinates), everything on 127.0.0.1.
# Usage: scripts/e2e-local.sh [seconds]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${BIN:-$ROOT/build/linux/bin/dscoop_testclient}"
DUR="${1:-8}"
OUT="${OUT:-$ROOT/build/e2e}"
mkdir -p "$OUT"
python3 "$ROOT/relay/relay.py" --control-port 27000 --port-range 27100-27120 --stats-interval 0 >"$OUT/relay.log" 2>&1 &
RELAY=$!
trap 'kill $RELAY 2>/dev/null || true' EXIT
sleep 0.5
"$BIN" host --relay 127.0.0.1:27000 --nick Host --motion static --duration "$((DUR+3))" --quiet >"$OUT/host.log" 2>&1 &
HOST=$!
CODE=""
for _ in $(seq 1 40); do
  CODE=$(grep -oE "room [A-Z0-9]{6} created" "$OUT/host.log" | head -1 | awk '{print $2}' || true)
  [ -n "$CODE" ] && break
  sleep 0.25
done
[ -n "$CODE" ] || { echo "no room code — relay or host failed"; cat "$OUT/host.log"; exit 1; }
echo "room code: $CODE"
"$BIN" join --relay 127.0.0.1:27000 --code "$CODE" --nick Fragile --motion circle --duration "$DUR" --chat "privet" >"$OUT/g1.log" 2>&1 &
"$BIN" join --relay 127.0.0.1:27000 --code "$CODE" --nick Deadman --motion walk --speed 5 --duration "$DUR" >"$OUT/g2.log" 2>&1 &
wait $HOST
# Player ids depend on who connected first, so match on nicknames only.
grep -q "'Fragile' joined" "$OUT/host.log" || { echo "FAIL: Fragile did not join"; exit 1; }
grep -q "'Deadman' joined" "$OUT/host.log" || { echo "FAIL: Deadman did not join"; exit 1; }
grep -Eq "\[chat\] #[0-9]+: privet" "$OUT/g2.log" || { echo "FAIL: chat not relayed"; exit 1; }
grep -Eq "#[0-9]+ Deadman .*pos=" "$OUT/g1.log" || { echo "FAIL: Fragile never saw Deadman"; exit 1; }
grep -Eq "#[0-9]+ Fragile .*pos=" "$OUT/g2.log" || { echo "FAIL: Deadman never saw Fragile"; exit 1; }
echo "OK — logs in $OUT"
tail -4 "$OUT/g1.log"
