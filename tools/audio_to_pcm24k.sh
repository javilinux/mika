#!/usr/bin/env bash
set -euo pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 <input_audio_file> <output_pcm_file>"
    echo "Example: $0 audio.ogg data/sfx/custom_24k.pcm"
    exit 1
fi

INPUT="$1"
OUTPUT="$2"

ffmpeg -y -i "$INPUT" -f s16le -acodec pcm_s16le -ar 24000 -ac 1 "$OUTPUT"
echo "OK: Converted $INPUT -> $OUTPUT (PCM 16-bit 24kHz mono)"
