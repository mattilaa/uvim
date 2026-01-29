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
if `XDG_CONFIG_HOME` is set). You can also provide a custom path:

```sh
uvim --init-config /path/to/uvim.yaml
```

## LSP flags

- `--clangd` enable clangd LSP
- `--robot-lsp` enable Robot Framework LSP
- `--python-lsp` enable Python LSP
- `--mlang-lsp` enable Mlang LSP (expects the Mlang repo `tools/mlang_lsp`)

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
