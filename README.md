# uVim

uVim is a configurable, terminal-native modal code editor. It keeps familiar
vi/Vim navigation and editing, but the project now also includes project
search and indexing, file and buffer browsers, nested splits, Git views,
formatters, diagnostics, and optional language-server clients.

The same source tree can produce very different editors. `uvim-config` can
build a reduced vi-style binary for a virtual-terminal environment, a compact
editor with selected integrations, or the complete development build. Large
features are compile-time options, so reduced builds do not merely hide unused
menus—they leave the corresponding implementation out of the binary.

## Highlights

- vi-style normal, insert, replace, visual, text-object, mark, and jump flows
- syntax highlighting, tabs, buffers, file browser, and nested split panes
- cached Ctrl-P fuzzy file finding and multithreaded Ctrl-A project grep
- regex search in buffers and projects
- Git status, diff, log, stage, commit, blame, stash, and related views
- formatting and diagnostics for supported languages
- optional LSP clients for C/C++, Robot Framework, Python, Mlang, HTML, CSS,
  JSON, JavaScript, and TypeScript
- project-aware `compile_commands.json` and `mlang_commands.json` discovery
- configurable themes, terminal colors, clipboard support, assembly docs,
  command output browsing, and auxiliary developer views
- POSIX and native Windows terminal backends

## Contents

