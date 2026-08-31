#!/usr/bin/env sh
set -eu

build_dir="build"
jobs=""
no_color_requested=0
dependency_install="ask"
old_no_color="${NO_COLOR-}"
had_no_color=0
if [ "${NO_COLOR+x}" = "x" ]; then
    had_no_color=1
fi

usage() {
    cat <<EOF
Usage: ./bootstrap.sh [--build-dir DIR] [--jobs N] [dependency options]

Options:
  --build-dir DIR    CMake build directory (default: build)
  --build-dir=DIR    Same as --build-dir DIR
  -j, --jobs N       Parallel build jobs
  --jobs=N           Same as --jobs N
  --install-deps     Install missing compilation dependencies without prompting
  --no-install-deps  Never install dependencies; report anything missing
  --no-color         Disable colored prompts and child tool output
  -h, --help         Show this help
EOF
}

restore_no_color() {
    if [ "$no_color_requested" = "1" ]; then
        if [ "$had_no_color" = "1" ]; then
            NO_COLOR="$old_no_color"
            export NO_COLOR
        else
            unset NO_COLOR
        fi
    fi
}

trap restore_no_color EXIT

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
        --no-color)
            no_color_requested=1
            NO_COLOR=1
            export NO_COLOR
            shift
            ;;
        --install-deps)
            dependency_install="yes"
            shift
            ;;
        --no-install-deps)
            dependency_install="no"
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

supports_color() {
    [ -t 1 ] || return 1
    [ -z "${NO_COLOR:-}" ] || return 1
    [ "${TERM:-}" != "dumb" ] || return 1
    [ -n "${TERM:-}" ] || return 1
    return 0
}

prompt_yes_no() {
    message="$1"
    suffix="(Y/n)"
    if supports_color; then
        printf "\033[1;36m%s\033[0m \033[1;32m%s\033[0m " "$message" "$suffix"
    else
        printf "%s %s " "$message" "$suffix"
    fi
}

confirm_yes_no() {
    prompt="$1"
    while true; do
        prompt_yes_no "$prompt"
        if ! IFS= read -r answer; then
            return 1
        fi
        case "$answer" in
            ""|y|Y)
                return 0
                ;;
            n|N)
                return 1
                ;;
        esac
    done
}

find_c_compiler() {
    if [ -n "${CC:-}" ] && command -v "$CC" >/dev/null 2>&1; then
        printf '%s\n' "$CC"
        return 0
    fi
    for candidate in cc clang gcc; do
        if command -v "$candidate" >/dev/null 2>&1; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

find_cxx_compiler() {
    if [ -n "${CXX:-}" ] && command -v "$CXX" >/dev/null 2>&1; then
        printf '%s\n' "$CXX"
        return 0
    fi
    for candidate in c++ clang++ g++; do
        if command -v "$candidate" >/dev/null 2>&1; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

compiler_supports_cxx20() {
    compiler="$1"
    printf '%s\n' \
        '#include <concepts>' \
        'template<class T> concept Number = requires(T value) { value + 1; };' \
        'static_assert(Number<int>);' \
        'int main() { return 0; }' |
        "$compiler" -std=c++20 -x c++ -fsyntax-only - >/dev/null 2>&1
}

cmake_is_usable() {
    command -v cmake >/dev/null 2>&1 || return 1
    cmake_version="$(cmake --version 2>/dev/null | sed -n '1s/[^0-9]*\([0-9][0-9.]*\).*/\1/p')"
    [ -n "$cmake_version" ] || return 1
    awk -v version="$cmake_version" 'BEGIN {
        split(version, parts, ".")
        exit !((parts[1] + 0) > 3 ||
               ((parts[1] + 0) == 3 && (parts[2] + 0) >= 14))
    }'
}

detect_missing_dependencies() {
    missing_dependencies=""
    missing_cmake=0
    missing_git=0
    missing_build_tool=0
    missing_compiler=0

    if ! cmake_is_usable; then
        missing_cmake=1
        missing_dependencies="CMake 3.14 or newer"
    fi
    if ! command -v git >/dev/null 2>&1; then
        missing_git=1
        missing_dependencies="${missing_dependencies}${missing_dependencies:+, }Git"
    fi
    if ! command -v ninja >/dev/null 2>&1 &&
        ! command -v make >/dev/null 2>&1; then
        missing_build_tool=1
        missing_dependencies="${missing_dependencies}${missing_dependencies:+, }Ninja or Make"
    fi

    c_compiler="$(find_c_compiler || true)"
    cxx_compiler="$(find_cxx_compiler || true)"
    if [ -z "$c_compiler" ] || [ -z "$cxx_compiler" ]; then
        missing_compiler=1
        missing_dependencies="${missing_dependencies}${missing_dependencies:+, }C/C++ compiler"
    elif ! compiler_supports_cxx20 "$cxx_compiler"; then
        missing_compiler=1
        missing_dependencies="${missing_dependencies}${missing_dependencies:+, }C++20-capable compiler"
    fi
}

run_as_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    else
        echo "bootstrap.sh: administrator access is required, but sudo was not found" >&2
        return 1
    fi
}

