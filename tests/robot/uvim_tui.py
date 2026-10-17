import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

import pexpect


class UvimTui:
    def __init__(self):
        self.child = None
        self.keylog = None

    def start_uvim(self, binary, *args):
        self._spawn(binary, None, *args)

    def start_uvim_in_dir(self, binary, workdir, *args):
        self._spawn(binary, workdir, *args)

    def _spawn(self, binary, workdir, *args):
        if self.child and self.child.isalive():
            self.stop_uvim()
        env = os.environ.copy()
        env.setdefault("TERM", "xterm-256color")
        # uvim logs every input byte it reads here; shown when a test fails
        # because uvim is gone.
        fd, self.keylog = tempfile.mkstemp(prefix="uvim_keylog_")
        os.close(fd)
        env["UVIM_KEYLOG"] = self.keylog
        self.child = pexpect.spawn(
            binary,
            list(args),
            cwd=workdir,
            env=env,
            encoding="utf-8",
            codec_errors="ignore",
            timeout=5,
        )
        self.child.delaybeforesend = 0.01
        self.child.setwinsize(24, 80)

    def _wait_for_text(self, text, timeout):
        self._ensure_child()
        deadline = time.monotonic() + float(timeout)
        buffer = ""
        while time.monotonic() < deadline:
            try:
                chunk = self.child.read_nonblocking(size=1024, timeout=0.1)
            except pexpect.TIMEOUT:
                chunk = ""
            except pexpect.EOF:
                break
            if chunk:
                buffer += chunk
                if len(buffer) > 65536:
                    buffer = buffer[-65536:]
            cleaned = _strip_ansi(buffer)
            if text in cleaned:
                return
        cleaned_tail = _strip_ansi(buffer)[-200:]
        raise AssertionError(
            f"Timeout waiting for text {text!r}. Last output: {cleaned_tail!r}"
        )

    def _wait_for_raw_text(self, text, timeout):
        self._ensure_child()
        deadline = time.monotonic() + float(timeout)
        buffer = ""
        while time.monotonic() < deadline:
            try:
                chunk = self.child.read_nonblocking(size=1024, timeout=0.1)
            except pexpect.TIMEOUT:
                chunk = ""
            except pexpect.EOF:
                break
            if chunk:
                buffer += chunk
                if len(buffer) > 65536:
                    buffer = buffer[-65536:]
            if text in buffer:
                return
        tail = buffer[-200:]
        raise AssertionError(
            f"Timeout waiting for raw text {text!r}. Last output: {tail!r}"
        )

    def expect_mode(self, mode, timeout=5.0):
        target = f" {mode} | "
        self._wait_for_text(target, timeout)

    def expect_text(self, text, timeout=5.0):
        self._wait_for_text(text, timeout)

    def expect_raw_text(self, text, timeout=5.0):
        self._wait_for_raw_text(text, timeout)

    def send_keys(self, text):
        self._ensure_child()
        self.child.send(text)

    def send_escape(self):
        self._ensure_child()
        self.child.send("\x1b")

    def send_enter(self):
        self._ensure_child()
        self.child.send("\r")

    def send_ctrl(self, char):
        self._ensure_child()
        if not char:
            raise ValueError("send_ctrl requires a character")
        key = char[0].lower()
        code = ord(key) - 96
        if code < 1 or code > 26:
            raise ValueError(f"invalid ctrl key: {char}")
        self.child.send(chr(code))

    def wait_for_exit(self, timeout=10.0):
        if not self.child:
            return
        try:
            self.child.expect(pexpect.EOF, timeout=float(timeout))
        except pexpect.TIMEOUT:
            raise AssertionError(
                f"uvim did not exit within {timeout}s.\n"
                f"Last output: {self.child.before[-300:]!r}\n"
                f"{_stack_sample(self.child.pid)}"
            ) from None
        finally:
            self.child.close(force=True)
            self.child = None

    def stop_uvim(self):
        if not self.child:
            return
        if self.child.isalive():
            try:
                self.child.send(":q\r")
                self.child.expect(pexpect.EOF, timeout=2)
            except Exception:
                self.child.close(force=True)
        self.child = None

    def _ensure_child(self):
        if not self.child:
            raise RuntimeError("uvim process is not running")
        if not self.child.isalive():
            raise RuntimeError(
                "uvim process is not running "
                f"(exit status {self.child.exitstatus}, "
                f"signal {self.child.signalstatus}).\n"
                f"Last output: {self._drain_output()[-300:]!r}\n"
                f"Keys uvim read: {self._keylog_text()!r}"
            )

    def _drain_output(self):
        try:
            return self.child.read_nonblocking(size=100000, timeout=0)
        except Exception:
            return self.child.before or ""

    def _keylog_text(self):
        try:
            with open(self.keylog, encoding="utf-8", errors="replace") as f:
                return f.read()[-300:]
        except Exception:
            return "(no key log)"


def _stack_sample(pid):
    """Where a hung uvim is stuck: a stack sample of every thread."""
    if sys.platform == "darwin" and shutil.which("sample"):
        cmd = ["sample", str(pid), "1"]
    elif shutil.which("gdb"):
        cmd = ["gdb", "-p", str(pid), "-batch", "-ex", "thread apply all bt"]
    else:
        return "(no stack sampler available)"
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=30
        )
    except Exception as error:  # diagnostics must never mask the failure
        return f"(stack sample failed: {error})"
    report = result.stdout or result.stderr
    # macOS `sample`: keep the call graph, not the binary image list.
    report = report.split("Binary Images:")[0]
    return "Stack sample:\n" + report[:12000]


CSI_RE = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]")
OSC_RE = re.compile(r"\x1b\][^\x07]*\x07")


def _strip_ansi(text):
    text = CSI_RE.sub("", text)
    return OSC_RE.sub("", text)


_TUI = UvimTui()


def start_uvim(binary, *args):
    _TUI.start_uvim(binary, *args)


def start_uvim_in_dir(binary, workdir, *args):
    _TUI.start_uvim_in_dir(binary, workdir, *args)


def expect_mode(mode, timeout=5.0):
    _TUI.expect_mode(mode, timeout=timeout)


def expect_text(text, timeout=5.0):
    _TUI.expect_text(text, timeout=timeout)


def expect_raw_text(text, timeout=5.0):
    _TUI.expect_raw_text(text, timeout=timeout)


def send_keys(text):
    _TUI.send_keys(text)


def send_escape():
    _TUI.send_escape()


def send_enter():
    _TUI.send_enter()


def send_ctrl(char):
    _TUI.send_ctrl(char)


def wait_for_exit(timeout=5.0):
    _TUI.wait_for_exit(timeout=timeout)


def stop_uvim():
    _TUI.stop_uvim()
