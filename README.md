# uVim
uViM (micro) editor

Reduced version of Vim.

- Normal Vim like code navigation
- Vim like text object handling
- Fzf style file browsing
- Ripgrep search from files
- Regex search in buffers (`/` and `?`, plus `:/` and `:?` from command mode)
- Run a shell command and browse its output (`:run`)
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

Published API/configuration docs are available on GitHub Pages:
https://mattilaa.github.io/uvim/

Install doxygen with your system package manager (e.g., `brew install doxygen`
on macOS, `apt-get install doxygen` on Debian/Ubuntu, or `dnf install doxygen`
on Fedora).

## Assembly instruction docs

Assembly instruction `gd` support is controlled by the CMake option
`UVIM_ENABLE_ASM_DOCS`. It is enabled by default.

Build with the default:

```sh
cmake -S . -B build
cmake --build build
```

Or enable it explicitly:

```sh
cmake -S . -B build -DUVIM_ENABLE_ASM_DOCS=ON
cmake --build build
```

To disable it:

```sh
cmake -S . -B build -DUVIM_ENABLE_ASM_DOCS=OFF
cmake --build build
```

Run uvim:

```sh
./build/uvim file.s
```

Usage:

- Open a `.s`, `.S`, `.asm`, or `.ASM` buffer.
- Put the cursor on an instruction row, for example `movq %rdi, %rax` or
  `ldr x0, [sp]`.
- Press `gd`.
- uvim opens a cached markdown instruction index and jumps to the matching
  instruction section. The index is generated from documentation text packed
  into the uvim binary, so this mode does not need network access.
- Press `Space-ga` to show the current instruction docs in a modal popup.
  Use `j/k` to scroll the popup and `q` to close it.

To additionally fetch and cache the Compiler Explorer/Godbolt asm
documentation for each instruction on first `gd`, run:

```sh
uvim --asm-docs-fetch file.s
```

This uses `curl` at runtime and calls the Compiler Explorer API, for example
`https://godbolt.org/api/asm/amd64/mov` or
`https://godbolt.org/api/asm/aarch64/ldr`. If that fetch fails or `curl` is not
available, uvim falls back to the packed local documentation index.

The cache is written to `$XDG_CACHE_HOME/uvim/asm-docs` when `XDG_CACHE_HOME`
is set, otherwise to `~/.cache/uvim/asm-docs`. You can override it with:

```sh
UVIM_ASM_DOCS_CACHE_DIR=/tmp/uvim-asm-docs uvim file.s
```

The default cached docs are generated from packed uvim-owned documentation text
with reference links for x86/x64 and AArch64 instructions. The
`--asm-docs-fetch` mode stores fetched Compiler Explorer docs separately under
`asm-docs/fetched/compiler-explorer/`.

## LSP flags

- `--clangd` enable clangd LSP
- `--ccdir <dir>` compile commands base dir/file for clangd
- `--cc-collect-all` recursively merge all `compile_commands.json` files
- `--cc-windows-to-wsl` rewrite Windows paths (`C:\...`) to WSL paths (`/mnt/c/...`)
- `--robot-lsp` enable Robot Framework LSP
- `--python-lsp` enable Python LSP
- `--mlang-lsp` enable Mlang LSP (optional; not distributed yet)
- `--html-lsp` enable HTML LSP
- `--css-lsp` enable CSS LSP
- `--json-lsp` enable JSON LSP
- `--ts-lsp` enable TypeScript/JavaScript LSP
- `--no-git-index` disable git-backed indexing for fuzzy find and grep
- `--no-gitignore` disable `.gitignore` filtering for file browser, fuzzy find,
  grep, and other project scans

Local scaffold example:

```sh
./build/uvim --clangd --python-lsp --html-lsp --css-lsp --ts-lsp
```

Startup note:

- when you start directly into a directory browser with `uvim .`,
  auto-detected LSP startup is deferred until you open a file
- this keeps directory launch fast while preserving `autodetectlsps: true` for
  normal editing

### Optional: mlangd-mla logging (private builds)

When uvim launches `mlangd-mla`, you can enable server logging directly from
uvim CLI:

- `--enable-log`
- `--enable-log=info`
- `--enable-log=debug`
- `--enable-log=verbose`
- `--log-level <info|debug|verbose>` (implies `--enable-log`)
- `--log-colors` (colored level tags in server log lines)
- `--log-dir <dir>` (default log file is `/tmp/mlangd-mla.log`)

