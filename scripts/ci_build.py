#!/usr/bin/env python3
"""Run a build, keep its log, and fail CI on project compiler warnings."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
WARNING = re.compile(r"^(.*?)\bwarning(?:\s+[a-z]+\d+)?\s*:", re.IGNORECASE)


def warning_kind(line):
    """Keep standard-library diagnostics caused by our template instantiations."""
    match = WARNING.search(ANSI.sub("", line))
    if not match:
        return None
    location = match.group(1).replace("\\", "/").lower()
    if re.search(r"(?:^|/)(?:_deps|third_party)(?:/|$)", location):
        return "dependency"
    return "project"


def build(command, log_path, summary_path):
    log_path = Path(log_path)
    summary_path = Path(summary_path)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    warnings = dict.fromkeys([])
    suppressed = 0
    with log_path.open("w", encoding="utf-8") as log:
        try:
            process = subprocess.Popen(
                command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, encoding="utf-8", errors="replace", bufsize=1,
            )
        except OSError as error:
            message = f"Unable to start build: {error}\n"
            log.write(message)
            print(message, end="", flush=True)
            returncode = 1
        else:
            with process:
                for line in process.stdout:
                    log.write(line)
                    kind = warning_kind(line)
                    if kind == "dependency":
                        suppressed += 1
                        continue
                    print(line, end="", flush=True)
                    if kind == "project":
                        warnings[ANSI.sub("", line).strip()] = None
                returncode = process.wait()

    result = "FAILED" if returncode or warnings else "PASSED"
    report = [
        f"## Compiler warning check: {result}", "",
        f"Build exit code: {returncode}",
        f"Project warnings: {len(warnings)}",
        f"Additional third-party warnings hidden: {suppressed}", "",
    ]
    if warnings:
        report += ["```text", *warnings, "```", ""]
    summary = "\n".join(report)
    summary_path.write_text(summary, encoding="utf-8")
    github_summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if github_summary:
        with Path(github_summary).open("a", encoding="utf-8") as output:
            output.write(summary + "\n")
    print("\n" + summary, flush=True)
    print(f"Full build log: {log_path}", flush=True)
    return returncode or (1 if warnings else 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", default="build/ci-build.log")
    parser.add_argument("--summary", default="build/ci-warnings.md")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("a build command is required after --")
    return build(command, args.log, args.summary)


if __name__ == "__main__":
    sys.exit(main())
