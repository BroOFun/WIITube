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

# Seeking the way the Wii does it: header + data from the seek-index piece containing 60 s.
# The first video and audio timestamps must match the index (they come from the tfdt boxes).
seek_check() {
    exp_v=$(sed -n 's/^expect: video \([0-9.]*\) audio.*/\1/p' "$OUT/seek.txt")
    exp_a=$(sed -n 's/^expect: video [0-9.]* audio \([0-9.]*\)/\1/p' "$OUT/seek.txt")
    got_v=$(ffprobe -v error -select_streams v:0 -show_entries frame=pts_time -read_intervals '%+#1' -of csv=p=0 "$OUT/seek.video.mp4" | head -1)
    got_a=$(ffprobe -v error -select_streams a:0 -show_entries packet=pts_time -read_intervals '%+#1' -of csv=p=0 "$OUT/seek.audio.m4a" | head -1)
    echo "video first frame at $got_v s (index says $exp_v), audio at $got_a s (index says $exp_a)"
    awk -v a="$got_v" -v b="$exp_v" -v c="$got_a" -v d="$exp_a" 'BEGIN { exit !(a != "" && c != "" && a - b < 0.5 && b - a < 0.2 && c - d < 0.2 && d - c < 0.2) }'
}
step seek sh -c "\"$CLI\" seek \"$VIDEO\" 60 -q 240 -o \"$OUT/seek\" | tee \"$OUT/seek.txt\""
if command -v ffprobe >/dev/null 2>&1 && [ -f "$OUT/seek.video.mp4" ]; then
    step seek-timestamps seek_check
fi
step captions "$CLI" captions "$VIDEO" 20

echo "summary: $fail step(s) failed"
exit "$fail"
