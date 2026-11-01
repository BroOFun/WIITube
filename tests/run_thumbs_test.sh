#!/bin/sh
# Serves test JPEGs on localhost and runs the thumbnail test against them.
# usage: sh tests/run_thumbs_test.sh BUILD_DIR
set -eu
BUILD=$1
DIR="$BUILD/thumbs-www"
PORT=8765
mkdir -p "$DIR"
for c in red green blue white; do
    ffmpeg -hide_banner -loglevel error -y -f lavfi -i "color=c=$c:size=320x180" -frames:v 1 "$DIR/$c.jpg"
done
python3 -I -m http.server "$PORT" --protocol HTTP/1.1 --bind 127.0.0.1 --directory "$DIR" >/dev/null 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT
sleep 1
"$BUILD/test_thumbs" "http://127.0.0.1:$PORT"
