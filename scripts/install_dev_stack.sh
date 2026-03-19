#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: ./scripts/install_dev_stack.sh [options]

Install uvim dependencies for:
- C/C++ (clangd)
- Python (pyright; optional pylsp)
- HTML/CSS/JSON (vscode-langservers-extracted)
- TypeScript (typescript-language-server + typescript)

Supported environments:
- macOS (Homebrew)
- Debian/Ubuntu (apt)
- Arch/Manjaro (pacman)
- WSL (runs inside your Linux distro)

Options:
  --with-pylsp   Also install python-lsp-server (pylsp)
  --help         Show this help

Examples:
  ./scripts/install_dev_stack.sh
  ./scripts/install_dev_stack.sh --with-pylsp
EOF
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1
}

is_wsl() {
  if [ -f /proc/sys/kernel/osrelease ] && grep -qi "microsoft" /proc/sys/kernel/osrelease; then
    return 0
  fi
  return 1
}

install_system_deps() {
  if need_cmd brew; then
    echo "Installing system dependencies via Homebrew..."
    brew install node python git llvm
    if [ -d "/opt/homebrew/opt/llvm/bin" ]; then
      echo "Add /opt/homebrew/opt/llvm/bin to PATH to use clangd"
    elif [ -d "/usr/local/opt/llvm/bin" ]; then
      echo "Add /usr/local/opt/llvm/bin to PATH to use clangd"
    fi
    return 0
  fi

  if need_cmd apt-get; then
    if is_wsl; then
      echo "WSL detected: installing dependencies in the WSL distro."
    fi
    echo "Installing system dependencies via apt..."
    sudo apt-get update
    sudo apt-get install -y nodejs npm python3 python3-pip git clangd
    return 0
  fi

  if need_cmd pacman; then
    if is_wsl; then
      echo "WSL detected: installing dependencies in the WSL distro."
    fi
    echo "Installing system dependencies via pacman..."
    sudo pacman -S --noconfirm nodejs npm python python-pip git clang
    return 0
  fi

  echo "No supported package manager found (brew/apt/pacman)." >&2
  exit 1
}

with_pylsp=false

while [ "$#" -gt 0 ]; do
  case "$1" in
    --with-pylsp)
      with_pylsp=true
      ;;
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

install_system_deps

if ! need_cmd npm; then
  echo "npm not found after installing system dependencies." >&2
  exit 1
fi

echo "Installing npm language servers..."
npm install -g \
  pyright \
  vscode-langservers-extracted \
  typescript-language-server \
  typescript

if ! need_cmd python3 && ! need_cmd python; then
  echo "python not found after installing system dependencies." >&2
  exit 1
fi

if $with_pylsp; then
  echo "Installing optional pylsp..."
  if need_cmd python3; then
    python3 -m pip install --upgrade python-lsp-server
  else
    python -m pip install --upgrade python-lsp-server
  fi
fi

echo
echo "Done. Start uvim with:"
echo "  uvim --clangd --python-lsp --html-lsp --css-lsp --ts-lsp"
if $with_pylsp; then
  echo "Or force pylsp:"
  echo "  uvim --clangd --python-lsp --python-lsp-path pylsp --html-lsp --css-lsp --ts-lsp"
fi