install_missing_dependencies() {
    system_name="$(uname -s 2>/dev/null || printf unknown)"

    if [ "$system_name" = "Darwin" ]; then
        if ! command -v brew >/dev/null 2>&1; then
            echo "bootstrap.sh: Homebrew is required to install missing macOS dependencies." >&2
            echo "Install it from https://brew.sh and run bootstrap.sh again." >&2
            if [ "$missing_compiler" = "1" ]; then
                echo "The Apple compiler can also be installed with: xcode-select --install" >&2
            fi
            return 1
        fi
        packages=""
        [ "$missing_cmake" = "0" ] || packages="$packages cmake"
        [ "$missing_git" = "0" ] || packages="$packages git"
        [ "$missing_build_tool" = "0" ] || packages="$packages ninja"
        [ "$missing_compiler" = "0" ] || packages="$packages llvm"
        # Package names are selected above and contain no shell metacharacters.
        # shellcheck disable=SC2086
        brew install $packages
        if [ "$missing_compiler" = "1" ]; then
            llvm_prefix="$(brew --prefix llvm 2>/dev/null || true)"
            if [ -n "$llvm_prefix" ] && [ -d "$llvm_prefix/bin" ]; then
                PATH="$llvm_prefix/bin:$PATH"
                export PATH
            fi
        fi
        return
    fi

    if command -v apt-get >/dev/null 2>&1; then
        packages=""
        [ "$missing_cmake" = "0" ] || packages="$packages cmake"
        [ "$missing_git" = "0" ] || packages="$packages git"
        [ "$missing_build_tool" = "0" ] || packages="$packages ninja-build"
        [ "$missing_compiler" = "0" ] || packages="$packages build-essential"
        run_as_root apt-get update
        # shellcheck disable=SC2086
        run_as_root apt-get install -y $packages
        return
    fi

    if command -v dnf >/dev/null 2>&1; then
        packages=""
        [ "$missing_cmake" = "0" ] || packages="$packages cmake"
        [ "$missing_git" = "0" ] || packages="$packages git"
        [ "$missing_build_tool" = "0" ] || packages="$packages ninja-build"
        [ "$missing_compiler" = "0" ] || packages="$packages gcc gcc-c++"
        # shellcheck disable=SC2086
        run_as_root dnf install -y $packages
        return
    fi

    if command -v pacman >/dev/null 2>&1; then
        packages=""
        [ "$missing_cmake" = "0" ] || packages="$packages cmake"
        [ "$missing_git" = "0" ] || packages="$packages git"
        [ "$missing_build_tool" = "0" ] || packages="$packages ninja"
        [ "$missing_compiler" = "0" ] || packages="$packages base-devel"
        # shellcheck disable=SC2086
        run_as_root pacman -S --needed --noconfirm $packages
        return
    fi

    if command -v zypper >/dev/null 2>&1; then
        packages=""
        [ "$missing_cmake" = "0" ] || packages="$packages cmake"
        [ "$missing_git" = "0" ] || packages="$packages git"
        [ "$missing_build_tool" = "0" ] || packages="$packages ninja"
        [ "$missing_compiler" = "0" ] || packages="$packages gcc gcc-c++"
        # shellcheck disable=SC2086
        run_as_root zypper --non-interactive install $packages
        return
    fi

    if command -v apk >/dev/null 2>&1; then
        packages=""
        [ "$missing_cmake" = "0" ] || packages="$packages cmake"
        [ "$missing_git" = "0" ] || packages="$packages git"
        [ "$missing_build_tool" = "0" ] || packages="$packages ninja"
        [ "$missing_compiler" = "0" ] || packages="$packages build-base"
        # shellcheck disable=SC2086
        run_as_root apk add $packages
        return
    fi

    echo "bootstrap.sh: no supported package manager was found." >&2
    echo "Install $missing_dependencies and run bootstrap.sh again." >&2
    return 1
}

ensure_build_dependencies() {
    detect_missing_dependencies
    if [ -z "$missing_dependencies" ]; then
        echo "Compilation dependencies found."
        return 0
    fi

    echo "Missing compilation dependencies: $missing_dependencies"
    case "$dependency_install" in
        yes)
            ;;
        no)
            echo "Install them manually or rerun with --install-deps." >&2
            return 1
            ;;
        ask)
            if ! confirm_yes_no "Do you want bootstrap.sh to install them?"; then
                echo "Cannot continue without the compilation dependencies." >&2
                echo "Install them manually or rerun with --install-deps." >&2
                return 1
            fi
            ;;
    esac

    install_missing_dependencies
    detect_missing_dependencies
    if [ -n "$missing_dependencies" ]; then
        echo "bootstrap.sh: dependencies are still missing: $missing_dependencies" >&2
        return 1
    fi
    echo "Compilation dependencies installed."
}

ensure_build_dependencies

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

if confirm_yes_no "Do you want to run uvim-config?"; then
    echo "$uvim_config"
    "$uvim_config"

    if confirm_yes_no "Do you want to build uVim?"; then
        echo "./build.sh --build-dir $build_dir"
        ./build.sh --build-dir "$build_dir"
    fi
fi
