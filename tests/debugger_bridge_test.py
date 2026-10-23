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
    def test_type_layout_counts_holes_tail_padding_and_overlapping_union_fields(self):
        def type_(size, members, bases=0):
            value = MagicMock()
            value.GetName.return_value = 'Example'
            value.GetByteSize.return_value = size
            value.GetByteAlign.return_value = 4
            value.GetNumberOfDirectBaseClasses.return_value = bases
            value.GetNumberOfVirtualBaseClasses.return_value = 0
            value.GetNumberOfFields.return_value = len(members)
            value.IsArrayType.return_value = False
            fields = []
            for name, offset, length in members:
                field = MagicMock()
                field.GetName.return_value = name
                field.GetOffsetInBits.return_value = offset * 8
                field.IsBitfield.return_value = False
                field.GetType.return_value.GetName.return_value = name + '_type'
                if isinstance(length, int):
                    field.GetType.return_value.GetByteSize.return_value = length
                    field.GetType.return_value.GetNumberOfFields.return_value = 0
                    field.GetType.return_value.IsArrayType.return_value = False
                else:
                    field.GetType.return_value = length
                fields.append(field)
            value.GetFieldAtIndex.side_effect = fields
            return value
        rows = bridge.type_layout(type_(12, [('tag', 0, 1), ('value', 4, 4), ('tail', 8, 1)]), 'item')
        self.assertIn('12 bytes, alignment 4 bytes', rows[0])
        self.assertIn('  Padding at +1: 3 bytes', rows)
        self.assertIn('  Padding at +9: 3 bytes', rows)
        self.assertTrue(any('Top-level padding: 6 bytes' in row for row in rows))
        nested = bridge.type_layout(type_(16, [('tag', 0, 1), ('inner', 4, type_(8, [('tag', 0, 1), ('value', 4, 4)])), ('tail', 12, 1)]), 'item')
        self.assertTrue(any('Storage: item.inner (Example), 8 bytes' in row for row in nested))
        self.assertTrue(any('Top-level padding: 3 bytes' in row for row in nested))
        self.assertTrue(any('Top-level padding: 6 bytes' in row for row in nested))
        union = bridge.type_layout(type_(4, [('byte', 0, 1), ('word', 0, 4)]), 'item')
        self.assertTrue(any('Top-level padding: 0 bytes' in row for row in union))
        inherited = bridge.type_layout(type_(16, [('member', 8, 4)], bases=1), 'item')
        self.assertFalse(any('Top-level padding:' in row for row in inherited))
        hidden = bridge.type_layout(type_(16, [('member', 8, 4)]), 'item')
        self.assertTrue(any('hidden ABI data' in row for row in hidden))

    def test_inspection_commands_preserve_expressions_and_bound_counts(self):
        self.assertEqual(bridge.inspection_request('disasm callback'), ('disasm', 'callback', 32))
        self.assertEqual(bridge.inspection_request('disasm'), ('disasm', '$pc', 32))
        self.assertEqual(bridge.inspection_request('p --asm --count 4 callback'), ('disasm', 'callback', 4))
        self.assertEqual(bridge.inspection_request('print --memory &count'), ('mem', '&count', 64))
        self.assertEqual(bridge.inspection_request('mem --count 4 ptr + 8'), ('mem', 'ptr + 8', 4))
        self.assertIsNone(bridge.inspection_request('p count'))
        for command in ('mem', 'mem --count 0 ptr', 'mem --count 4097 ptr',
                        'disasm --count 257 callback', 'mem --count nope ptr'):
            with self.subTest(command=command), self.assertRaises(ValueError):
                bridge.inspection_request(command)

    def test_memory_rows_preserve_bytes_and_render_ascii(self):
        rows = bridge.memory_rows(0x1000, bytes([0, 65, 255, 10]) + b'abcdefghijklmnop')
        self.assertEqual(len(rows), 3)
        self.assertIn('0x0000000000001000  00 41 ff 0a', rows[0])
        self.assertIn('|.A..abcd|', rows[0])
        self.assertIn('0x0000000000001010', rows[2])

    def test_gdb_layout_requests_size_and_native_padding_and_falls_back_without_symbols(self):
        gdb = bridge.GDB.__new__(bridge.GDB)
        gdb.lines, gdb.pending, gdb.master = queue.Queue(), {}, -1
        with patch.object(gdb, 'send') as send:
            gdb.command('layout item')
            send.assert_called_with('-data-evaluate-expression "sizeof(item)"',
                                    ('inspection-size', 'item', True))
            gdb.pending['1'] = send.call_args.args[1]
            gdb.lines.put('1^done,value="12"')
            with patch.object(bridge, 'emit') as emit:
                gdb.poll()
                emit.assert_called_with(layoutTitle='Size and padding: item',
                                        layoutLines=['Storage: item, 12 bytes (includes padding)'], layoutAppend=False)
            send.assert_called_with('-interpreter-exec console "ptype /o item"',
                                    ('inspection-layout-type', 'item', True, []))
            gdb.pending['7'] = send.call_args.args[1]
            gdb.lines.put('~"/* 3-byte hole */\\n"')
            gdb.lines.put('7^done')
            with patch.object(bridge, 'emit') as emit:
                gdb.poll()
                emit.assert_called_with(layoutTitle='Size and padding: item',
                                        layoutLines=['/* 3-byte hole */'], layoutAppend=True)
            gdb.pending['2'] = ('inspection-function', 'callback', 4096, 2)
            gdb.lines.put('2^error,msg="No function contains specified address"')
            gdb.poll()
            send.assert_called_with('-data-disassemble -s 0x1000 -e 0x1020 -- 2',
                                    ('inspection-assembly', 'callback', 4096, 2))

    def test_gdb_inspection_resolves_addresses_and_formats_mi_results(self):
        gdb = bridge.GDB.__new__(bridge.GDB)
        gdb.lines, gdb.pending, gdb.master = queue.Queue(), {}, -1
        with patch.object(gdb, 'send') as send:
            gdb.command('disasm --count 2 callback')
            send.assert_called_once_with('-data-evaluate-expression "(unsigned long long)(callback)"',
                                         ('inspection-address', 'disasm', 'callback', 2))
            gdb.pending['1'] = send.call_args.args[1]
            send.reset_mock()
            gdb.lines.put('1^done,value="4096"')
            gdb.poll()
            send.assert_called_once_with('-data-disassemble -a 0x1000 -- 2',
                                         ('inspection-function', 'callback', 4096, 2))
        gdb.pending['2'] = ('inspection-function', 'callback', 4096, 2)
        gdb.pending['3'] = ('inspection-memory', '&count', 8192, 4)
        gdb.pending['4'] = ('inspection-value', 'count')
        gdb.pending['5'] = ('inspection-address', 'mem', '0', 4)
        gdb.pending['6'] = ('inspection-memory', 'bad', 1, 4)
        for line in ['2^done,asm_insns=[{address="0x1000",func-name="twice",offset="0",opcodes="90",inst="nop"},{address="0x1001",inst="ret"},{address="0x1002",inst="nop"}]',
                     '3^done,memory=[{begin="0x2000",contents="2a000000"}]',
                     '4^done,value="42"', '5^done,value="0"',
                     '6^error,msg="Cannot access memory"']:
            gdb.lines.put(line)
        with patch.object(bridge, 'emit') as emit, patch.object(gdb, 'refresh') as refresh, patch.object(gdb, 'send'):
            gdb.poll()
            messages = [call.kwargs for call in emit.call_args_list]
            refresh.assert_called_once()
        self.assertTrue(all(m['inspection'] for m in messages))
        self.assertEqual(sum(row.startswith('0x') for row in messages[0]['output']), 2)
        self.assertIn('nop', messages[0]['output'][3])
        self.assertIn('2a 00 00 00', messages[1]['output'][1])
        self.assertEqual(messages[2]['output'], ['count = 42'])
        self.assertIn('non-null', messages[3]['output'][0])
        self.assertEqual(messages[4]['output'], ['Cannot access memory'])

    def test_mi_nested_records_and_escapes(self):
        value = bridge.mi_fields('frame={level="0",fullname="/tmp/a b.c",line="12"},variables=[{name="count",value="42"},{name="text",value="\\\"hello\\\""}]')
        self.assertEqual(value['frame']['fullname'], '/tmp/a b.c')
        self.assertEqual(value['variables'][1]['value'], '"hello"')
        self.assertEqual(bridge.mi_value('"a\\012b"')[0], 'a\nb')
        self.assertEqual(bridge.mi_value('[frame={level="0"},frame={level="1"}]')[0], [{'level': '0'}, {'level': '1'}])

    def test_gdb_stop_refreshes_panes_without_clearing_them_on_continue(self):
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
            self.assertIn({'variables': ['count: int = 42'], 'variableExpressions': ['count']}, messages)
            self.assertIn({'path': '/tmp/test.c', 'line': 5, 'frame': 0, 'function': ''}, messages)
            self.assertEqual(messages[-1], {'state': 'running'})

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

    def test_caller_next_and_step_use_selected_frame_but_frame_zero_uses_native_step(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'app.cpp'
            source.write_text('int main() {}')
            engine = bridge.LLDB.__new__(bridge.LLDB)
            engine.lldb = SimpleNamespace(eStateStopped=1, eStateCrashed=2, eStateSuspended=3)
            engine.target = MagicMock()
            process = engine.target.GetProcess.return_value
            process.GetState.return_value = 1
            frame = process.GetSelectedThread.return_value.GetSelectedFrame.return_value
            frame.IsValid.return_value = True
            frame.GetFrameID.return_value = 4
            with patch.object(engine, 'frame_source', return_value=(str(source), 51)):
                for command in ('next', 'n', 'step', 's'):
                    self.assertEqual(engine.caller_step_command(command),
                                     'thread until --frame 4 --run-mode all-threads 52')
                self.assertIsNone(engine.caller_step_command('continue'))
                self.assertIsNone(engine.caller_step_command('next --count 2'))
                frame.GetFrameID.return_value = 0
                self.assertIsNone(engine.caller_step_command('next'))
                frame.GetFrameID.return_value = 4
                process.GetState.return_value = 9
                self.assertIsNone(engine.caller_step_command('next'))

    def test_running_snapshot_does_not_clear_the_last_stop_or_read_frames(self):
        engine = bridge.LLDB.__new__(bridge.LLDB)
        engine.lldb = SimpleNamespace(eStateStopped=1, eStateCrashed=2, eStateSuspended=3,
                                     eStateRunning=4, eStateStepping=5, eStateExited=6)
        engine.target = MagicMock()
        process = engine.target.GetProcess.return_value
        process.GetState.return_value = 4
        with patch.object(bridge, 'emit') as emit:
            engine.snapshot()
            emit.assert_called_once_with(state='running')
            process.GetSelectedThread.assert_not_called()

    def test_gdb_breakpoints_normalize_locations_disabled_and_pending(self):
        data = {'BreakpointTable': {'body': [
            {'number': '1', 'type': 'breakpoint', 'enabled': 'y', 'addr': '0x10',
             'fullname': '/tmp/demo.cpp', 'line': '12'},
            {'number': '2', 'type': 'breakpoint', 'enabled': 'n', 'addr': '<MULTIPLE>',
             'locations': [{'fullname': '/tmp/demo.cpp', 'line': '15', 'enabled': 'y'},
                           {'fullname': '/tmp/demo.cpp', 'line': '18', 'enabled': 'n'}]},
            {'number': '3', 'type': 'breakpoint', 'enabled': 'y', 'addr': '<PENDING>',
             'original-location': '/tmp/future.cpp:42'},
            {'number': '4', 'type': 'hw watchpoint', 'enabled': 'y'}]}}
        result = bridge.gdb_breakpoints(data)
        self.assertEqual([bp['id'] for bp in result], ['1', '2', '3'])
        self.assertEqual(result[0]['locations'][0]['line'], 12)
        self.assertFalse(result[1]['enabled'])
        self.assertFalse(result[1]['locations'][1]['enabled'])
        self.assertTrue(result[2]['pending'])
        self.assertEqual(result[2]['locations'][0]['path'], '/tmp/future.cpp')
        separate = bridge.gdb_breakpoints({'BreakpointTable': {'body': [
            {'number': '9', 'type': 'breakpoint', 'enabled': 'y', 'addr': '<MULTIPLE>'},
            {'number': '9.1', 'enabled': 'y', 'fullname': '/tmp/template.hpp', 'line': '42'}]}})
        self.assertEqual(len(separate), 1)
        self.assertEqual(separate[0]['locations'][0]['line'], 42)
        gdb = bridge.GDB.__new__(bridge.GDB)
        with patch.object(gdb, 'send') as send:
            gdb.command('delete 2')
            send.assert_called_with('-break-delete 2', 'breakpoint-change')
        with self.assertRaises(ValueError):
            gdb.command('delete')

    @unittest.skipIf(os.name == 'nt', 'fixture uses POSIX symlinks')
    def test_breakpoint_file_picker_path_resolves_to_dwarf_spelling(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            real = root / 'real'
            real.mkdir()
            (real / 'helper.hpp').write_text('inline int helper() { return 42; }')
            alias = root / 'alias'
            alias.symlink_to(real, target_is_directory=True)
            def file(name):
                value = MagicMock()
                value.GetDirectory.return_value = str(alias)
                value.GetFilename.return_value = name
                return value
            engine = bridge.LLDB.__new__(bridge.LLDB)
            engine.target = MagicMock()
            engine.target.GetNumModules.return_value = 1
            module = engine.target.GetModuleAtIndex.return_value
            module.GetUUIDString.return_value = 'fixture-module'
            module.GetNumCompileUnits.return_value = 1
            unit = module.GetCompileUnitAtIndex.return_value
            unit.GetFileSpec.return_value = file('main.cpp')
            unit.GetNumSupportFiles.return_value = 1
            unit.GetSupportFileAtIndex.return_value = file('helper.hpp')
            self.assertEqual(engine.breakpoint_source_path(str(real / 'helper.hpp')),
                             str(alias / 'helper.hpp'))
            self.assertEqual(engine.breakpoint_source_path(str(real / 'helper.hpp')),
                             str(alias / 'helper.hpp'))
            module.GetCompileUnitAtIndex.assert_called_once_with(0)

    def test_gdb_commands_escape_paths(self):
        gdb = bridge.GDB.__new__(bridge.GDB)
        with patch.object(gdb, 'send') as send:
            gdb.command('b /tmp/source with spaces.c:12')
            send.assert_called_with('-break-insert -f "/tmp/source with spaces.c:12"', 'breakpoint-change')
            gdb.command('thread 7')
            send.assert_called_with('-thread-select 7', 'select')
            gdb.command('interrupt')
            send.assert_called_with('-exec-interrupt --all')


@unittest.skipUnless(os.environ.get('UVIM_DEBUGGER_LIVE_TESTS'), 'live debugger tests are opt-in')
class LiveTests(unittest.TestCase):
    def test_function_pointer_disassembly_and_variable_storage(self):
        with tempfile.TemporaryDirectory(prefix='uvim-inspection-') as directory:
            source, binary = Path(directory) / 'inspect.c', Path(directory) / 'inspect'
            source.write_text('struct Padded { char tag; int value; char tail; }; struct Nested { char tag; struct Padded child; };\nint twice(int x) { return x * 2; }\nint main(void) {\n'
                              '  int count = 42;\n  int (*callback)(int) = twice;\n'
                              '  struct Padded item = {1, 42, 2}; struct Nested nested = {0, {1, 42, 2}};\n  count = callback(count);\n  return count;\n}\n')
            subprocess.run(['clang', '-g', '-O0', str(source), '-o', str(binary)], check=True)
            for backend in ['lldb'] + (['mladbg'] if os.environ.get('UVIM_MLADBG_MODULE') else []):
                with self.subTest(backend=backend):
                    engine, messages = None, []
                    try:
                        with patch.object(bridge, 'emit', side_effect=lambda **value: messages.append(value)):
                            engine = bridge.LLDB(backend, False, str(binary), [])
                            engine.command(f'b {source}:7')
                            engine.command('continue')
                            deadline = time.monotonic() + 15
                            while time.monotonic() < deadline:
                                engine.poll()
                                if any(m.get('state') == 'stopped' and m.get('line') == 7 for m in messages):
                                    break
                                time.sleep(.02)
                            else:
                                self.fail(f'Breakpoint timed out: {messages}')
                            self.assertTrue(any('callback' in m.get('variableExpressions', []) for m in messages))
                            engine.command('p count')
                            self.assertTrue(any(m.get('inspection') and any('42' in row for row in m['output']) for m in messages))
                            engine.command('mem --count 4 &count')
                            memory = messages[-1]['output']
                            self.assertIn((42).to_bytes(4, sys.byteorder).hex(' '), memory[1])
                            engine.command('disasm --count 4 callback')
                            assembly = messages[-1]['output']
                            self.assertEqual(sum(row.startswith('0x') for row in assembly), 4)
                            self.assertTrue(any('code bytes' in row for row in assembly), assembly)
                            self.assertIn('Disassembly: callback', assembly[0])
                            self.assertTrue(any('Displayed: 4 instructions' in row for row in assembly))
                            frame = engine.target.GetProcess().GetSelectedThread().GetSelectedFrame()
                            address = frame.FindVariable('callback').GetValueAsAddress()
                            engine.command(f'p --asm --count 1 0x{address:x}')
                            self.assertEqual(sum(row.startswith('0x') for row in messages[-1]['output']), 1)
                            engine.command('p --memory --count 4 &count')
                            self.assertIn('Memory: &count', messages[-1]['output'][0])
                            engine.command('p item')
                            printed = next(m['output'] for m in reversed(messages) if m.get('inspection'))
                            self.assertTrue(any('Top-level padding: 6 bytes' in row for row in printed), printed)
                            engine.command('layout item')
                            self.assertIn('12 bytes', messages[-1]['layoutLines'][0])
                            self.assertTrue(any('alignment 4 bytes' in row for row in messages[-1]['layoutLines']))
                            engine.command('layout nested')
                            nested = messages[-1]['layoutLines']
                            self.assertIn('16 bytes', nested[0])
                            self.assertTrue(any('nested.child' in row and '12 bytes' in row for row in nested), nested)
                            self.assertTrue(any('Top-level padding: 6 bytes' in row for row in nested), nested)
                            for command in ('mem 0', 'disasm missing_symbol'):
                                with self.assertRaises(ValueError):
                                    engine.command(command)
                    finally:
                        if engine:
                            engine.close()

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

    def test_cpp_attach_next_advances_application_source_and_selected_worker(self):
        with tempfile.TemporaryDirectory(prefix='uvim-cpp-next-') as directory:
            generator = SOURCE.parents[1] / 'scripts/create_debugger_examples.py'
            subprocess.run([sys.executable, '-B', str(generator), '--language', 'cpp',
                            '--interval-ms', '500', '--output-dir', directory],
                           check=True, stdout=subprocess.DEVNULL)
            item, = json.loads(next(Path(directory).glob('uvim-debugger-*/manifest.json')).read_text())
            backends = ['lldb'] + (['mladbg'] if os.environ.get('UVIM_MLADBG_MODULE') else [])
            for backend in backends:
                with self.subTest(backend=backend):
                    target = subprocess.Popen([item['binary']], stdout=subprocess.PIPE, text=True)
                    engine = None
                    try:
                        self.assertIn('C++ main:', target.stdout.readline())
                        messages = []
                        with patch.object(bridge, 'emit', side_effect=lambda **value: messages.append(value)):
                            engine = bridge.LLDB(backend, True, str(target.pid), [])
                            engine.poll()
                            process = engine.target.GetProcess()
                            def snapshot():
                                return next(m for m in reversed(messages) if m.get('state') == 'stopped')
                            def step(command):
                                before = snapshot()
                                stop_id = process.GetStopID()
                                engine.command(command)
                                deadline = time.monotonic() + 10
                                while time.monotonic() < deadline:
                                    engine.poll()
                                    if process.GetStopID() > stop_id and process.GetState() == engine.lldb.eStateStopped:
                                        after = snapshot()
                                        self.assertEqual(after['path'], item['source'], after)
                                        self.assertNotEqual(after['line'], before['line'], after)
                                        self.assertEqual(after['frame'], 0, after)
                                        return after
                                    time.sleep(.02)
                                self.fail(f'Step timed out: {messages}')
                            self.assertGreater(snapshot()['frame'], 0, snapshot())
                            step('n')
                            step('next')
                            engine.command('thread 2')
                            thread = process.GetSelectedThread()
                            self.assertEqual(thread.GetIndexID(), 2)
                            # The worker can be in checkpoint/destructor code as
                            # well as sleep when the main-thread step completes.
                            caller = next(frame for frame in thread
                                          if (frame.GetFunctionName() or '').startswith('worker('))
                            engine.command('frame ' + str(caller.GetFrameID()))
                            self.assertIn('worker', snapshot()['function'])
                            worker = step('next')
                            self.assertIn('worker', worker['function'])
                            self.assertTrue(any('worker_id' in v for v in worker['variables']))
                    finally:
                        if engine:
                            engine.close()
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
                        command(f'b {source.resolve()}:4')
                        metadata = until(lambda m: bool(m.get('breakpoints')))
                        breakpoint_id = metadata['breakpoints'][0]['id']
                        self.assertTrue(any(loc['path'] == str(source) and loc['line'] == 4 for loc in metadata['breakpoints'][0]['locations']))
                        command('continue')
                        stopped = until(lambda m: m.get('state') == 'stopped' and m.get('line') == 4)
                        self.assertEqual(Path(stopped['path']), source)
                        self.assertTrue(any('count' in v and '42' in v for v in stopped['variables']), stopped)
                        self.assertTrue(stopped['threads'])
                        command('delete ' + breakpoint_id)
                        until(lambda m: m.get('breakpoints') == [])
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
