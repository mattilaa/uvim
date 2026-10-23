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


class LLDB:
    def __init__(self, backend, attach, target, args):
        self.lldb = lldb_module()
        self.session = None
        self.attached = attach
        self.source_selection_pending = attach
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
        select_thread = text.startswith('thread ') and text[7:].strip().isdigit()
        if text == 'interrupt':
            error = self.target.GetProcess().Stop()
            if error.Fail():
                raise RuntimeError(str(error))
        elif self.session:
            self.session.command(text)
            emit(output=list(self.session.log)[-100:])
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
            emit(output=(result.GetOutput() + result.GetError()).splitlines())
        self.target = self.debugger.GetSelectedTarget()
        if select_thread:
            self.select_source_frame(prefer_application=True)
        self.snapshot()

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

    def snapshot(self):
        process = self.target.GetProcess()
        stopped = process.GetState() in (self.lldb.eStateStopped, self.lldb.eStateCrashed,
                                         self.lldb.eStateSuspended)
        state = 'stopped' if stopped else 'running' if process.GetState() in (
            self.lldb.eStateRunning, self.lldb.eStateStepping) else 'exited' if process.GetState() == self.lldb.eStateExited else 'ready'
        value = dict(state=state, variables=[], threads=[], path='', line=0, frame=0, function='', stopFunction='')
        if stopped:
            if self.source_selection_pending:
                self.select_source_frame(prefer_application=True)
            thread = process.GetSelectedThread()
            frame = thread.GetSelectedFrame()
            value.update(frame=frame.GetFrameID(), function=frame.GetFunctionName() or '',
                         stopFunction=thread.GetFrameAtIndex(0).GetFunctionName() or '')
            path, line = self.frame_source(frame)
            value.update(path=path, line=line)
            for var in list(frame.GetVariables(True, True, False, True))[:256]:
                value['variables'].append(f'{var.GetName()}: {var.GetTypeName()} = {var.GetValue() or var.GetSummary() or "{…}"}')
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
        emit(state='ready', variables=[], threads=[], path='', line=0)

    def send(self, text, kind=''):
        self.token += 1
        self.pending[str(self.token)] = kind
        self.proc.stdin.write(str(self.token) + text + '\n')
        self.proc.stdin.flush()

    def command(self, text):
        verb, _, rest = text.partition(' ')
        aliases = {'run': '-exec-run', 'r': '-exec-run', 'continue': '-exec-continue',
                   'c': '-exec-continue', 'next': '-exec-next', 'n': '-exec-next',
                   'step': '-exec-step', 's': '-exec-step', 'finish': '-exec-finish',
                   'f': '-exec-finish', 'interrupt': '-exec-interrupt --all'}
        if verb in ('b', 'break'):
            self.send('-break-insert -f ' + json.dumps(rest.strip('"')))
        elif verb == 'thread':
            self.send('-thread-select ' + str(int(rest)), 'select')
        elif verb in aliases:
            self.send(aliases[verb])
        else:
            self.send('-interpreter-exec console ' + json.dumps(text), 'refresh')

    def refresh(self):
        self.send('-stack-info-frame', 'frame')
        self.send('-stack-list-variables --simple-values', 'variables')
        self.send('-thread-info', 'threads')

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
            if prefix == '*' and result == 'running':
                emit(state='running', variables=[], threads=[])
            elif prefix == '*' and result == 'stopped':
                if data.get('reason', '').startswith('exited'):
                    emit(state='exited', variables=[], threads=[])
                else:
                    emit(state='stopped')
                    self.send('-stack-list-frames 0 63', 'source-frames')
            elif prefix == '^':
                kind = self.pending.pop(token, '')
                if result == 'error':
                    if kind == 'source-frames':
                        self.refresh()
                    emit(**dict(output=[data.get('msg', 'Debugger command failed')], **({'state': 'error'} if kind == 'startup' else {})))
                elif kind == 'frame':
                    frame = data.get('frame', {})
                    emit(path=frame.get('fullname', frame.get('file', '')), line=int(frame.get('line', 0)),
                         frame=int(frame.get('level', 0)), function=frame.get('func', ''))
                elif kind == 'variables':
                    emit(variables=[f'{v.get("name")}: {v.get("type", "")} = {v.get("value", "{…}")}'
                                    for v in data.get('variables', [])[:256]])
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
                        emit(output=[str(error)])
    except Exception as error:
        emit(state='error', output=[str(error)])
    finally:
        if engine:
            engine.close()


if __name__ == '__main__':
    main()
