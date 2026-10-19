#!/usr/bin/env python3
"""Create and use a project-local C# environment. Python 3.9+, stdlib only."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import urllib.request
import venv


MANIFEST = "csharp-env.json"
STATE = ".csharp-env"
HELPER = "csharp-env.py"
DEFAULT_LSP = "0.28.0"
IS_WINDOWS = sys.platform == "win32"
NUGET_CONFIG = '''<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <packageSources>
    <clear />
    <add key="nuget.org" value="https://api.nuget.org/v3/index.json" />
  </packageSources>
</configuration>
'''
PROGRAM = '''using CSharpExample;

var greeter = new Greeter("Hello");

// Use gd on Greet to jump to Greeter.cs.
Console.WriteLine(greeter.Greet("Ada"));
Console.WriteLine(greeter.Greet("Linus"));

// Completion: type greeter. on a new line in insert mode.
// Diagnostics: uncomment the next line, then inspect it with Space e.
// Console.WriteLine(greeter.Greet(42));
'''
GREETER = '''namespace CSharpExample;

public sealed class Greeter
{
    public string Greeting { get; }

    public Greeter(string greeting)
    {
        Greeting = greeting;
    }

    // Use Space g r on Greet to find both calls in Program.cs.
    public string Greet(string name)
    {
        return $"{Greeting}, {name}!";
    }
}
'''
PROJECT = '''<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
    <RestorePackagesWithLockFile>true</RestorePackagesWithLockFile>
  </PropertyGroup>
</Project>
'''
PROJECT_GUIDE = '''# Local C# environment

Run these commands from this directory (use `python3` on macOS/Linux):

```text
python csharp-env.py --help
python csharp-env.py restore
python csharp-env.py dotnet build
python csharp-env.py run
python csharp-env.py edit Program.cs
```

If uvim is outside PATH, use `edit --uvim /path/to/uvim Program.cs`.
The editor must have C# LSP support (the full preset/release binaries do).
Use gd on Greet for its definition, Space g r for references, and type
greeter. in insert mode for completion. Uncomment the Greet(42) call to
produce a diagnostic; inspect it with Space e. Use :lspinfo for LSP status.
The first project load can take a while. The example prints:

```text
Hello, Ada!
Hello, Linus!
```

Python's .venv is optional for running the helper; it has no pip dependencies.
POSIX: `.venv/bin/python csharp-env.py run`
PowerShell: `& .\\.venv\\Scripts\\python.exe .\\csharp-env.py run`

csharp-env.json pins the SDK and csharp-ls versions. Commit it, global.json,
NuGet.Config, packages.lock.json, the source files and this helper. After
cloning, `restore` recreates the environment; use `restore --locked` to check
the committed NuGet lock file without updating its resolved dependencies.
Add a C# package with `dotnet add package NAME --version VERSION` through
this helper, then restore and commit the changed .csproj and lock file.

All downloaded tools/caches live in .csharp-env; Python lives in .venv.
No system SDK, global tool installation, shell profile or permanent PATH
changes are used. Environment variables are set only for child processes.
Use `dotnet` through this helper so it uses the local SDK and caches.
Your app may still access the system like any other program.

To reclaim the downloads, close the editor and delete .csharp-env and .venv
using your file manager; keep the manifest/helper/source to restore later.
bin and obj are generated build output and can also be deleted.
'''


class SetupError(Exception):
    pass


def run(args, root, env, capture=False):
    return subprocess.run(
        [str(arg) for arg in args], cwd=root, env=env, check=True,
        text=True, stdout=subprocess.PIPE if capture else None,
    )


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def read_manifest(root):
    path = root / MANIFEST
    if not path.is_file():
        raise SetupError(f"No {MANIFEST} in {root}; run init first.")
    config = json.loads(path.read_text(encoding="utf-8"))
    validate_manifest(config)
    return config


def validate_manifest(config):
    if not isinstance(config, dict):
        raise SetupError(f"{MANIFEST} must contain a JSON object.")
    sdk = config.get("sdk_version", "")
    lsp = config.get("csharp_ls_version", "")
    if not isinstance(sdk, str) or (sdk != "latest" and not re.fullmatch(r"\d+\.\d+\.\d+", sdk)):
        raise SetupError("sdk_version must be 'latest' or a stable SDK version.")
    if sdk != "latest" and int(sdk.split(".")[0]) != 10:
        raise SetupError("This starter project requires a .NET 10 SDK.")
    if not isinstance(lsp, str) or not re.fullmatch(r"\d+\.\d+\.\d+", lsp):
        raise SetupError("csharp_ls_version must be an exact stable version.")


def environment(root):
    state = root / STATE
    for name in ("home", "packages", "http-cache", "plugins-cache", "tmp", "bundles", "msbuild"):
        (state / name).mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    sdk = state / "sdk"
    env.update({
        "DOTNET_ROOT": str(sdk),
        "DOTNET_ROOT_X64": str(sdk),
        "DOTNET_ROOT_ARM64": str(sdk),
        "DOTNET_CLI_HOME": str(state / "home"),
        "DOTNET_BUNDLE_EXTRACT_BASE_DIR": str(state / "bundles"),
        "MSBuildUserExtensionsPath": str(state / "msbuild"),
        "NUGET_PACKAGES": str(state / "packages"),
        "NUGET_HTTP_CACHE_PATH": str(state / "http-cache"),
        "NUGET_PLUGINS_CACHE_PATH": str(state / "plugins-cache"),
        "DOTNET_CLI_TELEMETRY_OPTOUT": "1",
        "DOTNET_GENERATE_ASPNET_CERTIFICATE": "false",
        "DOTNET_NOLOGO": "1",
        "DOTNET_MULTILEVEL_LOOKUP": "0",
        "DOTNET_ADD_GLOBAL_TOOLS_TO_PATH": "0",
        "DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE": "true",
        "TMPDIR": str(state / "tmp"),
        "TEMP": str(state / "tmp"),
        "TMP": str(state / "tmp"),
    })
    python_bin = root / ".venv" / ("Scripts" if IS_WINDOWS else "bin")
    env["PATH"] = os.pathsep.join([
        str(python_bin), str(state / "tools"), str(sdk), env.get("PATH", "")
    ])
    return env


def dotnet_path(root):
    return root / STATE / "sdk" / ("dotnet.exe" if IS_WINDOWS else "dotnet")


def server_path(root):
    return root / STATE / "tools" / ("csharp-ls.exe" if IS_WINDOWS else "csharp-ls")


def download_installer(path, windows):
    url = "https://dot.net/v1/dotnet-install." + ("ps1" if windows else "sh")
    print(f"Downloading Microsoft installer: {url}", flush=True)
    with urllib.request.urlopen(url, timeout=120) as response:
        with path.open("wb") as output:
            shutil.copyfileobj(response, output)


def install_sdk(root, config, env):
    dotnet = dotnet_path(root)
    versions = []
    if dotnet.is_file():
        try:
            output = run([dotnet, "--list-sdks"], root, env, capture=True).stdout
            versions = [line.split()[0] for line in output.splitlines() if line.strip()]
        except (OSError, subprocess.CalledProcessError):
            # An interrupted SDK extraction may have left a broken host.
            pass
    requested = config["sdk_version"]
    matches = [v for v in versions if re.fullmatch(r"10\.\d+\.\d+", v)]
    if not (matches if requested == "latest" else requested in versions):
        downloads = root / STATE / "downloads"
        downloads.mkdir(exist_ok=True)
        windows = IS_WINDOWS
        installer = downloads / ("dotnet-install.ps1" if windows else "dotnet-install.sh")
        download_installer(installer, windows)
        if windows:
            shell = shutil.which("pwsh") or shutil.which("powershell")
            if not shell:
                raise SetupError("PowerShell is required to install the SDK on Windows.")
            command = [shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                       installer, "-Channel", "10.0", "-Version", requested,
                       "-InstallDir", dotnet.parent, "-NoPath"]
        else:
            shell = shutil.which("bash")
            if not shell:
                raise SetupError("Bash is required to install the SDK on POSIX.")
            command = [shell, installer, "--channel", "10.0", "--version", requested,
                       "--install-dir", dotnet.parent, "--no-path"]
        run(command, root, env)
    # Pin the selected SDK for subsequent restores and for the editor's MSBuild.
    if requested == "latest":
        output = run([dotnet, "--list-sdks"], root, env, capture=True).stdout
        versions = [line.split()[0] for line in output.splitlines() if line.strip()]
        matches = [v for v in versions if re.fullmatch(r"10\.\d+\.\d+", v)]
        if not matches:
            raise SetupError("The installer did not provide a stable .NET 10 SDK.")
        config["sdk_version"] = max(matches, key=lambda v: tuple(map(int, v.split("."))))
        write_json(root / MANIFEST, config)
    global_json = {"sdk": {"version": config["sdk_version"], "rollForward": "disable"}}
    write_json(root / "global.json", global_json)


def restore(root, locked=False):
    config = read_manifest(root)
    if not (root / ".venv").exists():
        print("Creating project-local Python venv (no pip dependencies).", flush=True)
        venv.EnvBuilder(with_pip=False).create(root / ".venv")
    env = environment(root)
    install_sdk(root, config, env)
    dotnet = dotnet_path(root)
    tools = root / STATE / "tools"
    installed = ""
    if tools.exists():
        installed = run([dotnet, "tool", "list", "--tool-path", tools], root, env,
                        capture=True).stdout
    rows = [line.split() for line in installed.splitlines()]
    version = config["csharp_ls_version"]
    current = next((row[1] for row in rows if len(row) > 1 and row[0] == "csharp-ls"), None)
    if current and (current != version or not server_path(root).is_file()):
        run([dotnet, "tool", "uninstall", "csharp-ls", "--tool-path", tools], root, env)
    if current != version or not server_path(root).is_file():
        run([dotnet, "tool", "install", "csharp-ls", "--tool-path", tools,
             "--version", version, "--configfile", root / "NuGet.Config"], root, env)
    projects = list(root.glob("*.sln")) + list(root.glob("*.slnx"))
    if not projects:
        projects = list(root.glob("*.csproj"))
    if len(projects) == 1:
        args = [dotnet, "restore", projects[0], "--use-lock-file",
                "--configfile", root / "NuGet.Config"]
        if locked:
            args.append("--locked-mode")
        run(args, root, env)
    elif projects:
        print("Multiple projects/solutions found; tools are ready. Use "
              "'dotnet restore PATH --use-lock-file' through this helper "
              "to choose one.", flush=True)
    print(f"Ready in {root}\nSDK {config['sdk_version']}; csharp-ls {version}", flush=True)


def init(root, name, empty, sdk_version, lsp_version):
    validate_manifest({"sdk_version": sdk_version, "csharp_ls_version": lsp_version})
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", name):
        raise SetupError("Project name must start with a letter and contain letters, digits or underscores.")
    files = {
        MANIFEST: json.dumps({"sdk_version": sdk_version,
                              "csharp_ls_version": lsp_version}, indent=2) + "\n",
        HELPER: Path(__file__).read_text(encoding="utf-8"),
        "NuGet.Config": NUGET_CONFIG,
        "CSHARP_ENV.md": PROJECT_GUIDE,
    }
    if not empty:
        files.update({f"{name}.csproj": PROJECT, "Program.cs": PROGRAM, "Greeter.cs": GREETER})
    # Reject collisions before writing files or downloading anything.
    collisions = [str(root / f) for f in [*files, "global.json", STATE, ".venv"]
                  if (root / f).exists() or (root / f).is_symlink()]
    if (root / ".gitignore").is_symlink():
        collisions.append(str(root / ".gitignore"))
    if not empty and any(root.glob("*.csproj")):
        raise SetupError("This directory already contains a C# project; use --empty to keep its sources.")
    if collisions:
        raise SetupError("Refusing to overwrite existing files/directories: " + ", ".join(collisions))
    root.mkdir(parents=True, exist_ok=True)
    for filename, content in files.items():
        (root / filename).write_text(content, encoding="utf-8")
    ignore = root / ".gitignore"
    existing = ignore.read_text(encoding="utf-8") if ignore.exists() else ""
    entries = ["/.venv/", "/.csharp-env/", "**/bin/", "**/obj/", "__pycache__/"]
    with ignore.open("a", encoding="utf-8") as output:
        output.write(("\n" if existing and not existing.endswith("\n") else "") +
                     "\n# Project-local C# environment\n" + "\n".join(entries) + "\n")
    restore(root)
    print(f"Next: cd {root}\nThen: python {HELPER} edit Program.cs\nHelp: python {HELPER} --help")


def parser():
    cli = argparse.ArgumentParser(
        description="Project-local C# SDK, LSP, NuGet caches and Python venv; no global installs.",
        epilog="Requires Python 3.9+, internet, Bash on POSIX or PowerShell on Windows. "
               "No pip packages required. Use a C#-enabled uVim build for edit.")
    cli.add_argument("-C", "--directory", type=Path, default=Path.cwd(),
                     help="project directory for restore/dotnet/run/edit (default: current directory)")
    commands = cli.add_subparsers(dest="command", required=True)
    create = commands.add_parser("init", help="create an environment and a two-file example project")
    create.add_argument("target", nargs="?", type=Path, default=Path.cwd(),
                        help="destination directory (default: current directory)")
    create.add_argument("--name", default="CSharpExample", help="project name (default: CSharpExample)")
    create.add_argument("--empty", action="store_true", help="keep existing sources; do not create the example")
    create.add_argument("--sdk-version", default="latest", help="exact .NET 10 SDK version; default: latest 10.0, then pinned")
    create.add_argument("--lsp-version", default=DEFAULT_LSP, help=f"exact csharp-ls version (default: {DEFAULT_LSP})")
    reset = commands.add_parser("restore", help="download pinned tools and restore project packages")
    reset.add_argument("--locked", action="store_true", help="restore NuGet packages in locked mode")
    for command, description in [("dotnet", "run any dotnet command with the local SDK and caches"),
                                  ("run", "run the example/project with dotnet run")]:
        item = commands.add_parser(command, help=description)
        item.add_argument("args", nargs=argparse.REMAINDER, help="arguments passed to dotnet")
    edit = commands.add_parser("edit", help="open uVim with the local C# language server")
    edit.add_argument("--uvim", default="uvim", help="uVim executable name or absolute path")
    edit.add_argument("files", nargs="*", default=["Program.cs"], help="files to open (default: Program.cs)")
    return cli


def main(argv=None):
    args = parser().parse_args(argv)
    root = args.directory.expanduser().resolve()
    try:
        if args.command == "init":
            init(args.target.expanduser().resolve(), args.name, args.empty,
                 args.sdk_version, args.lsp_version)
        elif args.command == "restore":
            restore(root, args.locked)
        else:
            read_manifest(root)
            if not dotnet_path(root).is_file() or not server_path(root).is_file():
                raise SetupError(f"Environment is incomplete; run python {HELPER} restore first.")
            env = environment(root)
            if args.command == "edit":
                run([args.uvim, "--csharp-lsp", "--csharp-lsp-path", server_path(root),
                     *(args.files or ["Program.cs"])], root, env)
            else:
                command_args = args.args
                if command_args[:1] == ["--"]:
                    command_args = command_args[1:]
                run([dotnet_path(root), *(["run"] if args.command == "run" else []),
                     *command_args], root, env)
    except (SetupError, OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"csharp-env: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
