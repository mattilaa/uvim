#!/usr/bin/env sh
set -eu

source_dir="."
build_dir="build"
config_file=""
target=""
jobs=""
install=""
install_dir=""
install_scope=""

usage() {
    cat <<EOF
Usage: ./build.sh [options]

Options:
  -S, --source-dir DIR    Source directory (default: .)
  -B, --build-dir DIR     CMake build directory (default: build)
      --config-file FILE  uvim-config.conf to import (default: BUILD_DIR/uvim-config.conf)
  -j, --jobs N            Override saved parallel build jobs
      --target NAME       Build a specific CMake target
  -i, --install           Install after build
      --no-install        Do not install after build
      --install-dir DIR   Override the executable installation directory
      --user-install      Install to ~/.local/bin (no administrator access)
      --system-install    Install to /usr/local/bin (uses sudo when needed)
  -h, --help              Show this help
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        -S|--source-dir)
            if [ "$#" -lt 2 ]; then
                echo "build.sh: $1 requires a value" >&2
                exit 2
            fi
            source_dir="$2"
            shift 2
            ;;
        --source-dir=*)
            source_dir="${1#--source-dir=}"
            shift
            ;;
        -B|--build-dir)
            if [ "$#" -lt 2 ]; then
                echo "build.sh: $1 requires a value" >&2
                exit 2
            fi
            build_dir="$2"
            shift 2
            ;;
        --build-dir=*)
            build_dir="${1#--build-dir=}"
            shift
            ;;
        --config-file)
            if [ "$#" -lt 2 ]; then
                echo "build.sh: --config-file requires a value" >&2
                exit 2
            fi
            config_file="$2"
            shift 2
            ;;
        --config-file=*)
            config_file="${1#--config-file=}"
            shift
            ;;
        -j|--jobs)
            if [ "$#" -lt 2 ]; then
                echo "build.sh: $1 requires a value" >&2
                exit 2
            fi
            jobs="$2"
            shift 2
            ;;
        --jobs=*)
            jobs="${1#--jobs=}"
            shift
            ;;
        --target)
            if [ "$#" -lt 2 ]; then
                echo "build.sh: --target requires a value" >&2
                exit 2
            fi
            target="$2"
            shift 2
            ;;
        --target=*)
            target="${1#--target=}"
            shift
            ;;
        -i|--install)
            install="true"
            shift
            ;;
        --no-install)
            install="false"
            shift
            ;;
        --install-dir)
            if [ "$#" -lt 2 ]; then
                echo "build.sh: --install-dir requires a value" >&2
                exit 2
            fi
            install_dir="$2"
            shift 2
            ;;
        --install-dir=*)
            install_dir="${1#--install-dir=}"
            shift
            ;;
        --user-install)
            install="true"
            install_dir="~/.local/bin"
            install_scope="user"
            shift
            ;;
        --system-install)
            install="true"
            install_dir="/usr/local/bin"
            install_scope="system"
            shift
            ;;
        *)
            echo "build.sh: unknown option: $1" >&2
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

if [ -z "$config_file" ]; then
    config_file="$build_dir/uvim-config.conf"
fi

if [ ! -f "$config_file" ]; then
    echo "build.sh: missing config file: $config_file" >&2
    echo "Run ./bootstrap.sh and ./build/uvim-config first, or pass --config-file." >&2
    exit 1
fi

uvim_config="$build_dir/uvim-config"
if [ ! -x "$uvim_config" ]; then
    echo "build.sh: $uvim_config is missing; bootstrapping uvim-config first"
    ./bootstrap.sh --build-dir "$build_dir"
fi

cache_file="$build_dir/uvim_config_cache.cmake"

if [ -z "$jobs" ]; then
    jobs="$(config_value jobs "$config_file")"
fi

if [ -z "$install_dir" ]; then
    install_dir="$(config_value install_dir "$config_file")"
    if [ -z "$install_dir" ]; then
        install_dir="~/.local/bin"
    fi
fi

if [ -z "$install_scope" ]; then
    case "$install_dir" in
        "~/.local/bin"|"$HOME/.local/bin") install_scope="user" ;;
        "/usr/local/bin") install_scope="system" ;;
    esac
fi

if [ -z "$install" ]; then
    install="$(config_value install_after_build "$config_file")"
fi

echo "$uvim_config --import $config_file --source-dir $source_dir --build-dir $build_dir --install-dir $install_dir --output $cache_file"
"$uvim_config" --import "$config_file" --source-dir "$source_dir" --build-dir "$build_dir" --install-dir "$install_dir" --output "$cache_file"

ninja_config="$(config_value ninja_generator "$config_file")"
use_ninja=""
if command -v ninja >/dev/null 2>&1 &&
    { [ -z "$ninja_config" ] || is_truthy "$ninja_config"; }; then
    use_ninja="1"
fi

if [ -n "$use_ninja" ]; then
    echo "cmake -C $cache_file -S $source_dir -B $build_dir -DUVIM_BOOTSTRAP_CONFIG_ONLY=OFF -G Ninja"
    cmake -C "$cache_file" -S "$source_dir" -B "$build_dir" -DUVIM_BOOTSTRAP_CONFIG_ONLY=OFF -G Ninja
else
    echo "cmake -C $cache_file -S $source_dir -B $build_dir -DUVIM_BOOTSTRAP_CONFIG_ONLY=OFF"
    cmake -C "$cache_file" -S "$source_dir" -B "$build_dir" -DUVIM_BOOTSTRAP_CONFIG_ONLY=OFF
fi

build_cmd="cmake --build $build_dir"
if [ -n "$target" ]; then
    build_cmd="$build_cmd --target $target"
fi
if [ -n "$jobs" ]; then
    build_cmd="$build_cmd --parallel $jobs"
fi
echo "$build_cmd"

if [ -n "$target" ] && [ -n "$jobs" ]; then
    cmake --build "$build_dir" --target "$target" --parallel "$jobs"
elif [ -n "$target" ]; then
    cmake --build "$build_dir" --target "$target"
elif [ -n "$jobs" ]; then
    cmake --build "$build_dir" --parallel "$jobs"
else
    cmake --build "$build_dir"
fi

if [ "$install" = "true" ] || [ "$install" = "ON" ] || [ "$install" = "1" ]; then
    echo "install destination: $install_dir"
    if [ "$install_scope" = "system" ] && [ "$(id -u)" -ne 0 ]; then
        if ! command -v sudo >/dev/null 2>&1; then
            echo "build.sh: sudo is required for a system installation" >&2
            exit 1
        fi
        echo "sudo cmake --install $build_dir --component uvim"
        sudo cmake --install "$build_dir" --component uvim
    else
        echo "cmake --install $build_dir --component uvim"
        cmake --install "$build_dir" --component uvim
    fi

    if [ "$install_scope" = "user" ]; then
        expanded_install_dir="$install_dir"
        case "$expanded_install_dir" in
            "~/"*) expanded_install_dir="$HOME/${expanded_install_dir#~/}" ;;
        esac
        case ":${PATH:-}:" in
            *":$expanded_install_dir:"*) ;;
            *)
                echo "warning: $expanded_install_dir is not on PATH" >&2
                echo 'Add this to your shell profile, then open a new terminal:' >&2
                echo '  export PATH="$HOME/.local/bin:$PATH"' >&2
                ;;
        esac
    fi
fi
