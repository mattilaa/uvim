"""Regression tests for CI compiler-warning reporting and failure handling."""

import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "ci_build.py"
SPEC = importlib.util.spec_from_file_location("ci_build", SCRIPT)
CI_BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CI_BUILD)


class CiBuildTest(unittest.TestCase):
    def test_compiler_formats(self):
        for diagnostic in [
            "src/editor.cpp:42:3: warning: unused variable [-Wunused-variable]",
            r"D:\a\uvim\src\editor.cpp(42,3): warning C4244: int to char",
            "LINK : warning LNK4098: conflicting runtime",
            "clang++: warning: unused command-line argument",
            "\x1b[33msrc/editor.cpp:42:3: warning: unused variable\x1b[0m",
        ]:
            with self.subTest(diagnostic=diagnostic):
                self.assertEqual(CI_BUILD.warning_kind(diagnostic), "project")
        self.assertIsNone(CI_BUILD.warning_kind("0 Warning(s)"))

    def test_dependencies_are_quiet_but_our_template_warnings_are_kept(self):
        for diagnostic in [
            "build/_deps/fmt-src/base.h:10: warning: unused result",
            r"D:\a\uvim\build\_deps\fmt-src\base.h(930,12): warning C4834: nodiscard",
            "third_party/yyjson/yyjson.c:42: warning: conversion",
        ]:
            self.assertEqual(CI_BUILD.warning_kind(diagnostic), "dependency")
        # A warning in the STL may originate from our std::transform callback.
        self.assertEqual(CI_BUILD.warning_kind(
            r"C:\Program Files\MSVC\include\algorithm(4248,24): warning C4244: int to char"
        ), "project")
        self.assertEqual(CI_BUILD.warning_kind(
            "src/editor.cpp:42: warning: cannot find build/_deps/foo.h"
        ), "project")

    def run_build(self, code):
        temp = tempfile.TemporaryDirectory(prefix="uvim_ci_build_")
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        log = root / "nested" / "build.log"
        summary = root / "report.md"
        github = root / "github-summary.md"
        result = subprocess.run(
            [sys.executable, "-B", str(SCRIPT), "--log", str(log),
             "--summary", str(summary), "--", sys.executable, "-c", code],
            capture_output=True, text=True,
            env={**os.environ, "GITHUB_STEP_SUMMARY": str(github)},
        )
        self.assertEqual(summary.read_text(), github.read_text().rstrip() + "\n")
        return result, log.read_text(), summary.read_text()

    def test_clean_build_passes(self):
        result, log, summary = self.run_build("print('Built uvim')")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Built uvim", log)
        self.assertIn("Project warnings: 0", summary)

    def test_warning_fails_after_the_build_finishes_and_is_listed_once(self):
        result, log, summary = self.run_build(
            "print('src/editor.cpp:42: warning: narrowing');"
            "print('src/editor.cpp:42: warning: narrowing');"
            "print('build finished')"
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("build finished", log)
        self.assertEqual(summary.count("src/editor.cpp:42"), 1)
        self.assertIn("Project warnings: 1", summary)
        self.assertLess(result.stdout.index("build finished"),
                        result.stdout.index("## Compiler warning check"))

    def test_dependency_warning_is_hidden_but_preserved_in_full_log(self):
        result, log, summary = self.run_build(
            "print('build/_deps/fmt-src/base.h:42: warning: nodiscard')"
        )
        self.assertEqual(result.returncode, 0)
        self.assertNotIn("warning: nodiscard", result.stdout)
        self.assertIn("warning: nodiscard", log)
        self.assertIn("third-party warnings hidden: 1", summary)

    def test_compiler_failure_is_preserved_without_warnings(self):
        result, log, summary = self.run_build(
            "import sys; print('fatal build error'); sys.exit(7)"
        )
        self.assertEqual(result.returncode, 7)
        self.assertIn("fatal build error", log)
        self.assertIn("Build exit code: 7", summary)
        self.assertIn("FAILED", summary)


if __name__ == "__main__":
    unittest.main()
