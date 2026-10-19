# Start a C# project with a local environment

For a walkthrough you can follow command by command, start with
[Your first C# project with uVim](csharp_quickstart.md). This page is the
reference for local environment setup and maintenance.

Use `scripts/csharp_env.py` to create a C# project without installing a
system SDK or global language server. The same Python script works on
macOS, Linux and Windows, and includes `--help` for every command.

Python's `venv` isolates Python packages. C# needs its own SDK and NuGet
dependencies, so this setup creates both a Python `.venv` and a local .NET
environment. The helper uses only Python's standard library; there is no
`pip install` step or Python `requirements.txt` to maintain.

## Prerequisites

- Python 3.9 or later (`python3` on macOS/Linux, `py -3` or `python` on
  Windows). On Linux, Python's `venv` module may be a separate OS package.
- Bash on macOS/Linux, or PowerShell on Windows. The helper downloads and
  runs [Microsoft's SDK installer](https://learn.microsoft.com/dotnet/core/tools/dotnet-install-script)
  into the project directory.
- Internet access to Microsoft and NuGet on the first setup/restore.
- uVim with C# LSP enabled (the `full` preset and release binaries include
  it). The helper does not install uVim.

The local SDK still needs the native OS libraries listed in
[Microsoft's platform installation guidance](https://learn.microsoft.com/dotnet/core/install/).
The helper does not install OS packages or request administrator access.

## Create a project in a chosen directory

From your uVim checkout, on macOS/Linux:

```sh
python3 scripts/csharp_env.py --help
python3 scripts/csharp_env.py init ~/projects/MyCSharpApp --name MyCSharpApp --template lsp
cd ~/projects/MyCSharpApp
python3 csharp-env.py run
python3 csharp-env.py edit Program.cs
```

On Windows PowerShell:

```powershell
py -3 scripts/csharp_env.py --help
py -3 scripts/csharp_env.py init "$HOME\projects\MyCSharpApp" --name MyCSharpApp --template lsp
Set-Location "$HOME\projects\MyCSharpApp"
py -3 csharp-env.py run
py -3 csharp-env.py edit Program.cs
```

You can run the original script by absolute path from anywhere. To use the
current directory, omit the target:

```sh
python3 /path/to/uvim/scripts/csharp_env.py init --name MyCSharpApp --template lsp
```

Setup refuses to overwrite an existing environment or generated project
files. An interrupted setup can be resumed with `python3 csharp-env.py
restore` from its target directory.

The `lsp` starter has `Program.cs` and `Greeter.cs`, based on the
[C# LSP example](../examples/lsp/csharp/README.md). It prints:

```text
Hello, Ada!
Hello, Linus!
```

Use `--template hello` (the default) for a minimal Hello World app, or
`--template classlib` for a reusable library. `python3 scripts/csharp_env.py
templates` lists the available starters. Add `--no-restore` to generate the
source, helper and guide offline, then use the generated helper's `restore`
command when you are ready to download the tools.

## Use the Python venv

Setup creates `.venv`, but activation is optional. You can run the generated
helper with your original Python or the venv's Python:

```sh
.venv/bin/python csharp-env.py dotnet build
.venv/bin/python csharp-env.py edit Program.cs
```

PowerShell:

```powershell
& .\.venv\Scripts\python.exe .\csharp-env.py dotnet build
& .\.venv\Scripts\python.exe .\csharp-env.py edit Program.cs
```

If you prefer activation, use `source .venv/bin/activate` on Bash/zsh or
`.\.venv\Scripts\Activate.ps1` on PowerShell, then run `python csharp-env.py
...`. `deactivate` leaves the Python venv. Activating the Python venv alone
does not configure .NET: continue using the helper for .NET and uVim.

## What stays inside the project

| Path | Purpose | Commit it? |
|---|---|---|
| `csharp-env.py` | Portable helper copied into your project | Yes |
| `csharp-env.json` | Exact SDK and `csharp-ls` versions, like a requirements file | Yes |
| `global.json` | Pins .NET SDK selection | Yes |
| `NuGet.Config` | Project package sources (initially nuget.org) | Yes |
| `*.csproj` | C# package references and project settings | Yes |
| `packages.lock.json` | Resolved project package versions/hashes | Yes |
| `CSHARP_ENV.md` | Short usage guide copied into your project | Yes |
| `.venv/` | Python environment; no pip dependencies are installed | No |
| `.csharp-env/sdk/` | Local .NET SDK/runtime | No |
| `.csharp-env/tools/` | Local `csharp-ls` installation | No |
| Other `.csharp-env/` subdirectories | NuGet/CLI/MSBuild caches, bundle extraction, temporary files and installer downloads | No |
| `bin/`, `obj/` | Generated C# build output | No |

The first setup chooses the latest stable .NET 10 SDK and records the exact
version in `csharp-env.json`. `csharp-ls` is pinned to `0.28.0` by default.
Use `init --sdk-version 10.0.100 --lsp-version 0.28.0` to choose exact
versions yourself. All platforms read the same manifest and download their
own native SDK/tool binaries.

The helper sets `DOTNET_ROOT`, `DOTNET_CLI_HOME`, NuGet cache locations and
`PATH` only in its child processes. It does not change shell profiles,
permanent environment variables, system SDKs or global .NET tools. Launch
uVim through `edit` so its language server inherits the local environment.
It also disables automatic ASP.NET development-certificate generation using
[.NET's environment settings](https://learn.microsoft.com/dotnet/core/tools/dotnet-environment-variables#dotnet_generate_aspnet_certificate).
This isolates tool installations and caches; it does not sandbox your C#
program's file or network access.

## Restore after cloning

Copy/commit the helper, manifest, `global.json`, `NuGet.Config`, source,
project file and lock file. There is no dependency on the original uVim
checkout once the project has been created.

From the cloned project, on macOS/Linux:

```sh
python3 csharp-env.py restore --locked
python3 csharp-env.py dotnet build --no-restore
python3 csharp-env.py run -- --no-restore
python3 csharp-env.py edit Program.cs
```

On Windows, replace `python3` with `py -3` or `python`. For a deliberate
dependency update, use `restore` without `--locked` and review/commit the
updated lock file. You can change the exact versions in `csharp-env.json`
and run `restore` to update the SDK or LSP.

To work from another directory, put `-C` before the command:

```sh
python3 /path/to/MyCSharpApp/csharp-env.py -C /path/to/MyCSharpApp dotnet build
```

## Add NuGet dependencies

The C# equivalent of Python dependencies lives in the `.csproj`, with
resolved versions in `packages.lock.json`. For example:

```sh
python3 csharp-env.py dotnet add package Newtonsoft.Json --version 13.0.3
python3 csharp-env.py restore
```

Commit the changed project and lock file. Edit `NuGet.Config` if you need a
different package source; the local SDK/tool restore uses that file too.

## Start without the example

Use `--empty` to prepare tools without copying `Program.cs`, `Greeter.cs`
or a project file. This also works around existing C# sources when there
are no conflicting helper/configuration files:

```sh
python3 /path/to/uvim/scripts/csharp_env.py init ./MyProject --empty
cd MyProject
python3 csharp-env.py dotnet new console --name MyProject --output .
python3 csharp-env.py restore
python3 csharp-env.py edit Program.cs
```

For multiple projects/solutions in one directory, use the `dotnet` command
with an explicit project/solution path, for example `dotnet restore
App/App.csproj`. Keep projects under this environment's directory so they
inherit its `global.json` and `NuGet.Config`.

## Try the editor and language server

```sh
python3 csharp-env.py edit Program.cs
# If uVim is not on PATH:
python3 csharp-env.py edit --uvim /absolute/path/to/uvim Program.cs
```

The helper passes the absolute path to the local `csharp-ls` executable to
uVim. The first solution load can take a while. Try:

1. `:lspinfo` to inspect the C# server status.
2. `gd` on a `Greet` call to open its declaration in `Greeter.cs`.
3. `Space g r` on the `Greet` declaration to find its two callers.
4. Type `greeter.` on a new line in insert mode for completion. Remove the
   temporary line afterwards.
5. Uncomment the `Greet(42)` line, inspect the type error with `Space e`,
   then comment it out again and check that the diagnostic clears.

## Help and cleanup

```sh
python3 csharp-env.py --help
python3 csharp-env.py init --help
python3 csharp-env.py restore --help
python3 csharp-env.py edit --help
```

If a download fails, fix connectivity and run `restore` again; completed
SDK/tool installations are reused. If the SDK reports missing native
libraries, consult Microsoft's platform requirements above.

Close uVim, then delete `.csharp-env` and `.venv` with your file manager to
remove the tools and caches. `bin` and `obj` can also be removed. Keeping
the helper, manifest and project files lets you recreate everything with
`restore`. No global uninstall or shell-profile edits are needed.
