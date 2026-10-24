# Debugger

uVim provides a debugger view for LLDB, GDB, and MLang's mladbg. Build the target with debug information, preferably `-g -O0`, so source locations, local variables, and types are available.

## Launch or attach

From the editor command prompt:

```text
:dbgrun ./build/app
:dbgrun --backend gdb ./build/app -- argument
:dbgattach 1234
:dbgattach --backend mladbg 1234
```

Quote paths and arguments containing spaces. LLDB is the default backend.

Type `:dbgattach --` without pressing Enter to open the process picker. Continue typing to fuzzy-find an executable name or PID. Ctrl+J/K or arrows selects; Enter attaches; Tab inserts the PID without attaching. Ctrl+R refreshes, Ctrl+U clears the filter, and Esc closes the picker. For example, `:dbgattach --backend gdb -- app` filters processes and uses GDB.

## Panes and execution

The Source and Output panes are joined by Variables and Threads when execution stops. The focused pane has an accented glyph border; small terminals show that pane alone.

| Key or command | Action |
| --- | --- |
| `j` / `k`, arrows | Select rows in the focused pane |
| Tab / Shift+Tab | Focus the next / previous pane |
| `v` / `t` | Focus Variables / Threads |
| Enter in Threads | Select the thread and refresh its source and locals |
| `n` / `:n` | Step over |
| `s` / `:s` | Step into |
| `f` / `:f` | Step out |
| `c` / `:c` | Continue |
| Ctrl+C | Interrupt execution |
| `r` / `:run` | Restart the program |
| `:` | Open the debugger command menu |
| `q` or Esc outside a prompt or popup | Close the debugger session |

Closing an attached session detaches and leaves the target alive. Closing a launched session ends that target. Esc first closes an active popup or cancels the prompt.

Attaching often stops inside a library sleep or wait call. uVim selects a readable application caller when possible. The title identifies the inspected frame; `:frame 0` selects the actual stop frame. LLDB/mladbg stepping from an inspected caller first finishes its pending call.

## Select a stack frame

Type `:frame --` to open the current thread's frame picker. Fuzzy-find by frame number, function, source path, or address. Ctrl+J/K or arrows highlights a frame; Enter selects it. Ctrl+R refreshes and Ctrl+U clears the query. Esc returns to the frame prompt. `:frame ID`, `:up`, and `:down` also select frames directly.

Highlighting a frame previews its file and source row before Enter, while preserving the debugger's selected frame and variables. The current backend frame has a `*` marker. Source locations, functions, arguments, frame numbers, and return types use separate syntax colors when available. Frames without readable local source use muted colors and a `[no source]` marker; addresses are dim gray. Return types come from debug symbols, rather than an evaluated return value.

Ctrl+D switches the preview to assembly, including for library frames with no local source. See [Disassembly and instruction help](Debugger-Disassembly.md) for row selection and documentation controls. Tab inserts a frame number while source preview is shown; in assembly preview, Tab shares Ctrl+I's instruction-help toggle.

## Breakpoints and other source files

```text
:b path/to/file.cpp:42
:bps
:delete ID
:disable ID
:enable ID
```

Tab completes breakpoint paths while retaining a row suffix. Enabled breakpoints have red source-row numbers. In Source, `j`/`k` selects a row and `b` toggles its breakpoint. An existing disabled breakpoint is enabled; an existing enabled breakpoint is removed.

`:bps` opens the breakpoint list. Ctrl+J/K or arrows selects; `d` removes the selected breakpoint; Esc or `q` closes the list. Pending and disabled breakpoints are labeled.

Ctrl+P opens the file finder inside the debugger when search tools are included in the build. Filter files, select with Ctrl+J/K, and press Enter to browse one. A suffix such as `helper.hpp:42` selects a source row. Press `b` to set a breakpoint there. Stepping, continuing, or selecting another frame or thread returns to the execution source at the next stop.

## Variable inspection

Select a variable with `j`/`k` in Variables:

| Key | Action |
| --- | --- |
| `p` / Enter | Print the value and storage layout in Output |
| `a` | Disassemble a function-pointer or address value |
| `m` | Show the variable's storage as bytes and ASCII in Output |
| `s` / `i` | Open the size and padding popup |

The size popup reports the whole storage size, member offsets and sizes, and nested layout when debug information supplies it. Scroll with `j`/`k`, Ctrl+J/K, or Page Up/Down; `g`/`G` selects the first/last row. Esc, Enter, or `q` closes it. See [memory and layout inspection](Debugger-Disassembly.md#memory-and-layout) for expression commands and size limitations.

## Test programs

Build fixtures in a new directory under `/tmp` by default:

```sh
python3 scripts/create_debugger_examples.py --language all
python3 scripts/create_debugger_examples.py --language cpp --run
python3 scripts/create_debugger_examples.py --language c --threads 0 --run
python3 scripts/create_debugger_examples.py --language mlang --threads 2 --run
```

The script prints launch and attach commands. Keep `--run` active in one terminal and attach from another. C, C++, and MLang fixtures have two worker threads by default; `--threads 0` disables them. Use `--output-dir /path/to/fixtures` to choose another parent directory. `--backend gdb` or `--backend mladbg` changes the printed commands. Run `--help` for compiler overrides and timing options.

## Build and runtime settings

Debugger support requires macOS or Linux, `UVIM_ENABLE_DEBUGGER=ON`, and `UVIM_ENABLE_AUXILIARY_VIEWS=ON`. Select Debugger in `uvim-config`; `--disable debugger` omits it and `--enable debugger` restores it.

The bridge requires Python 3 and the installed debugger. LLDB needs matching Python bindings; GDB uses its MI interface. `MLADBG_PYTHON` overrides the Python interpreter, `MLADBG_LLDB` selects LLDB for binding discovery, and `UVIM_GDB` overrides the GDB executable. mladbg loads from MLang's installed `share/mlang/debugger/mladbg.py`; a checkout can set `UVIM_MLADBG_MODULE=/path/to/mlang/tools/mladbg/mladbg.py`.

Interactive target stdin is not exposed in the debugger view. Variables show top-level values and summaries. Source, Variables, Threads, and the execution row backgrounds can be customized under `[theme.debugger]`; see the [README](../../README.md#debugger-view).
