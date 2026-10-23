#!/usr/bin/env python3
"""Build native debugger fixtures and optionally keep them running for attachment."""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time

REPO = Path(__file__).resolve().parents[1]
LANGUAGES = ('c', 'cpp', 'mlang')
EXTENSIONS = {'c': 'c', 'cpp': 'cpp', 'mlang': 'mla'}


def integer_range(minimum, maximum):
    def parse(value):
        number = int(value)
        if not minimum <= number <= maximum:
            raise argparse.ArgumentTypeError(f'expected {minimum}..{maximum}')
        return number
    return parse


def compiler(language, args):
    explicit = getattr(args, {'c': 'cc', 'cpp': 'cxx', 'mlang': 'mlang'}[language])
    env_name = {'c': 'CC', 'cpp': 'CXX', 'mlang': 'MLANG'}[language]
    configured = explicit or os.environ.get(env_name)
    if configured:
        command = shlex.split(configured)
        if not command or not shutil.which(command[0]):
            raise ValueError(f'{language}: compiler not found: {configured}')
        command[0] = str(Path(shutil.which(command[0])).resolve())
        return command
    candidates = {'c': ['clang', 'gcc', 'cc'], 'cpp': ['clang++', 'g++', 'c++'],
                  'mlang': ['mlang', str(REPO.parent / 'mlang/build/mlang')]}[language]
    for candidate in candidates:
        path = shutil.which(candidate)
        if path:
            return [str(Path(path).resolve())]
    raise ValueError(f'{language}: compiler not found; use --{env_name.lower()} or set {env_name}')


def render(language, threads, iterations, interval_ms):
    text = (REPO / 'examples/debugger' / f'demo.{EXTENSIONS[language]}.in').read_text()
    replacements = {'__THREADS__': str(threads), '__THREAD_STORAGE__': str(max(1, threads)),
                    '__ITERATIONS__': str(iterations), '__INTERVAL_MS__': str(interval_ms),
                    '__SPAWN_THREADS__': '\n'.join(
                        f'    let worker_{i}: std::thread::thread = thread::spawn(worker, {i + 1});'
                        for i in range(threads)),
                    '__JOIN_THREADS__': '\n'.join(f'    join(worker_{i});' for i in range(threads)),
                    '__ATTACH_DECLARATION__': '', '__ATTACH_SETUP__': ''}
    if sys.platform.startswith('linux'):
        replacements['__ATTACH_DECLARATION__'] = (
            '// Linux PR_SET_PTRACER: allow a debugger in another terminal to attach.\n'
            'extern fn prctl(option: i32, pid: i64, arg3: i64, arg4: i64, arg5: i64) -> i32;')
        replacements['__ATTACH_SETUP__'] = '    let attach_status: i32 = prctl(0x59616d61, -1, 0, 0, 0);'
    for token, value in replacements.items():
        text = text.replace(token, value)
    return text


def build(language, command, root, args):
    directory = root / language
    directory.mkdir()
    source = directory / f'demo.{EXTENSIONS[language]}'
    binary = directory / f'debugger-{language}'
    text = render(language, args.threads, args.iterations, args.interval_ms)
    source.write_text(text)
    flags = ['-g', '-O0']
    if language != 'mlang':
        flags += ['-fno-omit-frame-pointer', '-pthread', '-std=c11' if language == 'c' else '-std=c++17']
    # Current MLang link commands do not quote object paths. Keep its temporary
    # linker files in a space-free directory while retaining the real source path
    # in DWARF, and copy the executable and dSYM into the requested fixture root.
    staging = None
    workdir, build_binary = directory, binary
    if language == 'mlang' and any(c.isspace() for c in str(directory)):
        staging = tempfile.TemporaryDirectory(prefix='uvim-mlang-build-', dir='/tmp')
        workdir = Path(staging.name)
        build_binary = workdir / binary.name
    argv = command + flags + [str(source), '-o', str(build_binary)]
    print(f'Building {language}: {shlex.join(argv)}', flush=True)
    try:
        result = subprocess.run(argv, cwd=workdir, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (directory / 'build.log').write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f'{language} build failed ({result.returncode}). See {directory / "build.log"}\n{result.stdout}')
        if staging:
            shutil.copy2(build_binary, binary)
            symbols = Path(str(build_binary) + '.dSYM')
            if symbols.exists():
                shutil.copytree(symbols, Path(str(binary) + '.dSYM'))
    finally:
        if staging:
            staging.cleanup()
    breakpoint = next(i for i, line in enumerate(text.splitlines(), 1) if 'DEBUG_BREAKPOINT:' in line)
    return {'language': language, 'source': str(source), 'binary': str(binary),
            'breakpoint': breakpoint, 'workers': args.threads, 'build_command': argv,
            'run_log': str(directory / 'run.log')}


def editor_command(verb, argument, backend):
    # Editor startup commands parse shell-style quotes; breakpoints use the full rest of the line.
    prefix = '' if backend == 'lldb' else f'--backend {backend} '
    return f':{verb} {prefix}{shlex.quote(str(argument))}'


