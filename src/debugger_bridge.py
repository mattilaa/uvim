"""Private JSON-lines transport for uvim debugger backends (embedded at build)."""
import importlib
import importlib.util
import json
import os
from pathlib import Path
import queue
import re
import select
import shutil
import subprocess
import sys
import threading


def emit(**value):
    print(json.dumps(value, ensure_ascii=True), flush=True)


def lldb_module():
    try:
        return importlib.import_module('lldb')
    except ImportError:
        result = subprocess.run([os.environ.get('MLADBG_LLDB', 'lldb'), '-P'],
                                capture_output=True, text=True, timeout=10, check=True)
        sys.path.insert(0, result.stdout.strip())
        return importlib.import_module('lldb')


def readable_source(path, line):
    return bool(path and int(line or 0) > 0 and os.path.isfile(path))


def application_source(path, function):
    return not (function.startswith(('std::', '__')) or '/include/c++/' in path or
                '/stdlib/std/' in path or '/include/mlang/' in path or
                path.startswith(('/System/', '/usr/include/', '/usr/lib/', '/Applications/Xcode.app/')))


def inspection_request(text):
    verb, _, rest = text.strip().partition(' ')
    if verb in ('p', 'print'):
        flag, _, expression = rest.strip().partition(' ')
        if flag not in ('--asm', '--memory', '--layout'):
            return None
        verb, rest = {'--asm': 'disasm', '--memory': 'mem', '--layout': 'layout'}[flag], expression
    if verb not in ('disasm', 'mem', 'layout'):
        return None
    expression = rest.strip()
    if verb == 'layout':
        if not expression:
            raise ValueError('Usage: layout EXPRESSION')
        return verb, expression, 0
    count = 32 if verb == 'disasm' else 64
    if expression.startswith('--count'):
        match = re.fullmatch(r'--count\s+(\d+)\s+(.+)', expression)
        if not match:
            raise ValueError(f'Usage: {verb} [--count N] EXPRESSION')
        count, expression = int(match[1]), match[2].strip()
    maximum = 256 if verb == 'disasm' else 4096
    if not 1 <= count <= maximum:
        raise ValueError(f'{verb}: count must be between 1 and {maximum}')
    if not expression and verb == 'mem':
        raise ValueError('Usage: mem [--count N] ADDRESS (use &variable for its storage)')
    return verb, expression or '$pc', count


def memory_rows(address, data):
    rows = []
    for offset in range(0, len(data), 8):
        chunk = data[offset:offset + 8]
        ascii_text = ''.join(chr(byte) if 32 <= byte < 127 else '.' for byte in chunk)
        rows.append(f'0x{address + offset:016x}  {chunk.hex(" "):<23}  |{ascii_text}|')
    return rows


def type_layout(type_, label, depth=0, budget=None):
    if budget is None:
        budget = [128]
    size = type_.GetByteSize()
    align = type_.GetByteAlign() if hasattr(type_, 'GetByteAlign') else 0
    rows = [f'Storage: {label} ({type_.GetName()}), {size} bytes' +
            (f', alignment {align} bytes' if align else '') + ' (includes padding)']
    total_fields = type_.GetNumberOfFields()
    visible_fields = min(total_fields, budget[0])
    fields = [type_.GetFieldAtIndex(i) for i in range(visible_fields)]
    budget[0] -= visible_fields
    base_count = type_.GetNumberOfDirectBaseClasses() + type_.GetNumberOfVirtualBaseClasses()
    if base_count:
        rows.append('Padding: not calculated for inherited classes (base/ABI storage may be hidden)')
    if not fields:
        if total_fields:
            rows.append('Nested layout truncated at 128 members')
        elif hasattr(type_, 'IsArrayType') and type_.IsArrayType():
            element = type_.GetArrayElementType()
            element_size = element.GetByteSize()
            if element.IsValid() and element_size > 0:
                rows.append(f'  Array: {size // element_size} elements, {element_size} bytes per element (stride)')
                if element.GetNumberOfFields() and depth < 4 and budget[0]:
                    rows += ['  ' + row for row in type_layout(element, label + '[0]', depth + 1, budget)]
        return rows
    covered = []
    for field in fields:
        start = field.GetOffsetInBits()
        bits = field.GetBitfieldSizeInBits() if field.IsBitfield() else field.GetType().GetByteSize() * 8
        if start < 0 or start > size * 8 or bits < 0 or start + bits > size * 8:
            rows.append('Padding: field offsets unavailable in debug information')
            return rows
        rows.append(f'  +{start // 8}' + (f'.{start % 8} bits' if start % 8 else '') +
                    f': {field.GetName() or "<anonymous>"} ({field.GetType().GetName()}), ' +
                    (f'{bits} bits' if field.IsBitfield() else f'{bits // 8} bytes'))
        covered.append((start, start + bits))
        member_type = field.GetType()
        if member_type.GetNumberOfFields() or member_type.IsArrayType():
            if depth < 4 and budget[0]:
                rows += ['    ' + row for row in type_layout(
                    member_type, label + '.' + (field.GetName() or '<anonymous>'), depth + 1, budget)]
            else:
                rows.append('    Nested layout truncated (maximum depth 4 / 128 members)')
    if base_count or visible_fields < total_fields:
        if visible_fields < total_fields:
            rows.append('Padding: not calculated; field layout truncated at 128 members')
        return rows
    # A leading gap can contain an implicit vptr. Never label hidden ABI data
    # as padding when the debug information does not identify it.
    if covered and min(start for start, _ in covered) > 0:
        rows.append('Padding: not calculated; leading storage may contain hidden ABI data')
        return rows
    cursor, padding = 0, 0
    for start, end in sorted(covered) + [(size * 8, size * 8)]:
        if start > cursor:
            bits = start - cursor
            amount = f'{bits // 8} bytes' if bits % 8 == 0 else f'{bits} bits'
            rows.append(f'  Padding at +{cursor // 8}' +
                        (f'.{cursor % 8} bits' if cursor % 8 else '') + f': {amount}')
            padding += bits
        cursor = max(cursor, end)
    rows.append('Top-level padding: ' +
                (f'{padding // 8} bytes' if padding % 8 == 0 else f'{padding} bits') +
                ' (nested member padding is included in member storage)')
    return rows


