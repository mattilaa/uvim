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

config_value() {
    key="$1"
    file="$2"
    if [ ! -f "$file" ]; then
        return 0
    fi
    sed -n "s/^$key=//p" "$file" | tail -n 1
}

is_truthy() {
    case "$1" in
        ON|on|true|TRUE|yes|YES|1)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

confirm_yes_no() {
    prompt="$1"
    while true; do
        printf "%s " "$prompt"
        if ! IFS= read -r answer; then
            return 1
        fi
        case "$answer" in
            y|Y)
                return 0
                ;;
            n|N)
                return 1
                ;;
        esac
    done
}

if [ -z "$jobs" ] && [ -f "$build_dir/uvim-config.conf" ]; then
    jobs="$(config_value jobs "$build_dir/uvim-config.conf")"
fi

use_ninja=""
if command -v ninja >/dev/null 2>&1; then
    if [ -f "$build_dir/uvim-config.conf" ]; then
        ninja_config="$(config_value ninja_generator "$build_dir/uvim-config.conf")"
        if [ -z "$ninja_config" ] || is_truthy "$ninja_config"; then
            use_ninja="1"
        fi
    else
        use_ninja="1"
    fi
fi

if [ -n "$use_ninja" ]; then
    echo "cmake -S . -B $build_dir -DUVIM_BOOTSTRAP_CONFIG_ONLY=ON -DCMAKE_BUILD_TYPE=Release -G Ninja"
    cmake -S . -B "$build_dir" -DUVIM_BOOTSTRAP_CONFIG_ONLY=ON -DCMAKE_BUILD_TYPE=Release -G Ninja
else
    echo "cmake -S . -B $build_dir -DUVIM_BOOTSTRAP_CONFIG_ONLY=ON -DCMAKE_BUILD_TYPE=Release"
    cmake -S . -B "$build_dir" -DUVIM_BOOTSTRAP_CONFIG_ONLY=ON -DCMAKE_BUILD_TYPE=Release
fi
if [ -n "$jobs" ]; then
    echo "cmake --build $build_dir --target uvim-config --parallel $jobs"
    cmake --build "$build_dir" --target uvim-config --parallel "$jobs"
else
    echo "cmake --build $build_dir --target uvim-config"
    cmake --build "$build_dir" --target uvim-config
fi

uvim_config="$build_dir/uvim-config"
if [ ! -x "$uvim_config" ]; then
    if [ -x "$build_dir/uvim-config.exe" ]; then
        uvim_config="$build_dir/uvim-config.exe"
    else
        echo "bootstrap.sh: cannot find uvim-config in $build_dir after build" >&2
        exit 1
    fi
fi

if confirm_yes_no "Do you want to run uvim-config? (y/n)"; then
    echo "$uvim_config"
    "$uvim_config"

    if confirm_yes_no "Do you want to build uVim? (y/n)"; then
        echo "./build.sh --build-dir $build_dir"
        ./build.sh --build-dir "$build_dir"
    fi
fi
