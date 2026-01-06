import os
import re
import time

import pexpect


class UvimTui:
    def __init__(self):
        self.child = None

    def start_uvim(self, binary, *args):
        self._spawn(binary, None, *args)

    def start_uvim_in_dir(self, binary, workdir, *args):
        self._spawn(binary, workdir, *args)

    def _spawn(self, binary, workdir, *args):
        if self.child and self.child.isalive():
            self.stop_uvim()
        env = os.environ.copy()
        env.setdefault("TERM", "xterm-256color")
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

    def expect_mode(self, mode, timeout=5.0):
        target = f" {mode} | "
        self._wait_for_text(target, timeout)

    def expect_text(self, text, timeout=5.0):
        self._wait_for_text(text, timeout)

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
        if not self.child or not self.child.isalive():
            raise RuntimeError("uvim process is not running")


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