class LLDB:
    def __init__(self, backend, attach, target, args):
        self.lldb = lldb_module()
        self.session = None
        self.attached = attach
        self.source_selection_pending = attach
        self.requested_breakpoints = {}
        self.breakpoint_cache = None
        if backend == 'mladbg':
            paths = [os.environ.get('UVIM_MLADBG_MODULE', '')]
            launcher = shutil.which('mladbg')
            if launcher:
                root = Path(launcher).resolve().parent
                paths += [str(root / '../share/mlang/debugger/mladbg.py'),
                          str(root / '../share/mlang/mladbg/mladbg.py'),
                          str(root / '../share/mlang/mladbg.py'),
                          str(root / '../tools/mladbg/mladbg.py')]
            path = next((Path(p).resolve() for p in paths if p and Path(p).is_file()), None)
            if path is None:
                raise RuntimeError('Set UVIM_MLADBG_MODULE to tools/mladbg/mladbg.py from mlang')
            sys.path.insert(0, str(path.parent))
            spec = importlib.util.spec_from_file_location('uvim_mladbg', path)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            self.session = module.Session(self.lldb, '' if attach else target, args)
            self.debugger = self.session.debugger
            self.target = self.session.target
        else:
            self.debugger = self.lldb.SBDebugger.Create(False)
            self.debugger.SetAsync(True)
            self.debugger.SetUseColor(False)
            self.target = self.debugger.CreateTarget('' if attach else target)
            if not attach and not self.target.IsValid():
                raise RuntimeError('Cannot load executable: ' + target)
            info = self.target.GetLaunchInfo()
            info.SetArguments(args, False)
            info.AddOpenFileAction(0, os.devnull, True, False)
            info.SetLaunchFlags(info.GetLaunchFlags() & ~self.lldb.eLaunchFlagDisableASLR)
            self.target.SetLaunchInfo(info)
        if attach:
            error = self.lldb.SBError()
            self.target.AttachToProcessWithID(self.debugger.GetListener(), int(target), error)
            if error.Fail():
                raise RuntimeError(str(error))
        else:
            # Stop at entry before user code executes so breakpoints can be added.
            info = self.target.GetLaunchInfo()
            info.SetLaunchFlags(info.GetLaunchFlags() | self.lldb.eLaunchFlagStopAtEntry | self.lldb.eLaunchFlagDebug)
            error = self.lldb.SBError()
            self.target.Launch(info, error)
            if error.Fail():
                raise RuntimeError(str(error))
        self.marker = None
        self.snapshot()

    def command(self, text):
        text = text.strip()
        request = inspection_request(text)
        if request:
            self.inspect(*request)
            return
        select_thread = text.startswith('thread ') and text[7:].strip().isdigit()
        caller_step = self.caller_step_command(text)
        verb, _, rest = text.partition(' ')
        location = rest.strip().strip('"\'')
        path, colon, row = location.rpartition(':')
        if text == 'bps':
            self.publish_breakpoints()
        elif verb in ('b', 'break') and colon and row.isdigit():
            if int(row) <= 0:
                raise ValueError('Breakpoint line must be positive')
            debug_path = self.breakpoint_source_path(path.strip('"\''))
            bp = self.target.BreakpointCreateByLocation(debug_path, int(row))
            if not bp.IsValid():
                raise RuntimeError(f'Cannot create breakpoint at {path}:{row}')
            self.requested_breakpoints[bp.GetID()] = (path.strip('"\''), int(row))
            emit(output=[f'Breakpoint {bp.GetID()}: {path}:{row}'])
        elif verb in ('delete', 'enable', 'disable'):
            if not re.fullmatch(r'[1-9]\d*', rest.strip()):
                raise ValueError(f'Usage: {verb} BREAKPOINT_ID')
            identity = int(rest.strip())
            bp = self.target.FindBreakpointByID(identity)
            if not bp.IsValid():
                raise ValueError(f'Breakpoint {identity} does not exist')
            if verb == 'delete':
                if not self.target.BreakpointDelete(identity):
                    raise RuntimeError(f'Cannot delete breakpoint {identity}')
            else:
                bp.SetEnabled(verb == 'enable')
            emit(output=[f'Breakpoint {identity}: {verb}'])
        elif caller_step:
            result = self.lldb.SBCommandReturnObject()
            self.debugger.GetCommandInterpreter().HandleCommand(caller_step, result)
            emit(output=(result.GetOutput() + result.GetError()).splitlines())
        elif text == 'interrupt':
            error = self.target.GetProcess().Stop()
            if error.Fail():
                raise RuntimeError(str(error))
        elif self.session:
            # The bridge already consumed this stop. Do not let mladbg's own
            # polling reselect the previous stop thread after a thread/frame command.
            process = self.target.GetProcess()
            self.session.last_stop = (process.GetProcessID(), process.GetState(), process.GetStopID())
            self.session.command(text)
            rows = list(self.session.log)[-100:]
            if verb in ('p', 'print'):
                rows += self.variable_layout(rest)
            emit(output=rows, inspection=verb in ('p', 'print'))
            self.session.log.clear()
        else:
            verb, _, rest = text.partition(' ')
            aliases = {'c': 'continue', 'n': 'next', 's': 'step', 'f': 'finish',
                       'r': 'run', 'locals': 'frame variable', 'bt': 'thread backtrace',
                       'threads': 'thread list', 'thread': 'thread select'}
            if verb in ('b', 'break'):
                path, colon, row = rest.rpartition(':')
                if colon and row.isdigit():
                    text = 'breakpoint set --file ' + json.dumps(path.strip('\"')) + ' --line ' + row
                else:
                    text = 'breakpoint set --name ' + json.dumps(rest)
            elif verb == 'frame' and rest.strip().isdigit():
                text = 'frame select ' + rest.strip()
            else:
                text = aliases.get(verb, verb) + (' ' + rest if rest else '')
            result = self.lldb.SBCommandReturnObject()
            self.debugger.GetCommandInterpreter().HandleCommand(text, result)
            rows = (result.GetOutput() + result.GetError()).splitlines()
            if verb in ('p', 'print'):
                rows += self.variable_layout(rest)
            emit(output=rows,
                 inspection=verb in ('p', 'print'))
        self.target = self.debugger.GetSelectedTarget()
        if select_thread:
            self.select_source_frame(prefer_application=True)
        self.publish_breakpoints()
        self.snapshot()

    def variable_layout(self, expression):
        frame = self.target.GetProcess().GetSelectedThread().GetSelectedFrame()
        value = frame.FindVariable(expression.strip())
        if not value.IsValid():
            return []
        return type_layout(value.GetType(), expression)

    def inspect(self, kind, expression, count):
        process = self.target.GetProcess()
        if process.GetState() not in (self.lldb.eStateStopped, self.lldb.eStateCrashed,
                                     self.lldb.eStateSuspended):
            raise ValueError('Pause execution before inspecting memory or assembly')
        frame = process.GetSelectedThread().GetSelectedFrame()
        value = frame.FindVariable(expression)
        if not value.IsValid():
            value = frame.EvaluateExpression(expression)
        if kind == 'layout' and value.IsValid() and value.GetType().IsValid():
            emit(output=type_layout(value.GetType(), expression), inspection=True)
            return
        if not value.IsValid() or value.GetError().Fail():
            raise ValueError(f'Cannot evaluate {expression}: {value.GetError()}')
        # GetValueAsAddress strips pointer authentication/tag bits when needed.
        address = (value.GetValueAsAddress() if value.GetType().IsPointerType()
                   else value.GetValueAsUnsigned(self.lldb.LLDB_INVALID_ADDRESS))
        if address in (0, self.lldb.LLDB_INVALID_ADDRESS):
            raise ValueError(f'{expression} is not a readable, non-null address')
        rows = [f'{"Disassembly" if kind == "disasm" else "Memory"}: {expression} @ 0x{address:x}']
        if kind == 'mem':
            error = self.lldb.SBError()
            data = process.ReadMemory(address, count, error)
            if not data:
                raise ValueError(f'Cannot read memory at 0x{address:x}: {error}')
            rows += memory_rows(address, data)
            if len(data) < count:
                rows.append(f'Only {len(data)} of {count} bytes are readable')
            if value.GetType().IsPointerType():
                pointee = value.GetType().GetPointeeType()
                if pointee.IsValid() and pointee.GetByteSize() > 0:
                    rows += type_layout(pointee, '*(' + expression + ')')
        else:
            start = self.target.ResolveLoadAddress(address)
            instructions = self.target.ReadInstructions(start, count)
            if not instructions.GetSize():
                raise ValueError(f'No instructions at 0x{address:x}')
            function = start.GetFunction()
            symbol = function if function.IsValid() else start.GetSymbol()
            begin, end = symbol.GetStartAddress(), symbol.GetEndAddress()
            function_end = None
            if begin.IsValid() and end.IsValid():
                low, high = begin.GetLoadAddress(self.target), end.GetLoadAddress(self.target)
                if 0 <= low < high < self.lldb.LLDB_INVALID_ADDRESS:
                    rows.append(f'Function: {symbol.GetName()}, {high - low} code bytes')
                    if low <= address < high:
                        function_end = high
            if function_end is None:
                rows.append('Function size: unavailable; displaying bytes at the requested address')
            rows.append('Function padding: not identified separately by debug symbols')
            displayed_bytes, displayed_count = 0, 0
            for instruction in instructions:
                location = instruction.GetAddress().GetLoadAddress(self.target)
                if function_end is not None and location >= function_end:
                    break
                data = instruction.GetData(self.target)
                error = self.lldb.SBError()
                encoded = ' '.join(f'{data.GetUnsignedInt8(error, i):02x}'
                                   for i in range(min(instruction.GetByteSize(), 16)))
                if error.Fail():
                    encoded = '??'
                displayed_bytes += instruction.GetByteSize()
                displayed_count += 1
                mnemonic = instruction.GetMnemonic(self.target) or '?'
                operands = instruction.GetOperands(self.target) or ''
                comment = instruction.GetComment(self.target) or ''
                rows.append(f'0x{location:016x}  {mnemonic} {operands}  [{encoded}]' +
                            (f'  ; {comment}' if comment else ''))
            rows.append(f'Displayed: {displayed_count} instructions, {displayed_bytes} bytes')
        emit(output=rows, inspection=True)

    def caller_step_command(self, text):
        # Source shown after attach may be several frames above the actual PC.
        # Native next/step only advances frame zero, potentially one instruction
        # at a time in libc. Finish the pending call in the inspected frame first.
        if text not in ('next', 'n', 'step', 's'):
            return None
        process = self.target.GetProcess()
        if process.GetState() not in (self.lldb.eStateStopped, self.lldb.eStateCrashed,
                                     self.lldb.eStateSuspended):
            return None
        frame = process.GetSelectedThread().GetSelectedFrame()
        if not frame.IsValid() or frame.GetFrameID() == 0:
            return None
        path, line = self.frame_source(frame)
        if not readable_source(path, line):
            return None
        return f'thread until --frame {frame.GetFrameID()} --run-mode all-threads {line + 1}'

    def poll(self):
        event = self.lldb.SBEvent()
        while self.debugger.GetListener().GetNextEvent(event):
            pass
        process = self.target.GetProcess()
        for getter in (process.GetSTDOUT, process.GetSTDERR):
            data = getter(8192)
            if data:
                emit(output=data.splitlines())
        marker = (process.GetState(), process.GetStopID())
        if marker != self.marker:
            self.marker = marker
            if process.GetState() == self.lldb.eStateStopped:
                for thread in process:
                    if thread.GetStopReason() not in (self.lldb.eStopReasonInvalid, self.lldb.eStopReasonNone):
                        process.SetSelectedThread(thread)
                        break
            if process.GetState() == self.lldb.eStateStopped:
                self.select_source_frame(prefer_application=self.source_selection_pending)
            if process.GetState() == self.lldb.eStateExited:
                emit(output=['Process exited: ' + str(process.GetExitStatus()) + ' ' + (process.GetExitDescription() or '')])
            elif process.GetState() == self.lldb.eStateStopped:
                emit(output=['Stopped: ' + (process.GetSelectedThread().GetStopDescription(512) or '')])
            self.snapshot()

    @staticmethod
    def frame_source(frame):
        entry = frame.GetLineEntry()
        if not entry.IsValid():
            return '', 0
        file = entry.GetFileSpec()
        return os.path.join(file.GetDirectory() or '', file.GetFilename() or ''), entry.GetLine()

    def select_source_frame(self, prefer_application=False):
        process = self.target.GetProcess()
        if process.GetState() not in (self.lldb.eStateStopped, self.lldb.eStateCrashed,
                                     self.lldb.eStateSuspended):
            return
        thread = process.GetSelectedThread()
        path, line = self.frame_source(thread.GetSelectedFrame())
        if readable_source(path, line) and not prefer_application:
            return
        fallback = None
        for index in range(min(thread.GetNumFrames(), 64)):
            frame = thread.GetFrameAtIndex(index)
            path, line = self.frame_source(frame)
            if not readable_source(path, line):
                continue
            if fallback is None:
                fallback = index
            if application_source(path, frame.GetFunctionName() or ''):
                fallback = index
                break
        if fallback is not None:
            thread.SetSelectedFrame(fallback)
        self.source_selection_pending = False

    def breakpoint_source_path(self, path):
        # The file picker canonicalizes paths, while DWARF can retain a symlink
        # spelling (/var versus /private/var on macOS). LLDB's resolver matches
        # debug-info paths; retain that spelling for the requested physical file.
        wanted = os.path.realpath(os.path.abspath(path))
        cache = getattr(self, 'source_aliases', {})
        modules = [self.target.GetModuleAtIndex(i) for i in range(self.target.GetNumModules())]
        signature = tuple(module.GetUUIDString() or str(module.GetFileSpec()) for module in modules)
        key = (signature, wanted)
        if key in cache:
            return cache[key]
        for module in modules:
            for unit_index in range(module.GetNumCompileUnits()):
                unit = module.GetCompileUnitAtIndex(unit_index)
                primary = unit.GetFileSpec()
                directory = primary.GetDirectory() or ''
                files = [primary]
                files.extend(unit.GetSupportFileAtIndex(i) for i in range(unit.GetNumSupportFiles()))
                for file in files:
                    name = file.GetFilename()
                    if not name:
                        continue
                    candidate = os.path.join(file.GetDirectory() or '', name)
                    if not os.path.isabs(candidate) and directory:
                        candidate = os.path.join(directory, candidate)
                    if os.path.realpath(os.path.abspath(candidate)) == wanted:
                        cache[key] = candidate
                        self.source_aliases = cache
                        return candidate
        return path

    def publish_breakpoints(self):
        result = []
        live_ids = set()
        for index in range(self.target.GetNumBreakpoints()):
            bp = self.target.GetBreakpointAtIndex(index)
            if bp.IsInternal():
                continue
            identity = bp.GetID()
            live_ids.add(identity)
            stream = self.lldb.SBStream()
            bp.GetDescription(stream)
            locations = []
            for number in range(bp.GetNumLocations()):
                location = bp.GetLocationAtIndex(number)
                entry = location.GetAddress().GetLineEntry()
                file = entry.GetFileSpec()
                path = os.path.join(file.GetDirectory() or '', file.GetFilename() or '')
                if entry.IsValid() and path and entry.GetLine() > 0:
                    locations.append(dict(path=path, line=entry.GetLine(), enabled=location.IsEnabled()))
            pending = bp.GetNumResolvedLocations() == 0
            if not locations and identity in self.requested_breakpoints:
                path, line = self.requested_breakpoints[identity]
                locations.append(dict(path=os.path.abspath(path), line=line, enabled=True))
            result.append(dict(id=str(identity), enabled=bp.IsEnabled(), pending=pending,
                               description=stream.GetData() or '', locations=locations))
        self.requested_breakpoints = {k: v for k, v in self.requested_breakpoints.items() if k in live_ids}
        if result != self.breakpoint_cache:
            self.breakpoint_cache = result
            emit(breakpoints=result)

    def snapshot(self):
        process = self.target.GetProcess()
        stopped = process.GetState() in (self.lldb.eStateStopped, self.lldb.eStateCrashed,
                                         self.lldb.eStateSuspended)
        state = 'stopped' if stopped else 'running' if process.GetState() in (
            self.lldb.eStateRunning, self.lldb.eStateStepping) else 'exited' if process.GetState() == self.lldb.eStateExited else 'ready'
        if state == 'running':
            emit(state=state)
            return
        self.publish_breakpoints()
        value = dict(state=state, variables=[], variableExpressions=[], threads=[], path='', line=0, frame=0, function='', stopFunction='')
        if stopped:
            if self.source_selection_pending:
                self.select_source_frame(prefer_application=True)
            thread = process.GetSelectedThread()
            frame = thread.GetSelectedFrame()
            # LLDB may still publish stopped while an asynchronous resume has
            # already invalidated its frames. Wait for the next process event.
            if not frame.IsValid():
                return
            value.update(frame=frame.GetFrameID(), function=frame.GetFunctionName() or '',
                         stopFunction=thread.GetFrameAtIndex(0).GetFunctionName() or '')
            path, line = self.frame_source(frame)
            value.update(path=path, line=line)
            for var in list(frame.GetVariables(True, True, False, True))[:256]:
                value['variables'].append(f'{var.GetName()}: {var.GetTypeName()} = {var.GetValue() or var.GetSummary() or "{…}"}')
                value['variableExpressions'].append(var.GetName() or '')
            for thread in list(process)[:256]:
                value['threads'].append(dict(id=str(thread.GetIndexID()), label=str(thread)))
        emit(**value)

    def close(self):
        if not hasattr(self, 'debugger'):
            return
        process = self.target.GetProcess()
        if process.IsValid():
            process.Detach() if self.attached else process.Kill()
        self.lldb.SBDebugger.Destroy(self.debugger)


