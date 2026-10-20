#!/usr/bin/env python3
"""Create and run a C# example in ~/test-cs-project-for-uvim."""

import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

import csharp_env


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Create a local C# example, build it and run it. No global .NET installation.",
        epilog="Run from anywhere. Open the resulting project with its csharp-env.py helper.")
    parser.add_argument("directory", nargs="?", type=Path,
                        default=Path.home() / "test-cs-project-for-uvim",
                        help="destination (default: ~/test-cs-project-for-uvim)")
    parser.add_argument("--template", choices=csharp_env.TEMPLATES, default="lsp",
                        help="starter: lsp (default), hello or classlib")
    parser.add_argument("--no-restore", action="store_true",
                        help="generate files only; do not download, build or run")
    parser.add_argument("--update-helper", action="store_true",
                        help="update an existing example's helper and generated guide; preserve C# source edits")
    args = parser.parse_args(argv)
    root = args.directory.expanduser().resolve()
    try:
        manifest = root / csharp_env.MANIFEST
        if manifest.exists():
            config = csharp_env.read_manifest(root)
            if (config.get("template") != args.template or
                    not (root / "UvimCSharpExample.csproj").is_file()):
                raise csharp_env.SetupError("This directory contains a different project; choose another directory.")
            if args.update_helper:
                shutil.copyfile(Path(csharp_env.__file__).resolve(), root / csharp_env.HELPER)
                (root / "CSHARP_ENV.md").write_text(
                    csharp_env.project_guide("UvimCSharpExample", args.template), encoding="utf-8")
                if not (root / csharp_env.REQUIREMENTS).exists():
                    csharp_env.write_requirements(root, config)
            if not args.no_restore:
                csharp_env.restore(root)
        else:
            csharp_env.init(root, "UvimCSharpExample", False, "latest",
                            csharp_env.DEFAULT_LSP, template=args.template,
                            no_restore=args.no_restore)
        if not args.no_restore:
            env = csharp_env.environment(root)
            csharp_env.run([csharp_env.dotnet_path(root), "build", "--no-restore"], root, env)
            if args.template != "classlib":
                csharp_env.run([csharp_env.dotnet_path(root), "run", "--no-build", "--no-restore"], root, env)
        print("\nCommands to continue:")
        if os.name == "nt":
            print("Set-Location -LiteralPath '" + str(root).replace("'", "''") + "'")
            python = "py -3"
        else:
            print("cd " + shlex.quote(str(root)))
            python = "python3"
        if args.no_restore:
            print(f"{python} csharp-env.py install -r requirements-csharp.txt")
        print(f"{python} csharp-env.py dotnet build")
        if args.template != "classlib":
            print(f"{python} csharp-env.py run")
        print(f"{python} csharp-env.py edit")
        print("Read CSHARP_ENV.md in the project for the step-by-step editor guide.")
    except (csharp_env.SetupError, OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"create-csharp-example: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
