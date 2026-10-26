#!/usr/bin/env bash
# e2e.sh — End-to-end checks for the Kaleidoscope screen saver.
#
# Launches each run mode and renders the live widget to a PNG via the
# --snapshot hook, which also reports the demo's internal phase. Needs no
# screen-recording permission and no display server, so it runs in CI.
#
# Assertions are on the reported phase (exact) plus a "not blank" pixel check.
# Pixel heuristics alone cannot separate the intro from a busy effect — both are
# large, colourful, black-dominated frames — so the phase is the real signal.
#
# All runs are pinned to the built-in display via --screen built-in so the
# test never covers the user's other monitors.
#
# Usage:  ./tools/e2e.sh [build-dir]
set -uo pipefail

BUILD="${1:-build}"
BIN="$BUILD/Kaleidoscope.app/Contents/MacOS/Kaleidoscope"
[ -x "$BIN" ] || BIN="$BUILD/Kaleidoscope"
[ -x "$BIN" ] || { echo "FAIL: binary not found in $BUILD (build it first)"; exit 1; }

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
pass=0; fail=0
ok()  { echo "  PASS  $1"; pass=$((pass+1)); }
bad() { echo "  FAIL  $1"; fail=$((fail+1)); }

# Launch a mode, snapshot after $1 ms; echo "<path> <phase>".
snap() { # ms name args...
  local ms="$1" name="$2"; shift 2
  local png="$OUT/$name.png"
  rm -f "$png"
  local out
  out="$("$BIN" "$@" --screen built-in --snapshot "$png@$ms" 2>&1)"
  local phase
  phase="$(sed -n 's/.*phase= \([a-z]*\).*/\1/p' <<<"$out")"
  [ -f "$png" ] && echo "$png ${phase:-none}" || echo "none ${phase:-none}"
}

# colours + dominant-colour share
stats() { python3 - "$1" <<'PY'
import sys
from PIL import Image
from collections import Counter
im = Image.open(sys.argv[1]).convert("RGB")
c = Counter(im.getdata())
print(len(c), f"{c.most_common(1)[0][1] / (im.width * im.height):.3f}")
PY
}

# A frame is "blank" only if it is essentially one flat colour. Several effects
# draw sparse shapes on black and are legitimately >90% black, so the threshold
# has to leave room for them; a truly blank frame is a single colour at ~100%.
assert_not_blank() { # file label
  local s; s="$(stats "$1" 2>/dev/null)"
  local n d; read -r n d <<<"$s"
  if [ "${n:-0}" -gt 8 ] && awk "BEGIN{exit !(${d:-1} < 0.995)}"; then
    ok "$2 is rendering ($n colours, dominant share $d)"
  else
    bad "$2 looks blank ($n colours, dominant share $d)"
  fi
}

echo "=== Kaleidoscope E2E ==="
echo "binary: $BIN"

echo "[1] windowed mode plays the intro"
# The palette cascade takes ~15s to develop (faithful to the DOS original:
# "starts black, colours cascade in ~15s, any key skips"), so sample past it.
read -r P PH <<<"$(snap 15000 intro -w)"
if [ "$P" != none ]; then
  assert_not_blank "$P" "intro frame"
  if [ "$PH" = intro ]; then
    ok "demo is in the intro phase"
  else
    bad "expected intro phase, got '$PH'"
  fi
  read -r P2 PH2 <<<"$(snap 20000 intro_late -w)"
  if [ "$P2" != none ] && [ "$PH2" = intro ]; then
    ok "intro persists past 20s (waits for a key, does not auto-advance)"
  else
    bad "intro did not persist (phase '$PH2')"
  fi
  if [ "$P" != none ] && [ "$P2" != none ] && ! cmp -s "$P" "$P2"; then
    ok "palette keeps cycling while the intro waits"
  else
    bad "intro frame did not change (animation stalled)"
  fi
else
  bad "no snapshot produced in windowed mode"
fi

echo "[2] screen-saver mode skips the intro"
read -r P PH <<<"$(snap 8000 saver -s)"
if [ "$P" != none ]; then
  assert_not_blank "$P" "screen saver frame"
  if [ "$PH" != intro ]; then
    ok "screen saver is in an effect phase ('$PH'), never the intro"
  else
    bad "screen saver is showing the intro, which it must never do"
  fi
else
  bad "no snapshot produced in screen-saver mode"
fi

echo "[3] an effect flag bypasses the intro"
read -r P PH <<<"$(snap 8000 flag -w /O2)"
if [ "$P" != none ]; then
  assert_not_blank "$P" "/O2 frame"
  if [ "$PH" != intro ]; then
    ok "/O2 starts on an effect ('$PH'), not the intro"
  else
    bad "/O2 still shows the intro"
  fi
else
  bad "no snapshot produced with /O2"
fi

echo "[4] the demo does not quit on its own"
# Regression guard: a POSIX snapshot path used to be parsed by the core as the
# DOS "/T" timeout flag, making the demo quit after a few seconds. Run several
# times because the failure was intermittent.
selfquit=0
for i in 1 2 3 4 5 6; do
  read -r P PH <<<"$(snap 9000 selfquit$i -s)"
  [ "$P" = none ] && selfquit=$((selfquit+1))
done
if [ "$selfquit" -eq 0 ]; then
  ok "survived 6 consecutive 9s runs without self-quitting"
else
  bad "$selfquit/6 runs exited before the snapshot deadline"
fi

echo
echo "=== $pass passed, $fail failed ==="
[ "$fail" -eq 0 ]