#!/bin/sh
# End-to-end check against the live YouTube site: search, stream lookup, 240p download.
# Usage: sh tests/youtube_smoke.sh [VIDEO_ID]   (run from the repo root after "make")
# Exit status is the number of failed steps.
set -u

CLI=${CLI:-./wiitube-cli}
VIDEO=${1:-dQw4w9WgXcQ}
OUT=${OUT:-test-output}
fail=0

step() {
    name=$1
    shift
    echo "=== $name: $*"
    if "$@"; then
        echo "--> $name: OK"
    else
        echo "--> $name: FAILED"
        fail=$((fail + 1))
    fi
    echo
}

mkdir -p "$OUT"
echo "date:   $(date -u '+%Y-%m-%d %H:%M UTC')"
echo "commit: $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
echo

step search "$CLI" search wii sports
step info "$CLI" info "$VIDEO"
step get "$CLI" get "$VIDEO" -q 240 -o "$OUT/test"

if command -v ffprobe >/dev/null 2>&1; then
    for f in "$OUT"/test.*; do
        [ -f "$f" ] || continue
        step "decode-check $(basename "$f")" ffprobe -v error \
            -show_entries stream=codec_name,width,height,sample_rate -of compact=p=0 "$f"
    done
fi

echo "summary: $fail step(s) failed"
exit "$fail"