# GDB/MI values use C strings and recursively nested result tuples/lists.
def mi_value(text, pos=0):
    if pos >= len(text):
        raise ValueError('Incomplete MI value')
    if text[pos] == '"':
        end = pos + 1
        while end < len(text):
            if text[end] == '\\':
                end += 2
            elif text[end] == '"':
                # GDB uses octal escapes, which JSON does not support.
                raw = text[pos:end + 1]
                raw = re.sub(r'\\([0-7]{1,3})', lambda m: '\\u%04x' % int(m[1], 8), raw)
                return json.loads(raw), end + 1
            else:
                end += 1
        raise ValueError('Incomplete MI string')
    if text[pos] in '[{':
        closing = ']' if text[pos] == '[' else '}'
        values, fields = [], {}
        pos += 1
        while pos < len(text) and text[pos] != closing:
            match = re.match(r'([\w-]+)=', text[pos:])
            if match:
                key = match[1]
                item, pos = mi_value(text, pos + len(match[0]))
                fields[key] = item
                values.append(item)
            else:
                item, pos = mi_value(text, pos)
                values.append(item)
            if pos < len(text) and text[pos] == ',':
                pos += 1
        if pos >= len(text):
            raise ValueError('Incomplete MI collection')
        return (fields if closing == '}' else values), pos + 1
    raise ValueError('Invalid MI value')


