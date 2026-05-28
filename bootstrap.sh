#!/usr/bin/env sh
set -eu

build_dir="build"
jobs=""

usage() {
    cat <<EOF
Usage: ./bootstrap.sh [--build-dir DIR] [--jobs N]

Options:
  --build-dir DIR    CMake build directory (default: build)
  --build-dir=DIR    Same as --build-dir DIR
  -j, --jobs N       Parallel build jobs
  --jobs=N           Same as --jobs N
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
        -j|--jobs)
            if [ "$#" -lt 2 ]; then
                echo "bootstrap.sh: --jobs requires a value" >&2
                exit 2
            fi
            jobs="$2"
            shift 2
            ;;
        --jobs=*)
            jobs="${1#--jobs=}"
            if [ -z "$jobs" ]; then
                echo "bootstrap.sh: --jobs requires a value" >&2
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

if [ -z "$jobs" ] && [ -f "$build_dir/uvim-config.conf" ]; then
    jobs="$(sed -n 's/^jobs=//p' "$build_dir/uvim-config.conf" | tail -n 1)"
fi

cmake -S . -B "$build_dir"
if [ -n "$jobs" ]; then
    echo "cmake --build $build_dir --target uvim-config --parallel $jobs"
    cmake --build "$build_dir" --target uvim-config --parallel "$jobs"
else
    echo "cmake --build $build_dir --target uvim-config"
    cmake --build "$build_dir" --target uvim-config
fi