- [Build profiles and uvim-config](#build-profiles-and-uvim-config)
- [Runtime configuration](#runtime-configuration)
- [Themes](#themes)
- [Generated documentation](#generated-documentation)
- [Assembly instruction docs](#assembly-instruction-docs)
- [LSP flags](#lsp-flags)
- [Dependencies](#dependencies-os-specific)
- [File browser](#file-browser)
- [`:run` output view](#run-output-view)
- [Tab bar and buffer management](#tab-bar--buffer-management)
- [Diagnostics](#diagnostics)
- [Search examples](#search-examples)

## Build profiles and uvim-config

`uvim-config` is the supported front end for selecting a build profile and
overriding individual CMake features. It works as an interactive terminal TUI
and as a non-interactive command-line tool.

### Presets

| Preset | Intended use | Included | Compiled out or disabled |
| --- | --- | --- | --- |
| `vi-real` | Small vi-style binary for VT-compatible terminals, recovery environments, remote machines, and minimal installations | Core modal editing, welcome screen, tabs, built-in file and buffer browsers | Colors, modern bindings, nested panes, auxiliary views, Git, search/indexing, formatters, clipboard, assembly docs, LSP, tests, and compile database output |
| `vi-min` | Small colored terminal editor with a few modern conveniences | `vi-real` editing/browser base plus terminal colors, color tools, modern bindings, and nested panes | Git, fuzzy/grep/regex tools, formatters, clipboard, auxiliary views, assembly docs, LSP, tests, and compile database output |
| `minimal` | Compact modern editor without language servers | Browsers, auxiliary views, Git, cached search, formatters, clipboard, colors, and nested panes | LSP, assembly docs, struct-size popup, tests, and compile database output |
| `basic` | Normal development build when LSP support is not needed | Editor features, browsers, Git, search, formatters, clipboard, assembly docs, auxiliary views, tests, and compile database output | All language-server clients |
| `full` | Complete uVim development environment | All editor features, tests, compile database output, and every available LSP client | Sanitizers, debug logging, stripping, and static linking remain opt-in build modes |

`vi-real`, `vi-min`, and `minimal` select `-Oz`, LTO, dead-code section
collection, and binary stripping. `basic` and `full` default to Release with
`-O2`, LTO, and dead-code section collection. Presets are starting points;
later `--enable` and `--disable` arguments override individual choices.

The `full` preset compiles the LSP integrations into uVim. Language servers
such as `clangd` and the VSCode language servers remain separate programs and
must be installed on the target system when those integrations are used.

### Quick start: interactive configurator

On macOS, Linux, or WSL, `bootstrap.sh` builds the configurator and prompts to
run it and then build uVim:

```sh
./bootstrap.sh
```

The equivalent explicit steps are:

```sh
./bootstrap.sh
./build/uvim-config
./build.sh
```

On native Windows PowerShell:

Use `.\bootstrap.ps1`, `.\build\uvim-config.exe`, and `.\build.ps1` for the
same workflow in native Windows PowerShell.

`bootstrap` builds only the configurator and offers to launch it. In the TUI:

- use `j`/`k` or the arrow keys to move
- use `h`/`l` to close or open a section
- press Space or Enter to toggle/cycle an option
- press Space or Enter on build jobs or install directory to edit the value
- press `s` to save and `q` to quit

The first save creates two files in the selected build directory:

- `uvim-config.conf` is the editable/importable profile state
- `uvim_config_cache.cmake` is the generated CMake cache initializer

The TUI reloads `uvim-config.conf` on the next run. `build.sh` imports it,
regenerates the CMake cache, configures the project, and builds with the saved
parallel job count.

### Quick start: preset from the command line

First bootstrap `uvim-config` and decline the prompt to open its TUI, then
select a preset from the command line:

```sh
./bootstrap.sh
./build/uvim-config --preset full --config Release --jobs 8
./build.sh
```

Build the reduced vi-style profile:

```sh
./build/uvim-config --preset vi-real --config Release -O Oz --jobs 8
./build.sh --target uvim
```

For the smallest profile, the built-in file and buffer browsers can also be
removed:

```sh
./build/uvim-config --preset vi-real --disable browser-tools
./build.sh --target uvim
```

Build and install the full profile on POSIX:

```sh
./build/uvim-config --preset full --install-dir ~/.local/bin --install
./build.sh --install
```

Equivalent PowerShell commands use `uvim-config.exe` and `build.ps1`.

### Terminal target selection

`--platform AUTO` is recommended and selects the native backend for the build
host. Use `--platform POSIX` for Unix terminals and PTYs, or `--platform WIN32`
for a native Windows console build. The Windows backend enables virtual
terminal processing and writes UTF-8 console text, allowing the same editor UI
to run in Windows Terminal and other compatible terminal hosts.

The `vi-real` preset disables optional terminal coloring but still requires a
terminal capable of full-screen cursor movement and raw key input. It is aimed
at VT-compatible terminal emulators rather than line-only or `TERM=dumb`
sessions.

Examples:

```sh
./build/uvim-config -p vi-real --platform POSIX -O Oz
./build/uvim-config -p full --platform AUTO -c RelWithDebInfo
```

```powershell
.\build\uvim-config.exe -p vi-real --platform WIN32 -O Oz
.\build.ps1 --target uvim
```

### Customize a preset

Options after `--preset` modify the selected profile:

```sh
# Compact editor with project search, but without Git or clipboard support.
./build/uvim-config -p minimal --disable git --disable clipboard

# Basic build plus only the C/C++ language server client.
./build/uvim-config -p basic --enable clangd

# Reload a saved profile and change one output option.
./build/uvim-config --import build/uvim-config.conf --disable tests
```

Available feature names include `browser-tools`, `auxiliary-views`, `git`,
`search`, `rg-cache`, `formatters`, `clipboard`, `asm-docs`, `struct-size`,
`color-tools`, `terminal-colors`, `modern-keybindings`, `multi-pane-splits`,
`per-pane-lsp`, `clangd`, `robot-lsp`, `python-lsp`, `mlang-lsp`,
`mlang-semantic-tokens`, `html-lsp`, `css-lsp`, `json-lsp`, `ts-lsp`, `tests`,
`compile-commands`, `auto-build-number`, `lto`, `gc-sections`, `strip`,
`static-link`, `sanitizers`, `debug-logging`, and `debug-lsp`.

Run this for the authoritative option list:

```sh
./build/uvim-config --help
```

Other useful arguments are `--build-dir`, `--source-dir`, `--output`,
`--import`, `--jobs`, `--ninja`/`--no-ninja`, `--install-dir`, and `--install`.
For example, an isolated build directory can be used throughout:

```sh
./bootstrap.sh --build-dir out/full --jobs 8
./out/full/uvim-config -p full -B out/full -j 8
./build.sh --build-dir out/full
```

The generated cache can also be used directly:

```sh
cmake -C build/uvim_config_cache.cmake -S . -B build
cmake --build build --parallel 8
```

### Direct CMake configuration

Advanced builds may bypass `uvim-config` and set the `UVIM_*` CMake options
directly. For example:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DUVIM_MINIMAL=ON \
  -DUVIM_ENABLE_BROWSER_TOOLS=ON \
  -DUVIM_OPTIMIZATION_LEVEL=Oz
cmake --build build --target uvim --parallel 8
```

See the options near the top of `CMakeLists.txt` or the generated configuration
reference for the complete list. `UVIM_MINIMAL=ON` is a hard compile gate that
forces most optional integrations off; use the individual feature switches
when constructing a custom profile between `vi-real` and `full`.

### Portable macOS and Linux archives

Build a release archive for the current host:

```sh
./scripts/build_portable.sh
```

The default `full` profile is packaged as a ZIP file under `dist/` with the executable,
themes, README, license, dependency report, and SHA-256 checksum. Linux
requests a statically linked executable by default. macOS validates that the
binary links only to Apple system libraries; use `--macos-arch universal` to
build a combined `arm64` and `x86_64` executable. Portable macOS builds target
macOS 13.3 or newer by default:

```sh
./scripts/build_portable.sh --profile basic --macos-arch universal
```

Run the script on macOS for a macOS archive and on Linux for a Linux archive.
Use `--dynamic` if a Linux toolchain does not provide static runtime libraries.
Git, ripgrep/fzf, formatters, clipboard helpers, and language servers remain
optional external programs discovered through `PATH`.

Pushing a tag matching `v*` runs the portable-release GitHub Actions workflow,
builds Linux `x86_64` and universal macOS ZIP archives, creates the GitHub
Release, and attaches both archives and their checksums. Release tags must use
`vMAJOR.MINOR.BUGFIX`, for example `v0.2.1`. The public release keeps that
three-part version. Build numbers are never included in release asset names:
the ZIP files are `uvim-v0.2.1-linux-x86_64.zip` and
`uvim-v0.2.1-macos-universal.zip`.

The macOS job also creates `uvim-v0.2.1-macos-universal.pkg`. It installs the
universal executable as `/usr/local/bin/uvim`. The workflow builds this as an
unsigned installer by default, so it does not require a paid Apple Developer
membership or repository secrets. macOS may require the user to explicitly
approve the installer in Privacy & Security. If signing is added later, public
distribution should use a Developer ID Installer certificate and Apple
notarization.

#### Installing the unsigned macOS package

Only bypass Gatekeeper for a package downloaded from the official uvim GitHub
Release. Download both the `.pkg` and its matching `.pkg.sha256` file, then
verify them from the download directory:

```sh
shasum -a 256 -c uvim-v0.2.1-macos-universal.pkg.sha256
```

After a successful checksum, double-click the `.pkg`. If macOS blocks the
unsigned installer, leave the warning open or dismiss it, open **System
Settings > Privacy & Security**, scroll to **Security**, and click **Open
Anyway** for the uvim package. Authenticate when prompted and confirm the
installer again. Apple makes **Open Anyway** available for about an hour after
the blocked launch attempt. Do not disable Gatekeeper globally. See Apple's
[security override instructions](https://support.apple.com/guide/mac-help/open-an-app-by-overriding-security-settings-mh40617/mac).

To add binaries to a release that already exists, open **Actions**, select
**Portable release binaries**, choose **Run workflow**, enter the release's
exact tag (for example `v0.2.1`), select the profile, and run it. The workflow
uploads or replaces the ZIP files and checksums on that release.

## Runtime configuration

Generate the default runtime config:

```sh
uvim --init-config
```

This writes `~/.config/uvim/config.toml`, or
`$XDG_CONFIG_HOME/uvim/config.toml` when `XDG_CONFIG_HOME` is set, and copies
the bundled themes into the adjacent `themes/` directory. A custom destination
may be supplied explicitly:

```sh
uvim --init-config /path/to/uvim.toml
```

Build configuration and runtime configuration are separate: `uvim-config`
chooses which code is compiled into the binary, while `config.toml`, command
line flags, and `:set` options control the compiled features at runtime.

## Themes

Built-in theme examples are in `themes/`. Copy a theme into the runtime theme
directory and reference it in `config.toml`:

```toml
[theme]
name = "solarized-dark"
```

A custom theme can also be loaded directly:

```sh
uvim --theme /path/to/theme.toml
```

## Generated documentation

Generate the configuration/API reference with:

```sh
./scripts/build_docs.sh
```

The output is written to `docs/doxygen/html/index.html`. Published docs are
available at <https://mattilaa.github.io/uvim/>. Doxygen can be installed with
the platform package manager (`brew install doxygen`, `apt-get install
doxygen`, or `dnf install doxygen`).

## Assembly instruction docs

Assembly instruction `gd` support is controlled by the CMake option
`UVIM_ENABLE_ASM_DOCS`. It is enabled by default.

Build with the default:

```sh
cmake -S . -B build
cmake --build build --parallel 8
```

Or enable it explicitly:

```sh
cmake -S . -B build -DUVIM_ENABLE_ASM_DOCS=ON
cmake --build build --parallel 8
```

To disable it:

```sh
cmake -S . -B build -DUVIM_ENABLE_ASM_DOCS=OFF
cmake --build build --parallel 8
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
- Mlang LSP semantic-token coloring is on by default when Mlang LSP is built
  and enabled; disable it with `editor.syntax.mlang.semantic_tokens: false`
  or `:set nosyntax.mlang.semantic_tokens`.
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
- LSP diagnostics from servers are shown as row markers and can be opened with
  `Space-e` when the cursor is on a diagnostic row.
- Active-LSP diagnostics can be collected into the same kind of jump list with
  `Space-m e` for errors and `Space-m w` for warnings. In C/C++ files this uses
  clangd; in mlang files this uses mlangd.
- Use `:set emitlsp=false` or config `editor.emitlsp: false` to suppress
  LSP-emitted warning/error diagnostics without disabling other LSP features.

### uvim logging

uvim logging is compiled out by default. Build with one of these CMake options
to enable it:

```sh
cmake -S . -B build -DUVIM_DEBUG_LSP=ON
cmake --build build --parallel 8
```

Use `UVIM_DEBUG_LSP` for LSP startup/stderr/debug logging, or
`UVIM_DEBUG_LOGGING` for broader editor debug logging. LSP log rows include the
server signature after the timestamp, for example `[CLANGD]`, `[PYTHON]`, or
`[TS]`.

Default log locations:

- POSIX: `/tmp/uvim.log`
- Windows: `%USERPROFILE%\Documents\uvim\uvim.log`

Override the log file at runtime with:

```sh
uvim --log-file /path/to/uvim.log file.cpp
```

When logging is not enabled in the build, `--log-file` is accepted but no uvim
log rows are written.

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

Open with `Space-x` (leader-x) or `:e .`. In the browser, use `:hs` for a
horizontal browser split and `:vs` for a vertical browser split.

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
- `q` - close the active browser pane; the final pane exits to editor/welcome
- `Q` - exit browser and save the current browser pane layout

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
- `Ctrl-Shift-H` / `Ctrl-Shift-L` - same as above when no split is active.
  When multi-pane splits are enabled and a split is active,
  `Ctrl-Shift-H/J/K/L` jumps between panes.
  Requires a terminal that supports either xterm `modifyOtherKeys=2` or
  the kitty keyboard protocol (kitty, ghostty, wezterm, alacritty,
  iTerm2 with "Report modifiers using CSI u" enabled, recent xterm,
  tmux with `xterm-keys on`). uvim requests both protocols on startup.
  In Apple Terminal these combos still arrive as plain `Ctrl-H`/`Ctrl-L`
  and just switch buffers — use the leader binding there.
- `Ctrl-h` / `Ctrl-l` - previous / next buffer (unchanged).
- `Ctrl-w` - browse open buffers for the active pane. Press `Enter` to open
  the selected buffer only in that pane.
- `Space-w c` - close the active split pane (`:close` and `:clo` are equivalent).

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

Fuzzy file finder:

- `Ctrl-p` - open fuzzy file finder
- `Ctrl-o` - inside fuzzy finder, toggle filename-first ranking

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
