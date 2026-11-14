#!/usr/bin/env bash
#
# Builds the two App Store screenshot sizes from raw iPad captures.
#
# Drop the PNGs taken on the device anywhere under tools/app_store_screenshots
# (subfolders included) and run this. For each source it writes, NEXT TO IT:
#   {name}_2420x1668.png   (iPad 11")
#   {name}_2752x2064.png   (iPad 13")
#   gallery_{n}.jpg        (website copy, quality 95)
#
# Sources are never modified, and already generated targets are skipped as
# inputs, so re-running just overwrites the outputs. The folder is gitignored:
# these are submission assets, not repo content.
#
# Geometry (aspect ratio is always preserved, the image is never stretched):
#   scale to the target WIDTH, then
#     - taller than the target  -> align TOP, cut the bottom (a few pixels)
#     - shorter than the target -> center vertically, black bars
#
# Alpha is flattened onto black: App Store screenshots must have no alpha.
#
# Usage:
#   tools/prepare_app_store_screenshots.sh
#
set -euo pipefail

SHOTS_DIR="$(cd "$(dirname "$0")" && pwd)/app_store_screenshots"
TARGETS=(2420x1668 2752x2064)

if [[ ! -d "$SHOTS_DIR" ]]; then
    echo "Screenshots folder not found: $SHOTS_DIR" >&2
    exit 1
fi

if ! command -v magick >/dev/null 2>&1; then
    echo "ImageMagick not found. Install it with: brew install imagemagick" >&2
    exit 1
fi

# Finder junk, harmless but it clutters the listing below.
find "$SHOTS_DIR" \( -name '.DS_Store' -o -name '._*' \) -delete

# Build the "already generated" name filter from the target list itself.
skip_pattern=""
for target in "${TARGETS[@]}"; do
    skip_pattern+="${skip_pattern:+|}_${target}"
done

processed=0
gallery_index=1
while IFS= read -r -d '' src; do
    stem="${src%.*}"
    stem="${stem##*/}"
    [[ "$stem" =~ (${skip_pattern})$ ]] && continue

    read -r width height < <(magick identify -format '%w %h\n' "$src[0]")
    if [[ -z "${width:-}" || -z "${height:-}" ]]; then
        echo "Skipped (unreadable): $src" >&2
        continue
    fi

    echo "$stem (${width}x${height})"
    for target in "${TARGETS[@]}"; do
        target_w="${target%x*}"
        target_h="${target#*x}"
        # Height once scaled to the target width, rounded like the resizer.
        scaled_h=$(( (height * target_w + width / 2) / width ))
        if (( scaled_h >= target_h )); then
            gravity=North   # cut the bottom
            note="cut $(( scaled_h - target_h ))px bottom"
        else
            gravity=Center  # black bars, split evenly
            note="black bars $(( (target_h - scaled_h) / 2 ))px"
        fi
        magick "$src[0]" \
            -filter Lanczos -resize "${target_w}x" \
            -background black -gravity "$gravity" -extent "$target" \
            -alpha remove -alpha off -depth 8 -strip \
            "${src%/*}/${stem}_${target}.png"
        echo "  -> ${stem}_${target}.png ($note)"
    done
    magick "$src[0]" \
        -alpha remove -alpha off -quality 95 -strip \
        "${src%/*}/gallery_${gallery_index}.jpg"
    echo "  -> gallery_${gallery_index}.jpg (quality 95)"
    gallery_index=$(( gallery_index + 1 ))
    processed=$(( processed + 1 ))
done < <(find "$SHOTS_DIR" -type f \( -iname '*.png' \) -print0 | sort -z)

if (( processed == 0 )); then
    echo "No source screenshots found in $SHOTS_DIR"
fi
