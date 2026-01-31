#!/usr/bin/env sh
set -e

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUTPUT_DIR="$ROOT_DIR/tests/robot/output"

mkdir -p "$OUTPUT_DIR"

if [ "$#" -eq 0 ]; then
  robot --outputdir "$OUTPUT_DIR" "$ROOT_DIR/tests/robot"
else
  robot --outputdir "$OUTPUT_DIR" "$@"
fi