Log level behavior:

- `info`: includes `[INFO]`, `[WARN]`, `[ERROR]`
- `debug`: includes `info` + `[DEBUG]`
- `verbose`: includes `debug` + `[VERBOSE]` (all levels)

Examples:

```sh
uvim --mlang-lsp --enable-log
uvim --mlang-lsp --enable-log=debug
uvim --mlang-lsp --log-level verbose
```

Notes:

- These flags are passed through when uvim resolves `mlangd-mla`.
- If you force `mlangd` (C++ server), uvim-specific `mlangd-mla` logging flags
  are not injected.

## Dependencies (OS-specific)

uvim LSP helpers need Node.js (npm), Python, git, and clangd. Yarn is optional
if you prefer it over npm.

If you run `uvim` inside WSL, install these tools inside the WSL distro
terminal (`apt`/`pacman`), not only on Windows host (`winget`/`choco`).

### Language servers for Python + C/C++ + HTML/CSS/TS

Install these language servers:

```sh
npm install -g pyright vscode-langservers-extracted \
  typescript-language-server typescript
```

Then run:

```sh
uvim --clangd --python-lsp --html-lsp --css-lsp --ts-lsp
```

If you prefer `pylsp` instead of `pyright`:

```sh
python3 -m pip install --upgrade python-lsp-server
uvim --clangd --python-lsp --python-lsp-path pylsp --html-lsp --css-lsp --ts-lsp
```

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

### WSL with Windows build outputs (clangd)

If you run `uvim` inside WSL but your project/builds are on Windows drives
(for example `/mnt/c/dev/...`) and compile databases are generated by Visual
Studio/MSVC, use:

```sh
# inside WSL terminal
sudo apt-get update
sudo apt-get install -y clangd nodejs npm python3 python3-pip git
npm install -g pyright vscode-langservers-extracted \
  typescript-language-server typescript
```

Then run uvim:

```sh
uvim --clangd --cc-collect-all --cc-windows-to-wsl /mnt/c/dev/your-project
```

What this does:

- collects all `compile_commands.json` files recursively from the browsed
  Windows directory tree, for example under `/mnt/c/dev/your-project`
- rewrites Windows-style paths in compile database entries and command lines
- writes a merged db for clangd to `.uvim/clangd/compile_commands.json` under
  the directory where `uvim` was started in WSL
- when `--cc-windows-to-wsl` is used, uvim watches source compile databases for
  updates and refreshes `.uvim/clangd/compile_commands.json` automatically

Current behavior is WSL-specific:

- opening `/mnt/c/dev/your-project` starts the file browser in that directory
- clangd still reads the merged compile database from the WSL startup
  directory, not from `/mnt/c/...`

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

Note:
- `--python` in `install_lsps.sh` installs `python-lsp-server` (`pylsp`).
- uvim default Python LSP path is `pyright-langserver`; install `pyright` (npm)
  or run with `--python-lsp-path pylsp`.

### One-shot setup script (macOS/WSL/Ubuntu/Arch)

For Python + C/C++ + HTML/CSS/TS, you can use:

```sh
./scripts/install_dev_stack.sh
```

Optional (`pylsp` fallback):

```sh
./scripts/install_dev_stack.sh --with-pylsp
```

This script detects Homebrew/apt/pacman and, when running in WSL, installs
inside the WSL distro environment.

### Sample files (LSP smoke tests)

Sample files for HTML/CSS/JS/TS live in `examples/lsp/`:

- `examples/lsp/sample.html`
- `examples/lsp/sample.css`
- `examples/lsp/sample.js`
- `examples/lsp/sample.ts`

## File browser

Open with `Space-e` (leader-e) or `:Ex` / `:Explore`.

Navigation:

- `j`/`k` - move cursor down/up
- `Enter` / `l` / `→` - open file or descend into directory
- `h` / `←` / `-` - go to parent directory
- `Ctrl-d` / `Ctrl-u` - half-page down/up
- `gg` / `G` - top/bottom
- `Ctrl-o` / `Tab` - history back / forward
- `cd` - chdir to current directory (then `:pwd` reports it)
- `.` - toggle hidden files
- `Ctrl-g` - toggle `.gitignore` filtering
- `r` / `Ctrl-l` - refresh

Selection (multi-file):

- `Space` - toggle selection on the cursor entry
- `Shift-V` - start/stop visual-line selection. Exiting with `Shift-V`
  keeps the range as persistent selection; you can then move the cursor
  and press `Shift-V` again to extend with another segment.
