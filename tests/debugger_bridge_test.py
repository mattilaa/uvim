"""Transport tests; set UVIM_DEBUGGER_LIVE_TESTS=1 to exercise installed LLDB."""
import importlib.util
import json
import os
from pathlib import Path
import queue
import select
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import MagicMock, patch
from types import SimpleNamespace

SOURCE = Path(__file__).resolve().parents[1] / 'src/debugger_bridge.py'
spec = importlib.util.spec_from_file_location('debugger_bridge', SOURCE)
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)


class BridgeTests(unittest.TestCase):
    def test_mi_nested_records_and_escapes(self):
        value = bridge.mi_fields('frame={level="0",fullname="/tmp/a b.c",line="12"},variables=[{name="count",value="42"},{name="text",value="\\\"hello\\\""}]')
        self.assertEqual(value['frame']['fullname'], '/tmp/a b.c')
        self.assertEqual(value['variables'][1]['value'], '"hello"')
        self.assertEqual(bridge.mi_value('"a\\012b"')[0], 'a\nb')
        self.assertEqual(bridge.mi_value('[frame={level="0"},frame={level="1"}]')[0], [{'level': '0'}, {'level': '1'}])

    def test_gdb_stop_refreshes_panes_and_clears_them_on_continue(self):
        gdb = bridge.GDB.__new__(bridge.GDB)
        gdb.lines = queue.Queue()
        gdb.pending = {'1': 'variables', '2': 'threads', '3': 'frame'}
        gdb.master = -1
        for line in ['*stopped,reason="breakpoint-hit"',
                     '1^done,variables=[{name="count",type="int",value="42"}]',
                     '2^done,threads=[{id="7",name="worker",state="stopped"}]',
                     '3^done,frame={fullname="/tmp/test.c",line="5"}', '*running,thread-id="all"']:
            gdb.lines.put(line)
        with patch.object(gdb, 'send') as send, patch.object(bridge, 'emit') as emit:
            gdb.poll()
            send.assert_any_call('-stack-list-frames 0 63', 'source-frames')
            messages = [call.kwargs for call in emit.call_args_list]
            self.assertIn({'variables': ['count: int = 42']}, messages)
            self.assertIn({'path': '/tmp/test.c', 'line': 5, 'frame': 0, 'function': ''}, messages)
            self.assertEqual(messages[-1], {'state': 'running', 'variables': [], 'threads': []})

    def test_attach_selects_readable_application_caller(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'app.cpp'
            path.write_text('int main() {}')
            def frame(source, function):
                value = MagicMock()
                file = value.GetLineEntry.return_value.GetFileSpec.return_value
                file.GetDirectory.return_value = str(Path(source).parent)
                file.GetFilename.return_value = Path(source).name
                value.GetLineEntry.return_value.GetLine.return_value = 1
                value.GetFunctionName.return_value = function
                return value
            runtime = frame('/missing/libc.c', '__nanosleep')
            sdk_path = Path(directory) / 'thread.hpp'
            sdk_path.write_text('// simulated SDK header')
            sdk = frame(sdk_path, 'std::this_thread::sleep_for')
            app = frame(path, 'main')
            engine = bridge.LLDB.__new__(bridge.LLDB)
            engine.lldb = SimpleNamespace(eStateStopped=1, eStateCrashed=2, eStateSuspended=3)
            engine.target = MagicMock()
            process = engine.target.GetProcess.return_value
            process.GetState.return_value = 1
            thread = process.GetSelectedThread.return_value
            thread.GetSelectedFrame.return_value = runtime
            thread.GetNumFrames.return_value = 3
            thread.GetFrameAtIndex.side_effect = [runtime, sdk, app]
            engine.select_source_frame(prefer_application=True)
            thread.SetSelectedFrame.assert_called_once_with(2)

    def test_gdb_selects_source_frame_before_reading_locals(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'app.cpp'
            path.write_text('int main() {}')
            gdb = bridge.GDB.__new__(bridge.GDB)
            gdb.lines = queue.Queue()
            gdb.master = -1
            gdb.pending = {'1': 'source-frames'}
            gdb.lines.put('1^done,stack=[frame={level="0",func="nanosleep"},frame={level="3",func="main",fullname=' + json.dumps(str(path)) + ',line="1"}]')
            with patch.object(gdb, 'send') as send, patch.object(bridge, 'emit'):
                gdb.poll()
                send.assert_called_once_with('-stack-select-frame 3', 'refresh')

    def test_gdb_commands_escape_paths(self):
        gdb = bridge.GDB.__new__(bridge.GDB)
        with patch.object(gdb, 'send') as send:
            gdb.command('b /tmp/source with spaces.c:12')
            send.assert_called_with('-break-insert -f "/tmp/source with spaces.c:12"')
            gdb.command('thread 7')
            send.assert_called_with('-thread-select 7', 'select')
            gdb.command('interrupt')
            send.assert_called_with('-exec-interrupt --all')


@unittest.skipUnless(os.environ.get('UVIM_DEBUGGER_LIVE_TESTS'), 'live debugger tests are opt-in')
class LiveTests(unittest.TestCase):
    def test_attach_quit_detaches_and_keeps_process_alive(self):
        with tempfile.TemporaryDirectory(prefix='uvim-attach-') as directory:
            source = Path(directory) / 'attach.c'
            source.write_text('#include <unistd.h>\n#include <stdio.h>\nint main(void) { int count = 42; puts("ready"); fflush(stdout); for (;;) { sleep(1); count = count % 100 + 1; } }\n')
            binary = Path(directory) / 'attach-target'
            subprocess.run(['clang', '-g', '-O0', str(source), '-o', str(binary)], check=True)
            backends = ['lldb'] + (['mladbg'] if os.environ.get('UVIM_MLADBG_MODULE') else [])
            for backend in backends:
                with self.subTest(backend=backend):
                    target = subprocess.Popen([str(binary)], stdout=subprocess.PIPE, text=True)
                    self.assertEqual(target.stdout.readline().strip(), 'ready')
                    proc = subprocess.Popen([sys.executable, '-u', str(SOURCE), backend, 'attach', str(target.pid)],
                                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    try:
                        pending = b''
                        deadline = time.monotonic() + 15
                        stopped = False
                        while not stopped and time.monotonic() < deadline:
                            if select.select([proc.stdout], [], [], 0.1)[0]:
                                data = os.read(proc.stdout.fileno(), 65536)
                                if not data:
                                    break
                                pending += data
                                while b'\n' in pending:
                                    line, pending = pending.split(b'\n', 1)
                                    message = json.loads(line)
                                    self.assertNotEqual(message.get('state'), 'error', message)
                                    if message.get('state') == 'stopped':
                                        self.assertEqual(message.get('path'), str(source), message)
                                        self.assertTrue(any('count' in v for v in message['variables']), message)
                                        stopped = True
                        self.assertTrue(stopped, 'Attach did not stop the target')
                        proc.stdin.close()
                        proc.wait(timeout=5)
                        self.assertIsNone(target.poll(), 'Quitting an attach session killed its target')
                    finally:
                        if not proc.stdin.closed:
                            proc.stdin.close()
                        if proc.poll() is None:
                            proc.kill()
                            proc.wait()
                        proc.stdout.close()
                        proc.stderr.close()
                        target.terminate()
                        target.wait()
                        target.stdout.close()

    def test_lldb_and_mladbg_breakpoint_variables_and_exit(self):
        with tempfile.TemporaryDirectory(prefix='uvim-debugger-') as directory:
            path = Path(directory)
            source = path / 'source with spaces.c'
            source.write_text('int main(void) {\n    int count = 41;\n    count += 1;\n    return count;\n}\n')
            binary = path / 'debug target'
            subprocess.run(['clang', '-g', '-O0', str(source), '-o', str(binary)], check=True)
            backends = ['lldb']
            if os.environ.get('UVIM_MLADBG_MODULE'):
                backends.append('mladbg')
            for backend in backends:
                with self.subTest(backend=backend):
                    proc = subprocess.Popen([sys.executable, '-u', str(SOURCE), backend, 'run', str(binary)],
                                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    pending = b''
                    messages = []
                    def until(predicate):
                        nonlocal pending
                        deadline = time.monotonic() + 15
                        while time.monotonic() < deadline:
                            if select.select([proc.stdout], [], [], 0.1)[0]:
                                data = os.read(proc.stdout.fileno(), 65536)
                                if not data:
                                    self.fail(f'Bridge disconnected: {messages}; {proc.stderr.read()}')
                                pending += data
                                while b'\n' in pending:
                                    line, pending = pending.split(b'\n', 1)
                                    value = json.loads(line)
                                    messages.append(value)
                                    if value.get('state') == 'error':
                                        self.fail(str(value))
                                    if predicate(value):
                                        return value
                        self.fail(f'Timed out: {messages}')
                    def command(value):
                        proc.stdin.write((json.dumps(value) + '\n').encode())
                        proc.stdin.flush()
                    try:
                        until(lambda m: m.get('state') == 'stopped')
                        command(f'b {source}:4')
                        command('continue')
                        stopped = until(lambda m: m.get('state') == 'stopped' and m.get('line') == 4)
                        self.assertEqual(Path(stopped['path']), source)
                        self.assertTrue(any('count' in v and '42' in v for v in stopped['variables']), stopped)
                        self.assertTrue(stopped['threads'])
                        command('continue')
                        until(lambda m: m.get('state') == 'exited')
                    finally:
                        proc.stdin.close()
                        try:
                            proc.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            proc.kill()
                            proc.wait()
                        proc.stdout.close()
                        proc.stderr.close()


if __name__ == '__main__':
    unittest.main()
