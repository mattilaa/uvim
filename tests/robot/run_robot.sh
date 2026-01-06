#!/usr/bin/env sh
set -e

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUTPUT_DIR="$ROOT_DIR/tests/robot/output"

mkdir -p "$OUTPUT_DIR"

robot --outputdir "$OUTPUT_DIR" "$@"
