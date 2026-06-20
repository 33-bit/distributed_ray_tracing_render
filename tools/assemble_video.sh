#!/usr/bin/env bash
# Member D — assemble a directory of PPM frames into an MP4 with ffmpeg.
#
#   tools/assemble_video.sh [frames_dir] [out.mp4] [fps]
#
# Defaults: frames -> output/render.mp4 at 24 fps. The scale filter rounds the
# dimensions down to even numbers because the yuv420p pixel format libx264 needs
# requires even width/height.
set -euo pipefail

FRAMES="${1:-frames}"
OUT="${2:-output/render.mp4}"
FPS="${3:-24}"

if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "error: ffmpeg not found (brew install ffmpeg)" >&2
    exit 1
fi
if ! ls "$FRAMES"/frame_*.ppm >/dev/null 2>&1; then
    echo "error: no frame_*.ppm in $FRAMES" >&2
    exit 1
fi

mkdir -p "$(dirname "$OUT")"
ffmpeg -y -loglevel warning \
    -framerate "$FPS" \
    -i "$FRAMES/frame_%04d.ppm" \
    -vf "scale=trunc(iw/2)*2:trunc(ih/2)*2" \
    -c:v libx264 -pix_fmt yuv420p -crf 18 \
    "$OUT"

echo "wrote $OUT ($(ls "$FRAMES"/frame_*.ppm | wc -l | tr -d ' ') frames @ ${FPS}fps)"
