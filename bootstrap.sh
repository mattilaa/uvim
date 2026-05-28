#!/usr/bin/env sh
set -eu

build_dir="build"

usage() {
    cat <<EOF
Usage: ./bootstrap.sh [--build-dir DIR]

Options:
  --build-dir DIR    CMake build directory (default: build)
  --build-dir=DIR    Same as --build-dir DIR
  -h, --help         Show this help
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        --build-dir)
            if [ "$#" -lt 2 ]; then
                echo "bootstrap.sh: --build-dir requires a value" >&2
                exit 2
            fi
            build_dir="$2"
            shift 2
            ;;
        --build-dir=*)
            build_dir="${1#--build-dir=}"
            if [ -z "$build_dir" ]; then
                echo "bootstrap.sh: --build-dir requires a value" >&2
                exit 2
            fi
            shift
            ;;
        *)
            echo "bootstrap.sh: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

cmake -S . -B "$build_dir"
cmake --build "$build_dir" --target uvim-config