def guide(fixtures, backend):
    lines = ['Native debugger fixtures (C, C++, MLang)', '']
    for item in fixtures:
        lines += [f'[{item["language"]}] {item["workers"]} worker threads + main',
                  editor_command('dbgrun', item['binary'], backend)]
        if 'pid' in item:
            lines += [editor_command('dbgattach', item['pid'], backend), f'Program output: {item["run_log"]}']
        lines += [f':b {item["source"]}:{item["breakpoint"]}', ':c', '']
    lines += ['At the breakpoint: inspect count, worker_id, round, enabled, ratio, message, point, and values.',
              'Use j/k for rows; Tab or Ctrl+Shift+H/J/K/L for panes; v for Variables; t for Threads.',
              'Enter selects a thread. Attach displays a readable application caller when stopped in sleep.',
              'Try :n, :s, :f, :bt, :p count, and Ctrl-C while running.',
              'Breakpoints are reached repeatedly by main and workers. Set the breakpoint, then continue.',
              'q closes a launched session or detaches an attached session.',
              'Zero iterations means run until stopped. Stop the --run supervisor with Ctrl-C.',
              'Generated files remain available after the script exits.',
              'These are native fixtures; managed/interpreted source debugging needs additional adapters.']
    return '\n'.join(lines) + '\n'


def save(root, fixtures, backend):
    (root / 'manifest.json').write_text(json.dumps(fixtures, indent=2) + '\n')
    content = guide(fixtures, backend)
    (root / 'COMMANDS.txt').write_text(content)
    print('\n' + content, flush=True)


def run(fixtures, root, backend):
    children = []
    try:
        for item in fixtures:
            log = open(item['run_log'], 'w')
            try:
                child = subprocess.Popen([item['binary']], stdin=subprocess.DEVNULL,
                                         stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            finally:
                log.close()
            children.append(child)
            item['pid'] = child.pid
        save(root, fixtures, backend)
        print('Targets are running. Attach from the other terminal; Ctrl-C here stops them.', flush=True)
        while any(child.poll() is None for child in children):
            time.sleep(0.2)
        failures = [child.returncode for child in children if child.returncode]
        return 1 if failures else 0
    except KeyboardInterrupt:
        print('\nStopping fixture processes.', flush=True)
        return 0
    finally:
        for child in children:
            if child.poll() is None:
                child.terminate()
                # A debugger may have stopped the target when the supervisor is interrupted.
                try:
                    os.kill(child.pid, signal.SIGCONT)
                except ProcessLookupError:
                    pass
        for child in children:
            try:
                child.wait(timeout=3)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.ArgumentDefaultsHelpFormatter,
        epilog='Examples: --language all; --language cpp --run; --language mlang --threads 0 --iterations 5')
    parser.add_argument('-l', '--language', choices=(*LANGUAGES, 'all'), default='all')
    parser.add_argument('--output-dir', type=Path, default=Path('/tmp'), help='parent directory for a new unique fixture directory')
    parser.add_argument('--threads', type=integer_range(0, 32), default=2, help='worker threads per program, in addition to main; 0 disables workers')
    parser.add_argument('--iterations', type=integer_range(0, 1000000), default=0, help='rounds per thread; 0 runs until stopped')
    parser.add_argument('--interval-ms', type=integer_range(1, 60000), default=1000, help='delay between rounds')
    parser.add_argument('--run', action='store_true', help='start targets, print attach commands, and supervise them until Ctrl-C or exit')
    parser.add_argument('--backend', choices=('lldb', 'gdb', 'mladbg'), default='lldb', help='backend used in printed editor commands')
    parser.add_argument('--cc', help='C compiler command (or CC)')
    parser.add_argument('--cxx', help='C++ compiler command (or CXX)')
    parser.add_argument('--mlang', help='MLang compiler command (or MLANG); also checks ../mlang/build/mlang')
    args = parser.parse_args(argv)
    if sys.platform not in ('darwin', 'linux'):
        parser.error('the native debugger fixtures currently require macOS or Linux')
    try:
        languages = LANGUAGES if args.language == 'all' else (args.language,)
        commands = {language: compiler(language, args) for language in languages}
        parent = args.output_dir.expanduser().resolve()
        parent.mkdir(parents=True, exist_ok=True)
        root = Path(tempfile.mkdtemp(prefix='uvim-debugger-', dir=parent))
        print(f'Fixture directory: {root}', flush=True)
        fixtures = [build(language, commands[language], root, args) for language in languages]
        if args.run:
            return run(fixtures, root, args.backend)
        save(root, fixtures, args.backend)
        print(f'To run a target for attachment: {shlex.quote(fixtures[0]["binary"])}', flush=True)
        print(f'Commands saved to {root / "COMMANDS.txt"}', flush=True)
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        print(f'debugger-examples: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
