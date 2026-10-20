#!/usr/bin/env python3
"""Run a hands-free uvim demo in the current terminal, ready to record."""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

from uvim_control import UvimClient


SOURCE = '''"""A small greeting app, edited live by Python controlling uvim."""


def greet(name):
    return f"Hello, {name}!"


def main():
    for name in ["Ada", "Grace", "Linus"]:
        print(greet(name))


if __name__ == "__main__":
    main()
'''


def demonstrate(editor, speed=1):
    """Edit this sequence to choreograph your own recording.

    speed=2 runs twice as fast. Pauses leave time for viewers to read.
    All edits go through ordinary uvim keys and commands.
    """
    def pause(seconds=1.5):
        time.sleep(seconds / speed)

    def type_text(text):
        editor.type_text(text, interval=0.055 / speed)

    def command(text):
        editor.command(text, interval=0.09 / speed)
        pause()

    pause(2)
    # Navigate, then search for the greeting.
    editor.keys("gg")
    pause()
    editor.keys("/")
    type_text("Hello")
    pause(0.7)
    editor.keys("\r")
    pause()
    # Replace a word using a Vim text object.
    editor.keys("ciw")
    type_text("Welcome")
    editor.keys("\x1b")
    pause()
    # Undo and redo, with a beat to see each result.
    editor.keys("u")
    pause()
    editor.keys("\x12")  # Ctrl-R
    pause()
    # Select the function, then return to normal mode.
    editor.keys("gg3jVj")
    pause(2)
    editor.keys("\x1b")
    # Add a visible comment above the function (no auto-indent dependency).
    editor.keys("ggO")
    type_text("# This live edit is driven entirely by a Python script.")
    editor.keys("\x1b")
    pause()
    command("w")
    # Show the same file in two panes, then return to one pane.
    command("vs")
    editor.keys("G")
    pause(2)
    command("close")
    editor.keys("gg")
    pause(2)


def positive_float(value):
    result = float(value)
    if not 0 < result < float("inf"):
        raise argparse.ArgumentTypeError("must be a finite positive number")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--uvim", default=str(Path(__file__).resolve().parents[1] / "build/uvim"))
    parser.add_argument("--speed", type=positive_float, default=1,
                        help="playback multiplier (default: 1)")
    parser.add_argument("--countdown", type=positive_float, default=3,
                        help="seconds before opening uvim (default: 3)")
    parser.add_argument("--workdir", type=Path,
                        help="create this NEW directory for demo files; otherwise use /tmp")
    parser.add_argument("--exit", action="store_true",
                        help="quit after playback; normally leave uvim open for you")
    args = parser.parse_args()
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        parser.error("run this demo in a terminal so uvim is visible")
    executable = shutil.which(args.uvim)
    if executable is None:
        parser.error(f"uvim not found: {args.uvim}; build it or pass --uvim PATH")

    if args.workdir:
        workspace = args.workdir.resolve()
        workspace.mkdir(parents=True, exist_ok=False)
    else:
        workspace = Path(tempfile.mkdtemp(prefix="uvim-demo-"))
    (workspace / "demo.py").write_text(SOURCE, encoding="utf-8")
    print(f"Demo files: {workspace}")
    print("Record this terminal. Playback ends with uvim open; :qa quits, Ctrl-] aborts.")
    print(f"Starting in {args.countdown:g} seconds...", flush=True)
    time.sleep(args.countdown)

    bridge = Path(__file__).with_name("uvim_control.py")
    # Keep socket paths short (macOS has a ~104 byte Unix socket path limit).
    with tempfile.TemporaryDirectory(prefix="uvim-", dir="/tmp") as control_dir:
        socket_path = Path(control_dir) / "control.sock"
        process = subprocess.Popen(
            [sys.executable, str(bridge), "--socket", str(socket_path),
             "--", executable, "demo.py"], cwd=workspace,
        )
        try:
            editor = UvimClient(socket_path)
            deadline = time.monotonic() + 15
            while True:
                if process.poll() is not None:
                    raise RuntimeError("uvim bridge exited before startup")
                try:
                    editor.wait_for("demo.py", timeout=0.2)
                    break
                except (OSError, TimeoutError):
                    if time.monotonic() >= deadline:
                        raise TimeoutError("uvim did not start within 15 seconds")
                    time.sleep(0.05)
            demonstrate(editor, args.speed)
            if args.exit:
                editor.command("qa!", interval=0)
            # Leave the bridge running so the user can inspect/edit the result.
            return process.wait()
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            print(f"\nDemo files kept at {workspace}")


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, TimeoutError, KeyboardInterrupt) as error:
        sys.exit(f"Demo stopped: {error}")
