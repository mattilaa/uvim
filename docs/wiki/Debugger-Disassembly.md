# Disassembly and instruction help

[Back to the debugger guide](Debugger.md)

## Preview assembly for a frame

1. Stop or attach to a program and type `:frame --`.
2. Fuzzy-find a frame. Ctrl+J/K or arrows highlights it without selecting it in the debugger.
3. Press Ctrl+D to switch its source preview to disassembly. This also works for library frames without local source.
4. Use Ctrl+Shift+J/K to select instruction rows. With `:set iskitty=false`, Alt+J/K provides the terminal fallback.
5. Press Ctrl+I to toggle instruction documentation for the selected row.

The preview loads a bounded instruction window around the frame's program counter and initially selects that instruction. The execution marker remains at the program counter as you browse; the selected instruction has its own row highlight. The title shows the viewport position. Browsing assembly does not change the selected stack frame, source file, or locals in the debugger.

Instructions, registers, immediates, bytes, and symbols use separate syntax colors; address columns are dim gray. Ctrl+D returns to source preview. Enter chooses the highlighted frame, and Esc closes instruction help before closing the frame picker.

## Instruction-help controls

| Context | Toggle help | Browse instructions | Scroll help |
| --- | --- | --- | --- |
| Frame picker showing assembly | Ctrl+I, also Tab | Ctrl+Shift+J/K; Alt+J/K with `iskitty=false` | Ctrl+J/K |
| Output focused on a disassembly instruction | `i` | `j`/`k` or Ctrl+Shift+J/K; Alt+J/K with `iskitty=false` | Ctrl+J/K |

While the frame prompt accepts a search query, `i` stays a query character. Ctrl+I toggles help without submitting or replacing the query. The terminal key layer gives Tab and Ctrl+I the same key code, so both toggle help in assembly preview. To insert a frame number with Tab instead, return to source preview using Ctrl+D. Outside the frame picker, Tab continues to switch debugger panes.

Esc closes help and preserves the frame picker or Output selection. Use the toggle again to close it as well. While help is open, Ctrl+J/K scrolls documentation instead of moving between frames; close help to resume frame selection with those keys.

The colored popup is shared with the assembly view. It shows the selected instruction, built-in instruction documentation, and optional cached or fetched Compiler Explorer documentation. It updates after a 300 ms pause when the instruction changes, retaining the previous text while browsing. A loading indicator appears only for requests still pending after 300 ms. Changing rows or closing help invalidates old responses.

```text
:set asmdocsapi=false
:set asmdocsapi
:set asmdocsapi?
```

Use `asmdocsapi=false` for built-in help only, `asmdocsapi` to enable API lookup, and `asmdocsapi?` to query the setting. Persist it as `asmdocsapi = false` under `[editor]` in `config.toml` if desired. Built-in help remains the fallback if API retrieval fails. Unsupported instruction rows show an explanatory message. If `UVIM_ENABLE_ASM_DOCS=OFF`, the popup explains that documentation was not compiled into the build. Debugger instruction help does not require the separate assembly browser to be built.

## Disassembly in Output

```text
:disasm
:disasm callback
:disasm 0x12345678
:disasm --count 8 callback
:p --asm callback
```

`:disasm` alone uses the selected frame's program counter. Expressions can identify a function pointer or an address. The default is 32 instructions, with a maximum of 256; `--count N` precedes the expression. Select Output with Tab, select an instruction row with `j`/`k`, and press `i` for help. Headers and summary rows are not instructions.

In Variables, `a` disassembles the selected function-pointer or address value. The result appears in Output; `v` returns to Variables.

## Memory and layout

Inspection requires a stopped process and supports LLDB, GDB, and mladbg:

```text
:p count
:mem &count
:mem pointer
:mem --count 16 &count
:p --memory &count
:layout item
:p --layout item
```

Use `&variable` to read its storage and a pointer value to read the memory it points to. Memory reads default to 64 bytes, with a maximum of 4096. The `m` shortcut in Variables reads the selected variable's own storage.

The Variables `s` shortcut opens a size and padding popup. It includes total storage size, member sizes and offsets, nested layouts, and padding where available. Printing a variable also writes its layout to Output. LLDB/mladbg report alignment when supplied by debug information; GDB uses `ptype /o` for layout and padding.

Nested members include their own sizes and padding, up to four levels and 128 fields. Arrays include element count and stride. Inherited or hidden ABI storage is marked unknown rather than counted as padding.

Disassembly reports the function's code extent when symbols provide it, plus the number of instruction bytes displayed. Code size excludes runtime stack usage and allocations. Function alignment padding is not identified separately.
