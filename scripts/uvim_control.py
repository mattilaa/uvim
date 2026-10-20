#!/usr/bin/env python3
"""Display uvim in this terminal and expose its input over a private Unix socket.

No editor changes or third-party Python packages are required. macOS/Linux only.
"""

import argparse
import errno
import fcntl
import json
import os
from pathlib import Path
import pty
import selectors
import shutil
import signal
import socket
import struct
import sys
import termios
import time
import tty


class UvimClient:
    """Control a bridge started with ``uvim_control.py --socket PATH``.

    Calls acknowledge delivery to the terminal, not completion by the editor.
    Use wait_for(), or check the resulting file, when completion matters.
    """

    def __init__(self, socket_path):
        self.socket_path = str(socket_path)

    def _request(self, **request):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(5)
            connection.connect(self.socket_path)
            connection.sendall(json.dumps(request).encode() + b"\n")
            with connection.makefile("rb") as stream:
                response = json.loads(stream.readline())
        if "error" in response:
            raise RuntimeError(response["error"])
        return response

    def keys(self, keys):
        """Send literal keys, e.g. 'gg', '\\x1b', '\\r', or '\\x12' (Ctrl-R)."""
        self._request(op="keys", text=keys)
        # uvim distinguishes a standalone Escape from Alt-key sequences.
        if keys.endswith("\x1b"):
            time.sleep(0.08)

    def type_text(self, text, interval=0.04):
        """Type in the CURRENT mode; this does not enter insert mode for you."""
        if interval < 0:
            raise ValueError("interval must be nonnegative")
        for char in text.replace("\r\n", "\n").replace("\n", "\r"):
            self.keys(char)
            time.sleep(interval)

    def command(self, command, interval=0.04):
        """Leave the current mode and visibly type an Ex command (without ':')."""
        if any(char in command for char in "\r\n\x1b"):
            raise ValueError("command must be a single line without Escape")
        self.keys("\x1b")
        self.keys(":")
        self.type_text(command, interval)
        self.keys("\r")

    def wait_for(self, text, timeout=10):
        """Wait for text in recent terminal OUTPUT (not a parsed screen).

        Previously drawn text can match. Useful for initial readiness; it is
        not a general assertion about the current buffer or cursor position.
        """
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if text in self._request(op="output")["text"]:
                return
            time.sleep(0.05)
        raise TimeoutError(f"uvim did not output {text!r} within {timeout}s")


def _write_all(fd, data):
    while data:
        count = os.write(fd, data)
        data = data[count:]


