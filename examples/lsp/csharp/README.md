# C# editor and LSP example

A small console project for trying C# highlighting and language-server
features in uVim. It has no external package dependencies.

To use this example as the base of a new project with a project-local SDK,
LSP and Python venv, follow the
[local C# environment guide](../../../docs/csharp_environment.md).
The setup script accepts the current directory or a chosen target directory.
Choose `--template lsp` to generate this two-file example, or follow
[the step-by-step Hello World walkthrough](../../../docs/csharp_quickstart.md)
for a simpler starting point.

Use the .NET 10 SDK and `csharp-ls`, plus a uVim build with C# LSP enabled
(the `full` preset and release binaries include it). See the
[C# setup instructions](../../../README.md#c) for installation.

From the repository root, restore the project and open it:

```sh
dotnet restore examples/lsp/csharp/CSharpExample.csproj
uvim examples/lsp/csharp/Program.cs
```

uVim finds the nearby `.csproj` and starts the language server when opening
`Program.cs`. The first project load can take a while; use `:lspinfo` to
check the server status.

Try these actions:

1. **Highlighting:** inspect comments, strings, types and keywords in both
   `.cs` files.
2. **Definition:** put the cursor on `Greet` in either call in `Program.cs`
   and press `gd` or `Space d`. It should open the method in `Greeter.cs`.
3. **References:** put the cursor on the `Greet` declaration in `Greeter.cs`
   and press `Space g r`. The results should include both calls in
   `Program.cs`.
4. **Completion:** in insert mode on a new line in `Program.cs`, type
   `greeter.`. The completion list should offer `Greet` and `Greeting`.
   Remove the temporary line afterwards.
5. **Diagnostics:** uncomment `Console.WriteLine(greeter.Greet(42));`.
   The server should report that an integer cannot be passed as a string.
   Move to that line and press `Space e` for details. Comment the line out
   again and check that the diagnostic disappears.

Run the unmodified example from the repository root:

```sh
dotnet run --project examples/lsp/csharp/CSharpExample.csproj
```

Expected output:

```text
Hello, Ada!
Hello, Linus!
```
