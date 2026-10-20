# Your first C# project with uVim

Follow these steps each time you want a new project. Start with Hello World,
then try the two-file LSP example when you want to practice navigation and
diagnostics. The setup keeps the SDK, language server, Python venv and caches
inside your project. It does not install global tools or change your shell
profile.

The commands below use macOS/Linux. A complete Windows PowerShell sequence
is included at the end. The [environment reference](csharp_environment.md)
explains versions, dependencies, restoring clones and cleanup in more detail.

## Ready-made example in ~/test-cs-project-for-uvim

From your uVim checkout, run:

```sh
python3 scripts/create_csharp_example.py --template lsp
cd "$HOME/test-cs-project-for-uvim"
python3 csharp-env.py run
python3 csharp-env.py edit
```

The launcher creates `UvimCSharpExample.csproj`, `Program.cs` and `Greeter.cs`,
downloads the project-local SDK/LSP, builds the project and runs the greeting
example (`Hello, Ada!` and `Hello, Linus!`). It then
prints the commands for opening it in uVim. Read `CSHARP_ENV.md` in the
generated directory for editor exercises. Running the launcher again reuses
the existing environment and preserves edits to the source files.

To upgrade an older example to the latest helper and requirements-file
support, from the uVim checkout:

```sh
python3 scripts/create_csharp_example.py --update-helper
cd "$HOME/test-cs-project-for-uvim"
.venv/bin/python csharp-env.py install -r requirements-csharp.txt
.venv/bin/python csharp-env.py edit Program.cs
```

This updates the generated helper/guide while preserving your C# source
files. All SDK/LSP installations remain inside the example directory.

For a new directory containing a minimal Hello World example instead:

```sh
python3 scripts/create_csharp_example.py "$HOME/test-cs-hello-for-uvim" --template hello
```

Use `--no-restore` to generate files without downloads, or `--help` for
options. For a custom uVim path, from the generated directory:

```sh
python3 csharp-env.py edit --uvim /absolute/path/to/uvim
```

Windows PowerShell, from the uVim checkout:

```powershell
py -3 scripts/create_csharp_example.py --template lsp
Set-Location "$HOME\test-cs-project-for-uvim"
py -3 csharp-env.py run
py -3 csharp-env.py edit
```

## 1. Check the tools you need

You need Python 3.9+, Bash (normally present on macOS/Linux), internet access,
and a uVim build with C# LSP support. The `full` preset and release binaries
include C# support. You do not need to install .NET or `csharp-ls` globally.

Open a terminal in your uVim checkout and check:

```sh
python3 --version
uvim --version
python3 scripts/csharp_env.py --help
python3 scripts/csharp_env.py templates
```

If `uvim` is not on PATH, use its absolute path for the version check and
the `--uvim` option in step 4. If your checkout is elsewhere, run the Python
script by its absolute path.

## 2. Generate a Hello World project

From the uVim checkout:

```sh
python3 scripts/csharp_env.py init ~/projects/HelloUvim --name HelloUvim --template hello
cd ~/projects/HelloUvim
```

Wait for setup to finish. It downloads a local .NET 10 SDK and C# language
server, creates a Python venv, restores packages, and prints `Ready in ...`.
The first download is large; later restores reuse the installed versions.

You now have:

```text
HelloUvim.csproj      Project settings and NuGet dependencies
Program.cs           The Hello World source code
CSHARP_ENV.md         Instructions for this generated project
csharp-env.py        Your copy of the setup/run/editor helper
requirements-csharp.txt  Local SDK/LSP dependency versions (install with install -r)
csharp-env.json      Pinned SDK and language-server versions
global.json          SDK selection
NuGet.Config         Package sources
packages.lock.json   Resolved project packages
.venv/               Local Python environment
.csharp-env/         Local SDK, language server and caches
```

To generate into the current directory instead, start in an empty directory
and run the script by its absolute path, omitting the target:

```sh
python3 /path/to/uvim/scripts/csharp_env.py init --name HelloUvim --template hello
```

The script refuses to overwrite conflicting files. If a download fails,
enter the generated directory and run `python3 csharp-env.py restore` to
resume. To create the files first and download later, add `--no-restore`
to `init`, then run `restore` when you are ready.

## 3. Build and run it

From `~/projects/HelloUvim`:

```sh
python3 csharp-env.py dotnet build
python3 csharp-env.py run
```

The build should succeed. The program prints:

```text
Hello, World!
```

Use the helper for .NET commands so they use this project's SDK and caches.
There is no need to activate the Python venv. If you prefer its Python,
replace `python3` with `.venv/bin/python`.

## 4. Open it in uVim

```sh
python3 csharp-env.py edit
```

This opens `Program.cs` and starts the project's local `csharp-ls`. You can
also name the file explicitly:

```sh
python3 csharp-env.py edit Program.cs
```

