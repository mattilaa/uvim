# Python control and automated video demos

Run this from a terminal on **macOS or Linux with Python 3.9+** and a built
uvim binary. No pip packages are needed.

```sh
python3 scripts/demo_uvim.py
```

Start your screen recorder during the three-second countdown. The script
opens the real uvim in your current terminal and plays a roughly two-minute
feature tour:

- Filter the command palette and open the file browser.
- Navigate, search, replace a word, undo/redo and select lines visually.
- Insert a comment and review the unsaved-change diff before saving.
- Find a test file with Ctrl-P and its code preview.
- Switch files through the buffer browser.
- Search the project with Ctrl-A and navigate matching results.
- Open a vertical split and move through the code.
- Run the edited greeting app and its two tests inside uvim's output view.
- Visit the built-in help, then return to the edited file.

Use a terminal at least 100 columns by 30 rows for a clear recording. The full
tour assumes the normal full build with browser, search, Git tools, splits and
help enabled. The unsaved diff is skipped if `git` is unavailable; it does not
need a Git repository. Project search can use uvim's built-in fallback when
ripgrep is unavailable. LSP servers and formatters are not required.

For the original roughly 40-second editing and split demo:

```sh
python3 scripts/demo_uvim.py --tour basic
```

The demo creates a fresh temporary workspace and prints its location; the
files are kept afterwards, including a README, example JSON settings and the
greeting tests. It leaves uvim open so you can inspect the result.
Type `:qa` to quit, or press **Ctrl-]** to abort the bridge. Your terminal
settings are restored on exit. To change the pace or binary:

```sh
python3 scripts/demo_uvim.py --uvim ./build/uvim --speed 1.5 --countdown 5
python3 scripts/demo_uvim.py --workdir /tmp/my-new-uvim-demo --exit
```

`--workdir` must name a directory that does not exist, to protect existing
files. `--exit` quits after playback. Edit `demonstrate()` in
[`scripts/demo_uvim.py`](../scripts/demo_uvim.py) to choreograph your video.
Playback uses timed pauses; slower machines may need a lower `--speed`.
The full tour also waits for the app output and test summary before continuing.
The scripts do not record video themselves.

## Control from another Python process

The bridge starts uvim in a pseudoterminal (PTY), mirrors its output to the
visible terminal and relays both keyboard and external input. This uses the
existing editor input path; no special uvim build or editor-core changes are
needed. It cannot attach to an editor that was started without the bridge.
Native Windows is not supported by these scripts; Linux under WSL can use them.

In terminal one, create a private control directory and launch the bridge:

```sh
mkdir -m 700 /tmp/my-uvim-control
python3 scripts/uvim_control.py --socket /tmp/my-uvim-control/control.sock -- ./build/uvim example.py
```

In terminal two, run this from the repository root, or put the `scripts`
directory on your Python path:

```python
import sys
sys.path.insert(0, "scripts")
from uvim_control import UvimClient

editor = UvimClient("/tmp/my-uvim-control/control.sock")
editor.wait_for("example.py")
editor.keys("gg")                    # normal-mode navigation
editor.keys("i")                     # enter insert mode
editor.type_text("# Hello from Python!\n", interval=0.06)
editor.keys("\x1b")                  # Escape, with escape-sequence timing handled
editor.command("w")                  # visibly type :w and Enter
```

`keys()` sends literal characters/control codes: `\r` is Enter, `\x1b` is
Escape and `\x12` is Ctrl-R. Send standalone Escape in its own call, so it
cannot be interpreted as an Alt chord. `type_text()` types in the current
mode, with newlines converted to Enter; normal editor auto-indent and
bindings apply. `command()` leaves the current mode with Escape, opens `:`,
types the command and presses Enter. Do not type on the keyboard while an
automated sequence is running, since both inputs share the editor state.

Calls acknowledge delivery to the PTY, not completion of an editor action.
`wait_for()` searches recent raw output (including historical frames and ANSI
codes), which is useful for startup. For a reliable save check, inspect the
resulting file. Large operations and optional integrations may require extra
pauses or checks. This is terminal automation, not a buffer inspection RPC API.

The socket has mode `0600`, is removed on exit and is never overwritten if
it already exists. Use a private parent directory as above: a process that
can access this socket can perform any editor action, including commands.

## Verification

The integration checks use the real uvim binary, including a complete demo
inside a PTY, saved-file assertions, checks for picker/search/output views and
terminal restoration after interruption:

```sh
python3 -B tests/python_control_test.py ./build/uvim
```

For a reduced build without the full-tour features, append `--basic-only`.
