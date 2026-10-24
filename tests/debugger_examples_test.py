"""Build and exercise the interactive debugger fixture generator."""
import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/create_debugger_examples.py'
SPEC = importlib.util.spec_from_file_location('debugger_examples', SCRIPT)
EXAMPLES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXAMPLES)


@unittest.skipUnless(sys.platform in ('darwin', 'linux'), 'requires macOS/Linux')
class DebuggerExamplesTests(unittest.TestCase):
    def invoke(self, root, *args):
        return subprocess.run([sys.executable, '-B', str(SCRIPT), '--output-dir', str(root), *args],
                              capture_output=True, text=True, timeout=60)

    def test_available_languages_build_run_and_generate_commands(self):
        if sys.platform not in ('darwin', 'linux'):
            self.skipTest('native debugger fixtures require macOS/Linux')
        tested = 0
        for language in EXAMPLES.LANGUAGES:
            try:
                EXAMPLES.compiler(language, EXAMPLES.argparse.Namespace(cc=None, cxx=None, mlang=None))
            except ValueError:
                continue
            for threads in (0, 2):
                with self.subTest(language=language, threads=threads), tempfile.TemporaryDirectory(prefix='uvim examples ') as directory:
                    result = self.invoke(directory, '--language', language, '--threads', str(threads),
                                         '--iterations', '3', '--interval-ms', '5', '--run', '--backend', 'gdb')
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    root, = Path(directory).glob('uvim-debugger-*')
                    item, = json.loads((root / 'manifest.json').read_text())
                    self.assertTrue(Path(item['binary']).is_file())
                    source = Path(item['source']).read_text()
                    self.assertIn('DEBUG_BREAKPOINT:', source.splitlines()[item['breakpoint'] - 1])
                    self.assertNotIn('__ITERATIONS__', source)
                    self.assertEqual(item['workers'], threads)
                    self.assertIn(':dbgattach --backend gdb ' + str(item['pid']), result.stdout)
                    self.assertIn(f':b {item["source"]}:{item["breakpoint"]}', result.stdout)
                    output = Path(item['run_log']).read_text()
                    self.assertEqual(output.count('main: round='), 3, output)
                    self.assertIn(f'workers={threads}', output)
                    tested += 1
        if not tested:
            self.skipTest('no fixture compilers installed')

    def test_cpp_compiler_keeps_symlink_driver_name_and_configured_arguments(self):
        with tempfile.TemporaryDirectory(prefix='uvim compiler ') as directory:
            target = Path(directory) / 'clang'
            target.write_text('#!/bin/sh\nexit 0\n')
            target.chmod(0o755)
            driver = Path(directory) / 'clang++'
            driver.symlink_to(target.name)
            args = EXAMPLES.argparse.Namespace(cc=None, cxx=None, mlang=None)
            with patch.dict(os.environ, {'CXX': ''}), patch.object(
                    EXAMPLES.shutil, 'which', side_effect=lambda name: str(driver) if name == 'clang++' else None):
                self.assertEqual(EXAMPLES.compiler('cpp', args), [str(driver)])
            args.cxx = EXAMPLES.shlex.join([str(driver), '-stdlib=libc++'])
            self.assertEqual(EXAMPLES.compiler('cpp', args), [str(driver), '-stdlib=libc++'])

    def test_missing_compiler_and_invalid_thread_count_fail_before_creating_fixtures(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.invoke(directory, '--language', 'c', '--cc', '/missing/uvim-test-compiler')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('compiler not found', result.stderr)
            self.assertEqual(list(Path(directory).iterdir()), [])
            result = self.invoke(directory, '--threads', '-1')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('expected 0..32', result.stderr)

    @unittest.skipUnless(sys.platform in ('darwin', 'linux'), 'requires POSIX signals')
    def test_interrupt_reaps_the_running_target(self):
        try:
            EXAMPLES.compiler('c', EXAMPLES.argparse.Namespace(cc=None, cxx=None, mlang=None))
        except ValueError:
            self.skipTest('no C compiler installed')
        with tempfile.TemporaryDirectory() as directory:
            with open(Path(directory) / 'supervisor.log', 'w') as log:
                proc = subprocess.Popen([sys.executable, '-B', str(SCRIPT), '--language', 'c', '--run',
                                         '--output-dir', directory], stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 30
                    item = None
                    while time.monotonic() < deadline:
                        manifests = list(Path(directory).glob('uvim-debugger-*/manifest.json'))
                        if manifests:
                            try:
                                item, = json.loads(manifests[0].read_text())
                                break
                            except json.JSONDecodeError:
                                pass
                        if proc.poll() is not None:
                            self.fail('Supervisor exited early: ' + (Path(directory) / 'supervisor.log').read_text())
                        time.sleep(.05)
                    self.assertIsNotNone(item, 'Supervisor did not start')
                    proc.send_signal(signal.SIGINT)
                    self.assertEqual(proc.wait(timeout=10), 0)
                    with self.assertRaises(ProcessLookupError):
                        os.kill(item['pid'], 0)
                finally:
                    if proc.poll() is None:
                        proc.send_signal(signal.SIGINT)
                        proc.wait(timeout=10)


if __name__ == '__main__':
    unittest.main()