def mi_fields(text):
    return mi_value('{' + text + '}')[0]


def gdb_breakpoints(data):
    result = []
    for bp in data.get('BreakpointTable', {}).get('body', []):
        if 'breakpoint' not in bp.get('type', 'breakpoint') or '.' in bp.get('number', ''):
            continue
        locations = []
        for location in bp.get('locations', [bp]):
            path = location.get('fullname', location.get('file', ''))
            line = int(location.get('line', 0))
            if path and line > 0:
                locations.append(dict(path=path, line=line, enabled=location.get('enabled', 'y') == 'y'))
        if not locations:
            path, colon, row = bp.get('original-location', '').rpartition(':')
            if colon and row.isdigit():
                locations.append(dict(path=os.path.abspath(path), line=int(row), enabled=True))
        result.append(dict(id=bp['number'], enabled=bp.get('enabled') == 'y',
                           pending=bp.get('addr') == '<PENDING>', locations=locations,
                           description=bp.get('original-location', bp.get('func', bp.get('what', '')))))
    # Older MI versions emit sublocations as separate body records.
    roots = {bp['id']: bp for bp in result}
    for location in data.get('BreakpointTable', {}).get('body', []):
        number = location.get('number', '')
        parent = roots.get(number.split('.')[0]) if '.' in number else None
        path = location.get('fullname', location.get('file', ''))
        line = int(location.get('line', 0))
        if parent is not None and path and line > 0:
            entry = dict(path=path, line=line, enabled=location.get('enabled', 'y') == 'y')
            if entry not in parent['locations']:
                parent['locations'].append(entry)
    return result


