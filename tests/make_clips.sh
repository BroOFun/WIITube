#!/bin/sh
# Generates short clips shaped like YouTube's DASH streams (fragmented MP4 with sidx):
# 426x240 H.264 Main @ 25 fps with B-frames, and AAC-LC 44.1 kHz stereo.
set -eu
OUT=$1
mkdir -p "$OUT"
FRAG=+frag_keyframe+empty_moov+default_base_moof+global_sidx
ffmpeg -hide_banner -loglevel error -y -f lavfi -i testsrc2=size=426x240:rate=25 -t 3 \
    -c:v libx264 -profile:v main -level 2.1 -pix_fmt yuv420p \
    -colorspace bt709 -color_primaries bt709 -color_trc bt709 -bf 2 -g 50 \
    -movflags $FRAG -an "$OUT/video.mp4"
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "sine=frequency=440:sample_rate=44100" \
    -f lavfi -i "sine=frequency=660:sample_rate=44100" -filter_complex amerge=inputs=2 -t 3 \
    -c:a aac -b:a 128k -movflags $FRAG -frag_duration 500000 "$OUT/audio.m4a"
ffmpeg -hide_banner -loglevel error -y -i "$OUT/video.mp4" -frames:v 1 -f rawvideo -pix_fmt rgb24 "$OUT/ref.rgb"
