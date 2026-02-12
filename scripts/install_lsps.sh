#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: ./scripts/install_lsps.sh [options]

Install language servers used by uvim.

Options:
  --all            Install all supported LSPs (npm + pip + clangd)
  --clangd         Install clangd (brew/apt/pacman)
  --html           Install vscode-html-language-server (npm)
  --css            Install vscode-css-language-server (npm)
  --json           Install vscode-json-language-server (npm)
  --ts             Install typescript-language-server + typescript (npm)
  --robot          Install robotframework-lsp (pip)
  --python         Install python-lsp-server (pylsp) (pip)
  --help           Show this help

Examples:
  ./scripts/install_lsps.sh --all
  ./scripts/install_lsps.sh --clangd
  ./scripts/install_lsps.sh --html --css --json --ts
  ./scripts/install_lsps.sh --robot --python
EOF
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1
}

want_html=false
want_css=false
want_json=false
want_ts=false
want_robot=false
want_python=false
want_clangd=false

if [ "$#" -eq 0 ]; then
  usage
  exit 1
fi

while [ "$#" -gt 0 ]; do
  case "$1" in
    --all)
      want_html=true
      want_css=true
      want_json=true
      want_ts=true
      want_robot=true
      want_python=true
      want_clangd=true
      ;;
    --clangd) want_clangd=true ;;
    --html) want_html=true ;;
    --css) want_css=true ;;
    --json) want_json=true ;;
    --ts) want_ts=true ;;
    --robot) want_robot=true ;;
    --python) want_python=true ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 1
      ;;
  esac
  shift
done

npm_pkgs=()
pip_pkgs=()

if $want_html || $want_css || $want_json; then
  npm_pkgs+=("vscode-langservers-extracted")
fi
if $want_ts; then
  npm_pkgs+=("typescript-language-server")
  npm_pkgs+=("typescript")
fi

if $want_robot; then
  pip_pkgs+=("robotframework-lsp")
fi
if $want_python; then
  pip_pkgs+=("python-lsp-server")
fi

if [ "${#npm_pkgs[@]}" -gt 0 ]; then
  if ! need_cmd npm; then
    echo "npm not found. Install Node.js first." >&2
    exit 1
  fi
  echo "Installing npm packages: ${npm_pkgs[*]}"
  npm install -g "${npm_pkgs[@]}"
fi

if [ "${#pip_pkgs[@]}" -gt 0 ]; then
  if need_cmd python3; then
    PIP="python3 -m pip"
  elif need_cmd python; then
    PIP="python -m pip"
  else
    echo "python not found. Install Python first." >&2
    exit 1
  fi
  echo "Installing pip packages: ${pip_pkgs[*]}"
  $PIP install --upgrade "${pip_pkgs[@]}"
fi

if $want_clangd; then
  if need_cmd clangd; then
    echo "clangd already installed"
  elif need_cmd brew; then
    echo "Installing clangd via brew..."
    brew install llvm
    if [ -d "/opt/homebrew/opt/llvm/bin" ]; then
      echo "Add /opt/homebrew/opt/llvm/bin to PATH to use clangd"
    elif [ -d "/usr/local/opt/llvm/bin" ]; then
      echo "Add /usr/local/opt/llvm/bin to PATH to use clangd"
    fi
  elif need_cmd apt-get; then
    echo "Installing clangd via apt..."
    sudo apt-get update
    sudo apt-get install -y clangd
  elif need_cmd pacman; then
    echo "Installing clangd via pacman..."
    sudo pacman -S --noconfirm clang
  else
    echo "No supported package manager found for clangd (brew/apt/pacman)." >&2
    exit 1
  fi
fi

echo "Done."
