#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: build_install.sh [--install] [--no-install] [--debug] [--no-debug] [--prefix <path>] [--help]

Builds uvim with the standard Ninja Release config and optionally installs it.

Options:
  --install          Install after build (default)
  --no-install       Skip install step
  --debug            Enable UVIM_DEBUG_LSP and UVIM_DEBUG_LOGGING
  --no-debug         Disable debug logging (default)
  --prefix <path>    Install prefix (default: $HOME/.local)
  --help             Show this help
EOF
}

install_after_build=true
prefix="${HOME}/.local"
debug=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help)
      usage
      exit 0
      ;;
    --install)
      install_after_build=true
      shift
      ;;
    --no-install)
      install_after_build=false
      shift
      ;;
    --prefix)
      if [[ $# -lt 2 ]]; then
        echo "error: --prefix requires a value" >&2
        exit 1
      fi
      prefix="$2"
      shift 2
      ;;
    --debug)
      debug=true
      shift
      ;;
    --no-debug)
      debug=false
      shift
      ;;
    *)
      echo "error: unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

cmake_args=(
  -S .
  -B build
  -G Ninja
  -DCMAKE_BUILD_TYPE=Release
  -DUVIM_EXPORT_COMPILE_COMMANDS=ON
  -DUVIM_ENABLE_CLANGD_LSP=ON
  -DUVIM_BUILD_TESTS=ON
)

if $debug; then
  cmake_args+=(-DUVIM_DEBUG_LSP=ON -DUVIM_DEBUG_LOGGING=ON)
else
  cmake_args+=(-DUVIM_DEBUG_LSP=OFF -DUVIM_DEBUG_LOGGING=OFF)
fi

cmake "${cmake_args[@]}"

cmake --build build

if $install_after_build; then
  cmake --install build --prefix "$prefix" --component uvim
fi
