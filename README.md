# uVim
uViM (micro) editor

Reduced version of Vim.

- Normal Vim like code navigation
- Vim like text object handling
- Fzf style file browsing
- Ripgrep search from files
- Syntax highlighting
- Really small binary

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