def serve(socket_path, argv):
    """Relay a real PTY to the terminal and accept newline-delimited JSON."""
    executable = shutil.which(argv[0])
    if executable is None:
        raise FileNotFoundError(f"uvim executable not found: {argv[0]}")
    socket_path = Path(socket_path)
    # Bind before starting uvim; never remove/replace an existing socket.
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    listener.bind(str(socket_path))
    os.chmod(socket_path, 0o600)
    listener.listen(8)
    listener.setblocking(False)
    old_terminal = None
    old_resize = signal.getsignal(signal.SIGWINCH)
    old_terminate = signal.getsignal(signal.SIGTERM)
    child = master = None
    selector = selectors.DefaultSelector()
    connections = {}
    recent = bytearray()
    child_status = None

    def resize(*_):
        size = shutil.get_terminal_size((100, 30))
        fcntl.ioctl(master, termios.TIOCSWINSZ,
                    struct.pack("HHHH", size.lines, size.columns, 0, 0))

    def disconnect(connection):
        selector.unregister(connection)
        connections.pop(connection, None)
        connection.close()

    def terminate(*_):
        raise KeyboardInterrupt

    try:
        child, master = pty.fork()
        if child == 0:
            try:
                os.environ.setdefault("TERM", "xterm-256color")
                os.execv(executable, argv)
            except BaseException as error:
                os.write(2, f"Cannot start uvim: {error}\n".encode())
                os._exit(127)
        resize()
        signal.signal(signal.SIGWINCH, resize)
        signal.signal(signal.SIGTERM, terminate)
        selector.register(master, selectors.EVENT_READ, "pty")
        selector.register(listener, selectors.EVENT_READ, "listener")
        if sys.stdin.isatty():
            old_terminal = termios.tcgetattr(sys.stdin.fileno())
            tty.setraw(sys.stdin.fileno())
            selector.register(sys.stdin, selectors.EVENT_READ, "stdin")

        running = True
        while running:
            for key, _ in selector.select(timeout=0.1):
                if key.data == "pty":
                    try:
                        data = os.read(master, 65536)
                    except OSError as error:
                        if error.errno != errno.EIO:
                            raise
                        data = b""
                    if not data:
                        running = False
                        break
                    recent.extend(data)
                    del recent[:-262144]
                    _write_all(sys.stdout.fileno(), data)
                elif key.data == "stdin":
                    data = os.read(sys.stdin.fileno(), 1024)
                    if not data or b"\x1d" in data:  # Ctrl-] is the bridge escape.
                        running = False
                        break
                    _write_all(master, data)
                elif key.data == "listener":
                    connection, _ = listener.accept()
                    connection.setblocking(False)
                    connections[connection] = bytearray()
                    selector.register(connection, selectors.EVENT_READ, "client")
                else:
                    connection = key.fileobj
                    try:
                        data = connection.recv(4096)
                    except ConnectionResetError:
                        data = b""
                    if not data:
                        disconnect(connection)
                        continue
                    buffer = connections[connection]
                    buffer.extend(data)
                    if len(buffer) > 16384:
                        disconnect(connection)
                        continue
                    if b"\n" not in buffer:
                        continue
                    try:
                        request = json.loads(buffer.split(b"\n", 1)[0])
                        if request["op"] == "keys":
                            data = request["text"].encode("utf-8")
                            if len(data) > 1024:
                                raise ValueError("keys is limited to 1024 bytes; use type_text")
                            _write_all(master, data)
                            response = {"ok": True}
                        elif request["op"] == "output":
                            response = {"text": recent.decode("utf-8", errors="replace")}
                        else:
                            raise ValueError("unknown operation")
                    except (ValueError, KeyError, TypeError, AttributeError) as error:
                        response = {"error": str(error)}
                    try:
                        connection.settimeout(0.2)
                        connection.sendall(json.dumps(response).encode() + b"\n")
                    except OSError:
                        pass  # A disconnected client must not stop the editor.
                    disconnect(connection)
    finally:
        signal.signal(signal.SIGWINCH, old_resize)
        signal.signal(signal.SIGTERM, old_terminate)
        if old_terminal is not None:
            termios.tcsetattr(sys.stdin.fileno(), termios.TCSADRAIN, old_terminal)
        # Restore visual terminal state even if the child crashed.
        if sys.stdout.isatty():
            _write_all(sys.stdout.fileno(),
                       b"\x1b[0m\x1b[?25h\x1b[?2004l\x1b[?1049l")
        for connection in list(connections):
            connection.close()
        selector.close()
        listener.close()
        socket_path.unlink(missing_ok=True)
        if master is not None:
            os.close(master)
        if child and child_status is None:
            # The bridge owns this child. HUP then bounded escalation avoids
            # leaving an editor behind when a recording is interrupted.
            for sig in (None, signal.SIGHUP, signal.SIGTERM, signal.SIGKILL):
                if sig is not None:
                    try:
                        os.kill(child, sig)
                    except ProcessLookupError:
                        pass
                deadline = time.monotonic() + 0.5
                while time.monotonic() < deadline:
                    waited, status = os.waitpid(child, os.WNOHANG)
                    if waited:
                        child_status = status
                        break
                    time.sleep(0.02)
                if child_status is not None:
                    break
    return os.waitstatus_to_exitcode(child_status) if child_status is not None else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket", required=True,
                        help="new socket path, preferably inside a private directory")
    parser.add_argument("editor", nargs=argparse.REMAINDER,
                        help="uvim executable and arguments, after --")
    args = parser.parse_args()
    argv = args.editor
    if argv[:1] == ["--"]:
        argv = argv[1:]
    try:
        return serve(args.socket, argv or ["uvim"])
    except (OSError, ValueError) as error:
        parser.exit(1, f"uvim control: {error}\n")
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