If uVim is not on PATH:

```sh
python3 csharp-env.py edit --uvim /absolute/path/to/uvim Program.cs
```

In normal mode, type `:lspinfo` and press Enter to check server status. The
first project load can take a while. Launch through `edit` each time so uVim
inherits the local SDK and language-server environment.

## 5. Edit, save and run your own greeting

In uVim, move to `Hello, World!`, press `i` to enter insert mode, and change
the string to `Hello from uVim!`.

Press Esc, type `:wq`, and press Enter to save and return to the terminal.
Run the project again:

```sh
python3 csharp-env.py run
```

It should now print `Hello from uVim!`. Use `:w` followed by Enter when you
want to save without leaving the editor.

For completion practice, reopen `Program.cs` and type `Console.` on a new
line in insert mode. Look for `WriteLine` in the suggestions. Remove that
temporary line before building again.

## 6. Create the two-file LSP practice project

Use a new terminal in the uVim checkout, or the absolute path to the original
setup script:

```sh
python3 /path/to/uvim/scripts/csharp_env.py init ~/projects/LspPractice --name LspPractice --template lsp
cd ~/projects/LspPractice
python3 csharp-env.py run
python3 csharp-env.py edit Program.cs
```

This template is based on the earlier
[C# example](../examples/lsp/csharp/README.md). It prints `Hello, Ada!` and
`Hello, Linus!` and has `Program.cs` plus `Greeter.cs`.

Try these actions in normal mode unless insert mode is explicitly mentioned:

1. Put the cursor on `Greet` in `Program.cs` and press `gd` to jump to its
   definition in `Greeter.cs`.
2. On the `Greet` declaration, press `Space g r` to find its callers.
3. In `Program.cs`, type `greeter.` on a new line in insert mode and look
   for `Greet` and `Greeting` in completion. Remove the temporary line.
4. Uncomment `Console.WriteLine(greeter.Greet(42));`. Inspect the integer to
   string error with `Space e`, then comment the line out again. The error
   should disappear.

## 7. Choose another starter when you need one

```sh
python3 /path/to/uvim/scripts/csharp_env.py templates
python3 /path/to/uvim/scripts/csharp_env.py init --help
```

| Option | Creates | First command after setup |
|---|---|---|
| `--template hello` | Minimal Hello World console app; the default | `python3 csharp-env.py run` |
| `--template lsp` | Two-file greeting app for LSP practice | `python3 csharp-env.py run` |
| `--template classlib` | Reusable `Greeter` class library | `python3 csharp-env.py dotnet build` |
| `--empty` | Environment and guide without C# source files | Create your own project with the local SDK |

For example:

```sh
python3 /path/to/uvim/scripts/csharp_env.py init ~/projects/MyLibrary --name MyLibrary --template classlib
cd ~/projects/MyLibrary
python3 csharp-env.py dotnet build
python3 csharp-env.py edit
```

A class library produces a DLL and has no application entry point, so use
`dotnet build` rather than `run`. `edit` opens `Greeter.cs` automatically for
this template.

For other .NET templates, first create an empty local environment, then use
the local SDK's template list:

```sh
python3 /path/to/uvim/scripts/csharp_env.py init ~/projects/AnotherApp --empty
cd ~/projects/AnotherApp
python3 csharp-env.py dotnet new list
python3 csharp-env.py dotnet new console --name AnotherApp --output .
python3 csharp-env.py restore
python3 csharp-env.py edit Program.cs
```

## 8. Return to the project tomorrow

Open a terminal in the generated directory:

```sh
cd ~/projects/HelloUvim
python3 csharp-env.py edit
```

The tools are reused. When cloning the project to another computer, first
run `python3 csharp-env.py restore --locked`. Commit the source, helper,
manifest and lock/configuration files, and leave `.venv`, `.csharp-env`,
`bin` and `obj` ignored. See the generated `CSHARP_ENV.md` for that checklist.

## Windows PowerShell: Hello World from start to finish

Run the first commands from your uVim checkout. Python's Windows launcher
is `py -3`; use `python` instead if that is how Python is installed.

```powershell
py -3 --version
uvim --version
py -3 scripts/csharp_env.py templates
py -3 scripts/csharp_env.py init "$HOME\projects\HelloUvim" --name HelloUvim --template hello
Set-Location "$HOME\projects\HelloUvim"
py -3 csharp-env.py dotnet build
py -3 csharp-env.py run
py -3 csharp-env.py edit Program.cs
```

Use `--template lsp` or `--template classlib` with a different target
directory to choose another starter. For a custom editor path:

```powershell
py -3 csharp-env.py edit --uvim "C:\path\to\uvim.exe" Program.cs
```

You do not need to activate the Python venv or change its execution policy.
To use the venv's Python directly:

```powershell
& .\.venv\Scripts\python.exe .\csharp-env.py edit Program.cs
```
