#!/usr/bin/env bash
set -euo pipefail

# Build Doxygen documentation using the repo Doxyfile.
if ! command -v doxygen >/dev/null 2>&1; then
  echo "doxygen not found in PATH" >&2
  exit 1
fi

doxygen "${1:-Doxyfile}"

out_dir="docs/doxygen/html/index.html"
if [ -f "$out_dir" ]; then
  echo "Docs generated: $out_dir"
fi