- `Esc` - cancel an in-progress visual selection (restores the prior set)
- Double-`Esc` - clear all selections (and any cut buffer)
- `Enter` with a non-empty selection - opens every selected file as a
  buffer (directories skipped, alphabetical order)

File operations:

- `n` - create new file (nested paths like `a/b/c.txt` are created with
  parent directories on confirmation)
- `Shift-D` - create new directory (`mkdir -p` semantics)
- `d` - delete selected entries (or the cursor entry) with confirmation
- `y` - yank selected entries (or the cursor entry) into the paste buffer
- `m` - cut selected entries into the paste buffer (move mode)
- `p` - paste buffer into the current directory (copy or move depending
  on whether `y` or `m` was used)
- `u` / `Ctrl-r` - undo / redo the last file operation
- `Shift-R` - rename the cursor entry

Command-mode entries:

- `:q` - exit file browser
- `:cd <path>` - change directory; `:cdr` jumps to project root
- `:pwd` - print working directory
- `:mkdir <name>` / `:md <name>` - create directory
- `:new <name>` / `:touch <name>` - create file (prompts to open or
  replace if it already exists)
- `:delete` / `:d` / `:rm` - delete cursor entry
- `:rename <name>` / `:r <name>` / `:mv <name>` - rename cursor entry
- `:/<regex>` / `:?<regex>` - regex match within the listing; `Ctrl-J`/
  `Ctrl-K` cycle matches, `Ctrl-Space`/`Ctrl-N` toggle selection on all
  matches
- `:run <cmd>` - run a shell command in the current directory and open
  the result in the run view (see below). Pressing `Tab` after `run `
  appends the currently selected files to the prompt, alphabetically and
  shell-quoted, so `:run zip a.zip` + `Tab` becomes `:run zip a.zip 'foo
  bar.txt' baz.txt ...`.

## :run output view

`:run <shell command>` (from the file browser command line) executes the
command in the current directory and opens its combined stdout+stderr in
a scrollable view. Inside the view:

- `j`/`k`, `Ctrl-d`/`Ctrl-u`, `gg`/`G` - cursor / scroll
- `Space` - toggle the cursor row's selection
- `Shift-V` - start/stop visual-line selection. Like the file browser,
  exiting with `Shift-V` keeps the range; pressing `Shift-V` again starts
  a new segment that extends the persistent selection. `Space` while
  visual is active commits the current range and exits visual.
- `Esc` while visual - cancel current segment (restore prior set)
- Double-`Esc` - clear all selection
- `y` - yank selected rows (or the cursor row if none) to the system
  clipboard and yank buffer; selection is preserved
- `/` - incremental search; matches highlight with grey background and
  black foreground, `n`/`Shift-N` cycle. `q` clears the highlight; press
  `q` again (or `Esc`) to exit the view.

Long output lines wrap on screen but count as a single logical row for
cursor movement and selection.

## Tab bar / buffer management

- Each tab shows the buffer index (e.g. `1:foo.cpp`). Toggle with
  `:set tabnumbers` / `:set notabnumbers` (default on). Persists in
  config under `editor.tabnumbers`.
- `:set showtabs` / `:set noshowtabs` - show/hide the tab bar entirely.
- `Space-h` / `Space-l` - move the current buffer left / right in the
  tab bar.
- `Ctrl-Shift-H` / `Ctrl-Shift-L` - same as above, no leader needed.
  Requires a terminal that supports either xterm `modifyOtherKeys=2` or
  the kitty keyboard protocol (kitty, ghostty, wezterm, alacritty,
  iTerm2 with "Report modifiers using CSI u" enabled, recent xterm,
  tmux with `xterm-keys on`). uvim requests both protocols on startup.
  In Apple Terminal these combos still arrive as plain `Ctrl-H`/`Ctrl-L`
  and just switch buffers — use the leader binding there.
- `Ctrl-h` / `Ctrl-l` - previous / next buffer (unchanged).

## Diagnostics

Set `UVIM_KEYLOG=<path>` to log every byte read from stdin to the given
file. Useful for working out what your terminal sends for a given key
combination:

```sh
UVIM_KEYLOG=/tmp/uvim_keys.log uvim somefile
```

Press the key, quit, then `cat /tmp/uvim_keys.log`. ESC is written as
`\e`, printable bytes as themselves, everything else as `<HH>`.

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