class GDB:
    def __init__(self, backend, attach, target, args):
        self.attached = attach
        self.proc = subprocess.Popen([os.environ.get('UVIM_GDB', 'gdb'), '--quiet', '--nx', '--interpreter=mi2'],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, text=True, bufsize=1)
        self.lines = queue.Queue()
        def reader():
            for line in self.proc.stdout:
                self.lines.put(line.rstrip())
            self.lines.put(None)
        threading.Thread(target=reader, daemon=True).start()
        self.master, self.slave = os.openpty()
        os.set_blocking(self.master, False)
        self.token = 0
        self.pending = {}
        self.send('-inferior-tty-set ' + json.dumps(os.ttyname(self.slave)))
        self.send('-gdb-set confirm off')
        self.send('-gdb-set pagination off')
        self.send('-gdb-set mi-async on')
        if attach:
            self.send('-target-attach ' + str(int(target)), 'startup')
        else:
            self.send('-file-exec-and-symbols ' + json.dumps(target), 'startup')
            if args:
                self.send('-exec-arguments ' + ' '.join(json.dumps(a) for a in args))
            self.send('-exec-run --start', 'startup')
        self.request_breakpoints()
        emit(state='ready', variables=[], threads=[], path='', line=0)

    def request_breakpoints(self):
        if 'breakpoints' not in self.pending.values():
            self.send('-break-list', 'breakpoints')

    def send(self, text, kind=''):
        self.token += 1
        self.pending[str(self.token)] = kind
        self.proc.stdin.write(str(self.token) + text + '\n')
        self.proc.stdin.flush()

    def command(self, text):
        text = text.strip()
        verb, _, rest = text.partition(' ')
        request = inspection_request(text)
        if request:
            kind, expression, count = request
            if kind == 'layout':
                self.send('-data-evaluate-expression ' + json.dumps(f'sizeof({expression})'),
                          ('inspection-size', expression, True))
                return
            self.send('-data-evaluate-expression ' + json.dumps(f'(unsigned long long)({expression})'),
                      ('inspection-address', kind, expression, count))
            return
        if verb in ('p', 'print') and rest.strip() and not rest.startswith('/'):
            self.send('-data-evaluate-expression ' + json.dumps(rest), ('inspection-value', rest))
            return
        aliases = {'run': '-exec-run', 'r': '-exec-run', 'continue': '-exec-continue',
                   'c': '-exec-continue', 'next': '-exec-next', 'n': '-exec-next',
                   'step': '-exec-step', 's': '-exec-step', 'finish': '-exec-finish',
                   'f': '-exec-finish', 'interrupt': '-exec-interrupt --all'}
        if text == 'bps':
            self.request_breakpoints()
        elif verb in ('b', 'break'):
            self.send('-break-insert -f ' + json.dumps(rest.strip('"')), 'breakpoint-change')
        elif verb in ('delete', 'enable', 'disable'):
            if not re.fullmatch(r'[1-9]\d*', rest.strip()):
                raise ValueError(f'Usage: {verb} BREAKPOINT_ID')
            self.send('-break-' + verb + ' ' + rest.strip(), 'breakpoint-change')
        elif verb == 'thread':
            self.send('-thread-select ' + str(int(rest)), 'select')
        elif verb in ('locals', 'threads'):
            native = 'info locals' if verb == 'locals' else 'info threads'
            self.send('-interpreter-exec console ' + json.dumps(native), 'refresh')
        elif verb in aliases:
            self.send(aliases[verb])
        else:
            self.send('-interpreter-exec console ' + json.dumps(text), 'refresh')

    def refresh(self):
        self.send('-stack-info-frame', 'frame')
        self.send('-stack-list-variables --simple-values', 'variables')
        self.send('-thread-info', 'threads')
        self.request_breakpoints()

    def poll(self):
        if self.master >= 0:
            try:
                data = os.read(self.master, 8192)
                if data:
                    emit(output=data.decode('utf-8', errors='replace').splitlines())
            except BlockingIOError:
                pass
        for _ in range(256):
            try:
                line = self.lines.get_nowait()
            except queue.Empty:
                break
            if line is None:
                raise RuntimeError('GDB disconnected')
            if line.startswith(('~', '@', '&')):
                emit(output=mi_value(line[1:])[0].splitlines())
                continue
            match = re.match(r'(\d*)([\^*=])([\w-]+)(?:,(.*))?$', line)
            if not match:
                continue
            token, prefix, result, payload = match.groups()
            data = mi_fields(payload or '')
            if prefix == '=' and result.startswith('breakpoint-'):
                self.request_breakpoints()
            elif prefix == '*' and result == 'running':
                emit(state='running')
            elif prefix == '*' and result == 'stopped':
                if data.get('reason', '').startswith('exited'):
                    emit(state='exited', variables=[], threads=[])
                else:
                    emit(state='stopped')
                    self.send('-stack-list-frames 0 63', 'source-frames')
            elif prefix == '^':
                kind = self.pending.pop(token, '')
                if result == 'error':
                    if isinstance(kind, tuple) and kind[0] == 'inspection-function':
                        _, expression, address, count = kind
                        self.send(f'-data-disassemble -s 0x{address:x} -e 0x{address + count * 16:x} -- 2',
                                  ('inspection-assembly', expression, address, count))
                        continue
                    if isinstance(kind, tuple) and kind[0] in ('inspection-size', 'inspection-layout-type'):
                        emit(output=['Layout unavailable: ' + data.get('msg', 'missing type information')],
                             inspection=kind[0] == 'inspection-size' and kind[2])
                        continue
                    if kind == 'source-frames':
                        self.refresh()
                    emit(**dict(output=[data.get('msg', 'Debugger command failed')],
                                inspection=isinstance(kind, tuple),
                                **({'state': 'error'} if kind == 'startup' else {})))
                elif isinstance(kind, tuple):
                    if kind[0] == 'inspection-value':
                        emit(output=[f'{kind[1]} = {data.get("value", "<unavailable>")}'], inspection=True)
                        self.send('-data-evaluate-expression ' + json.dumps(f'sizeof({kind[1]})'),
                                  ('inspection-size', kind[1], False))
                        self.refresh()
                    elif kind[0] == 'inspection-size':
                        _, expression, focus = kind
                        emit(output=[f'Storage: {expression}, {data.get("value", "unknown")} bytes'], inspection=focus)
                        self.send('-interpreter-exec console ' + json.dumps('ptype /o ' + expression),
                                  ('inspection-layout-type', expression))
                    elif kind[0] == 'inspection-layout-type':
                        pass  # ptype /o publishes field offsets and holes via console records.
                    elif kind[0] == 'inspection-address':
                        _, operation, expression, count = kind
                        try:
                            address = int(data.get('value', '').split()[0], 0)
                            if not 0 < address < 2**64:
                                raise ValueError()
                        except (ValueError, IndexError):
                            emit(output=[f'{expression} is not a readable, non-null address'], inspection=True)
                            continue
                        if operation == 'mem':
                            self.send(f'-data-read-memory-bytes 0x{address:x} {count}',
                                      ('inspection-memory', expression, address, count))
                        else:
                            self.send(f'-data-disassemble -a 0x{address:x} -- 2',
                                      ('inspection-function', expression, address, count))
                    else:
                        _, expression, address, count = kind
                        rows = [f'{"Memory" if kind[0] == "inspection-memory" else "Disassembly"}: {expression} @ 0x{address:x}']
                        if kind[0] == 'inspection-memory':
                            total = 0
                            for block in data.get('memory', []):
                                chunk = bytes.fromhex(block.get('contents', ''))[:count - total]
                                rows += memory_rows(int(block['begin'], 0), chunk)
                                total += len(chunk)
                            if total < count:
                                rows.append(f'Only {total} of {count} bytes are readable')
                        else:
                            instructions = data.get('asm_insns', [])
                            if kind[0] == 'inspection-function' and instructions:
                                low = min(int(i['address'], 0) for i in instructions)
                                high = max(int(i['address'], 0) + len(i.get('opcodes', '').split()) for i in instructions)
                                rows.append(f'Function: {instructions[0].get("func-name", "?")}, {high - low} code bytes')
                            else:
                                rows.append('Function size: unavailable; displaying bytes at the requested address')
                            rows.append('Function padding: not identified separately by debug symbols')
                            shown = [i for i in instructions if int(i['address'], 0) >= address][:count]
                            for instruction in shown:
                                symbol = instruction.get('func-name', '')
                                offset = instruction.get('offset', '0')
                                rows.append(f'{instruction.get("address", "?")}  {instruction.get("inst", "?")}  '
                                            f'[{instruction.get("opcodes", "")}]  <{symbol}+{offset}>')
                            rows.append(f'Displayed: {len(shown)} instructions, ' +
                                        str(sum(len(i.get('opcodes', '').split()) for i in shown)) + ' bytes')
                        emit(output=rows, inspection=True)
                        if kind[0] == 'inspection-memory':
                            self.send('-data-evaluate-expression ' + json.dumps(f'sizeof(*({expression}))'),
                                      ('inspection-size', f'*({expression})', False))
                elif kind == 'breakpoints':
                    emit(breakpoints=gdb_breakpoints(data))
                elif kind == 'breakpoint-change':
                    self.request_breakpoints()
                elif kind == 'frame':
                    frame = data.get('frame', {})
                    emit(path=frame.get('fullname', frame.get('file', '')), line=int(frame.get('line', 0)),
                         frame=int(frame.get('level', 0)), function=frame.get('func', ''))
                elif kind == 'variables':
                    emit(variables=[f'{v.get("name")}: {v.get("type", "")} = {v.get("value", "{…}")}'
                                    for v in data.get('variables', [])[:256]],
                         variableExpressions=[v.get('name', '') for v in data.get('variables', [])[:256]])
                elif kind == 'threads':
                    emit(threads=[dict(id=t['id'], label=f'{t["id"]} {t.get("name", t.get("target-id", ""))} {t.get("state", "")}')
                                  for t in data.get('threads', [])[:256]])
                elif kind == 'source-frames':
                    frames = data.get('stack', [])
                    readable = [f for f in frames if readable_source(f.get('fullname', f.get('file', '')), f.get('line', 0))]
                    preferred = next((f for f in readable if application_source(f.get('fullname', f.get('file', '')), f.get('func', ''))), None)
                    frame = preferred or next(iter(readable), None)
                    emit(stopFunction=frames[0].get('func', '') if frames else '')
                    if frame is not None:
                        self.send('-stack-select-frame ' + str(int(frame['level'])), 'refresh')
                    else:
                        self.refresh()
                elif kind == 'select':
                    self.send('-stack-list-frames 0 63', 'source-frames')
                elif kind == 'refresh':
                    self.refresh()

    def close(self):
        if not hasattr(self, 'proc'):
            return
        if self.proc.poll() is None:
            try:
                self.send('-target-detach' if self.attached else '-interpreter-exec console "kill"')
                self.send('-gdb-exit')
                self.proc.wait(timeout=2)
            except (OSError, subprocess.TimeoutExpired):
                self.proc.kill()
                self.proc.wait()
        if hasattr(self, 'master'):
            os.close(self.master)
            os.close(self.slave)


def main():
    backend, action, target, *args = sys.argv[1:]
    engine = None
    try:
        cls = GDB if backend == 'gdb' else LLDB
        engine = cls.__new__(cls)
        engine.__init__(backend, action == 'attach', target, args)
        pending = b''
        while True:
            engine.poll()
            if select.select([sys.stdin], [], [], 0.05)[0]:
                chunk = os.read(sys.stdin.fileno(), 65536)
                if not chunk:
                    break
                pending += chunk
                while b'\n' in pending:
                    line, pending = pending.split(b'\n', 1)
                    command = json.loads(line)
                    if command == 'quit':
                        return
                    try:
                        engine.command(command)
                    except Exception as error:
                        emit(output=[str(error)], inspection=True)
    except Exception as error:
        emit(state='error', output=[str(error)])
    finally:
        if engine:
            engine.close()


if __name__ == '__main__':
    main()
