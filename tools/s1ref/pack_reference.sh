#!/bin/bash
# Packs the reference data for download (README "Reference data"): the
# manifests reference/renders/manifest_*.jsonl, every render they list and
# reference/derived, into dist/synth2-reference.zip with repo-relative paths
# (unzipping at the repo root restores them). reference/meta is in git;
# nothing else is packed (reference/presets = local patch banks, never
# published; the presets group is anonymized: patch0001..).
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=dist/synth2-reference.zip
[[ -d reference/renders ]] || { echo "ERROR: no reference/renders" >&2; exit 1; }
[[ -e $OUT ]] && { echo "ERROR: $OUT exists, move it away first" >&2; exit 1; }
mkdir -p dist
{
    ls reference/renders/manifest_*.jsonl
    python3 -c '
import glob, json
for m in sorted(glob.glob("reference/renders/manifest_*.jsonl")):
    for line in open(m):
        print("reference/renders/" + json.loads(line)["file"])'
    ls reference/derived/*
} | zip -X -q "$OUT" -@
unzip -l "$OUT" | tail -1
