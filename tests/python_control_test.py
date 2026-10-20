"""Integration checks using the real uvim binary (macOS/Linux, stdlib only).

Run: python3 -B tests/python_control_test.py [path/to/uvim]
"""

import errno
import os
from pathlib import Path
import pty
import select
import signal
import subprocess
import sys
import tempfile
import termios
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from demo_uvim import SOURCE
from uvim_control import UvimClient

BINARY = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else ROOT / "build/uvim"


class PythonControlTest(unittest.TestCase):
    def wait_until(self, predicate, timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(0.05)
        self.fail("timed out waiting for editor result")

    def test_external_client_and_shutdown(self):
        with tempfile.TemporaryDirectory(dir="/tmp", prefix="uvim-test-") as directory:
            workspace = Path(directory)
            target = workspace / "example.py"
            target.write_text("original\n")
            socket_path = workspace / "control.sock"
            with tempfile.TemporaryFile() as output:
                process = subprocess.Popen(
                    [sys.executable, "-B", str(ROOT / "scripts/uvim_control.py"),
                     "--socket", str(socket_path), "--", str(BINARY), str(target)],
                    stdin=subprocess.DEVNULL, stdout=output, stderr=output,
                )
                try:
                    self.wait_until(socket_path.exists)
                    client = UvimClient(socket_path)
                    client.wait_for("example.py")
                    with self.assertRaises(RuntimeError):
                        client._request(op="invalid")
                    client.keys("ggciw")
                    client.type_text("controlled from Python", interval=0)
                    client.command("w", interval=0)
                    self.wait_until(lambda: target.read_text() == "controlled from Python\n")
                    # A normal-mode deletion is one undo step; insert-mode
                    # character grouping is an editor policy, not the bridge's.
                    client.keys("ggx")
                    client.command("w", interval=0)
                    self.wait_until(lambda: target.read_text() == "ontrolled from Python\n")
                    client.keys("u")
                    client.command("w", interval=0)
                    self.wait_until(lambda: target.read_text() == "controlled from Python\n")
                    client.keys("\x12")
                    client.command("w", interval=0)
                    self.wait_until(lambda: target.read_text() == "ontrolled from Python\n")
                    client.command("qa", interval=0)
                    self.assertEqual(process.wait(timeout=10), 0)
                    self.assertFalse(socket_path.exists())
                finally:
                    if process.poll() is None:
                        process.terminate()
                        process.wait(timeout=5)
                    output.seek(0)
                    transcript = output.read().decode(errors="replace")
                    self.assertIn("example.py", transcript)

    def test_visible_demo_and_interrupt_cleanup(self):
        # Give the demo an actual outer terminal, as it requires for recording.
        for interrupt in (False, True):
            with self.subTest(interrupt=interrupt), tempfile.TemporaryDirectory(
                dir="/tmp", prefix="uvim-video-test-"
            ) as directory:
                master, slave = pty.openpty()
                before = termios.tcgetattr(slave)
                workspace = Path(directory) / "demo"
                process = subprocess.Popen(
                    [sys.executable, "-B", str(ROOT / "scripts/demo_uvim.py"),
                     "--uvim", str(BINARY), "--speed", "8", "--countdown", "0.01",
                     "--workdir", str(workspace), "--exit"],
                    stdin=slave, stdout=slave, stderr=slave, start_new_session=True,
                )
                transcript = bytearray()
                try:
                    deadline = time.monotonic() + 25
                    sent_interrupt = False
                    while process.poll() is None and time.monotonic() < deadline:
                        if select.select([master], [], [], 0.1)[0]:
                            try:
                                transcript.extend(os.read(master, 65536))
                            except OSError as error:
                                if error.errno != errno.EIO:
                                    raise
                        if interrupt and not sent_interrupt and b"demo.py" in transcript:
                            # Terminating the demo also terminates its bridge,
                            # which must restore the terminal in its finally.
                            process.send_signal(signal.SIGINT)
                            sent_interrupt = True
                    self.assertIsNotNone(process.poll(), transcript[-2000:])
                    self.assertEqual(termios.tcgetattr(slave), before)
                    if not interrupt:
                        self.assertEqual(process.returncode, 0, transcript[-2000:])
                        expected = "# This live edit is driven entirely by a Python script.\n"
                        expected += SOURCE.replace("Hello", "Welcome")
                        self.assertEqual((workspace / "demo.py").read_text(), expected)
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait()
                    os.close(master)
                    os.close(slave)


if __name__ == "__main__":
    unittest.main()
