# uVim
uViM (micro) editor

Reduced version of Vim.

- Normal Vim like code navigation
- Vim like text object handling
- Fzf style file browsing
- Ripgrep search from files
- Regex search in buffers (`/` and `?`, plus `:/` and `:?` from command mode)
- Syntax highlighting
- Really small binary

## Config

Generate the default config:

```sh
uvim --init-config
```

This writes to `~/.config/uvim/config.yaml` (or `$XDG_CONFIG_HOME/uvim/config.yaml`
if `XDG_CONFIG_HOME` is set). It also copies bundled themes into
`~/.config/uvim/themes/`. You can also provide a custom path:

```sh
uvim --init-config /path/to/uvim.yaml
```

## Themes

Built-in theme examples are in `themes/`. To use a theme:

- Copy a theme file to `~/.config/uvim/themes/` (or `$XDG_CONFIG_HOME/uvim/themes/`).
- Reference it in your config:

```yaml
theme:
  name: "solarized-dark"
```

You can also load a custom theme directly:

```sh
uvim --theme /path/to/theme.yaml
```

## Documentation (Doxygen)

Generate the configuration reference:

```sh
./scripts/build_docs.sh
```

The HTML output is written to `docs/doxygen/html/index.html`.

Install doxygen with your system package manager (e.g., `brew install doxygen`
on macOS, `apt-get install doxygen` on Debian/Ubuntu, or `dnf install doxygen`
on Fedora).

## LSP flags

- `--clangd` enable clangd LSP
- `--robot-lsp` enable Robot Framework LSP
- `--python-lsp` enable Python LSP
- `--mlang-lsp` enable Mlang LSP (expects the Mlang repo `tools/mlang_lsp`)
- `--html-lsp` enable HTML LSP
- `--css-lsp` enable CSS LSP
- `--json-lsp` enable JSON LSP
- `--ts-lsp` enable TypeScript/JavaScript LSP

## Dependencies (OS-specific)

uvim LSP helpers need Node.js (npm), Python, git, and clangd. Yarn is optional
if you prefer it over npm.

### macOS (Homebrew)

```sh
brew install node python git llvm yarn
```

Notes:
- clangd is included with `llvm` (add `/opt/homebrew/opt/llvm/bin` or
  `/usr/local/opt/llvm/bin` to `PATH` if needed).

### Debian/Ubuntu (apt)

```sh
sudo apt-get update
sudo apt-get install -y nodejs npm python3 python3-pip git clangd yarn
```

### Arch/Manjaro (pacman)

```sh
sudo pacman -S --noconfirm nodejs npm python python-pip git clang yarn
```

### Windows (winget)

```powershell
winget install OpenJS.NodeJS
winget install Python.Python.3
winget install Git.Git
winget install LLVM.LLVM
winget install Yarn.Yarn
```

### Windows (Chocolatey)

```powershell
choco install -y nodejs python git llvm yarn
```

### VSCode language servers (HTML/CSS/JSON/TS)

These LSPs are Node-based. You can install them globally:

```sh
npm install -g vscode-html-language-server vscode-css-language-server \
  vscode-json-language-server typescript-language-server typescript
```

Then run uvim with autodetect (default) or explicit flags:

```sh
uvim --html-lsp --css-lsp --json-lsp --ts-lsp
```

### Install script

Use the helper script to install one or more LSPs (uses git+https for Node packages):

```sh
./scripts/install_lsps.sh --html --css --json --ts
./scripts/install_lsps.sh --robot --python
./scripts/install_lsps.sh --all
```

### Sample files (LSP smoke tests)

Sample files for HTML/CSS/JS/TS live in `examples/lsp/`:

- `examples/lsp/sample.html`
- `examples/lsp/sample.css`
- `examples/lsp/sample.js`
- `examples/lsp/sample.ts`

## Search examples

In normal mode:

- `/foo.*bar` - regex search forward
- `?^word` - regex search backward

In command mode:

- `:/foo.*bar` - regex search forward
- `:?^word` - regex search backward

## Robot Framework tests (venv setup)

These tests expect a built binary at `build/uvim`. You can override the path with
`UVIM_BIN`.

### 1) Create and activate a virtual environment

```sh
python3 -m venv .venv
source .venv/bin/activate
```

### 2) Install Robot dependencies

```sh
python -m pip install --upgrade pip
python -m pip install -r tests/robot/requirements.txt
```

### 3) Run Robot tests

```sh
./tests/robot/run_robot.sh tests/robot/
```

If you prefer calling Robot directly, pass `--outputdir`:

```sh
robot --outputdir tests/robot/output tests/robot/
```

### 4) Deactivate the virtual environment

```sh
deactivate
```
