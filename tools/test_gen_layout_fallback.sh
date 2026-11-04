#!/bin/sh
# Regenerating the layout without SteelSeries GG's file must keep the key positions already
# in src/layoutdata.cpp. Runs on a copy of the tracked files; msi-perkeyrgb is still needed.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
COPY="$(mktemp -d)"
trap 'rm -rf "$COPY"' EXIT
git -C "$ROOT" ls-files -z | (cd "$ROOT" && xargs -0 cp --parents -t "$COPY")
cd "$COPY"
OUT="$(GG_LAYOUT=/nonexistent python3 -I tools/gen_layout.py)"
echo "$OUT"
echo "$OUT" | grep -q "GG layout not found" || { echo "FAIL: the script did not fall back"; exit 1; }
cmp src/layoutdata.cpp "$ROOT/src/layoutdata.cpp"
echo "layout unchanged without GG"
