#!/usr/bin/env python3
"""Run a hands-free uvim demo in the current terminal, ready to record."""

import argparse
from pathlib import Path
import shutil
import shlex
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


PROJECT_FILES = {
    "README.md": """# Greeting workshop

A small project for exploring uvim's pickers, buffers and command output.

- demo.py: the greeting app
- tests/test_greetings.py: checks for the edited greeting
- examples/settings.json: example configuration

Try searching the project for greet, or finding test_greetings.py by name.
""",
    "tests/test_greetings.py": '''import unittest

from demo import greet


class GreetingTests(unittest.TestCase):
    def test_greet_ada(self):
        self.assertEqual(greet("Ada"), "Welcome, Ada!")

    def test_greet_grace(self):
        self.assertEqual(greet("Grace"), "Welcome, Grace!")
''',
    "examples/settings.json": '{\n  "greeting": "Welcome",\n  "names": ["Ada", "Grace", "Linus"]\n}\n',
}


def demonstrate(editor, speed=1, tour="full"):
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
    if tour == "full":
        # Discover an action by name and actually execute it.
        command("palette")
        type_text("file browser")
        pause(2)
        editor.keys("\r")
        pause(2)
        editor.keys("j")
        pause()
        editor.keys("q")
        pause()

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
    if tour == "full" and shutil.which("git"):
        # :sc compares the saved file with the buffer; no Git repo needed.
        command("sc")
        pause(2)
        editor.keys("\x1b")
        pause()
    command("w")
    if tour == "full":
        # Filter a project file picker, inspect its preview, then open a file.
        editor.keys("\x10")  # Ctrl-P
        pause()
        type_text("test_greetings.py")
        pause(2)
        editor.keys("\r")
        pause(2)
        # Pick the original file from the open buffer list.
        editor.keys("\x17")  # Ctrl-W
        pause()
        type_text("demo.py")
        pause(2)
        editor.keys("\r")
        pause()
        # Search across project files, moving the result and preview selection.
        editor.keys("\x01")  # Ctrl-A
        pause()
        type_text("greet")
        pause(2)
        editor.keys("\x0a")  # Ctrl-J: next result
        pause(2)
        editor.keys("\x1b")
        pause()

    # Show the same file in two panes, then return to one pane.
    command("vs")
    editor.keys("G")
    pause(2)
    command("close")
    if tour == "full":
        # Run both the edited app and its checks in the browser's output view.
        python = shlex.quote(sys.executable)
        command("e .")
        command(f"run {python} demo.py")
        editor.wait_for("Welcome, Ada!")
        pause(3)
        editor.keys("q")  # output -> browser
        pause()
        command(f"run {python} -m unittest discover -s tests -v")
        editor.wait_for("Ran 2 tests")
        pause(3)
        editor.keys("q")
        pause()
        editor.keys("q")  # browser -> editor
        pause()
        command("help palette")
        pause(2)
        editor.keys("\x1b")
        pause()
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
    parser.add_argument("--tour", choices=("basic", "full"), default="full",
                        help="full feature tour (default) or short editing demo")
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
    for name, content in PROJECT_FILES.items():
        path = workspace / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
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
            demonstrate(editor, args.speed, args.tour)
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
