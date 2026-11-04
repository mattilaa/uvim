# Size and memory layout

Use the source inspector for type storage and the debugger inspector for actual
bytes from a stopped program.

| Command | Where to use it | Shows |
| --- | --- | --- |
| `gs` or `:gs` | Normal mode, cursor on a C/C++ or Mlang symbol | Storage size, members, padding, and a byte layout map |
| `:ss expression` | DEBUG view, execution stopped | Live storage layout, member hex previews, raw memory, and a byte layout map |
| `:ss` | DEBUG view, execution stopped, variable selected | The selected variable's live storage |

## Source inspection with `:gs`

Open a C, C++, or `.mla` source file, place the cursor on a variable, type,
member expression, or function name, and press `gs` or enter `:gs`. Scalars such
as integers and floats show their byte count. Structures show total storage
and a table with **Offset**, **Bytes**, and **Member** columns, including nested
members when the compiler supplies their layout.

For example, consider this C++ type:

```cpp
struct Packet {
    char tag;
    int count;
    char flags;
};
```

On a typical target with a four-byte, four-aligned `int`, its 12-byte layout is:

| Offset | Bytes | Member |
| --- | --- | --- |
| 0 | 1 | `tag` |
| 1 | 3 | *(blank dim red padding row)* |
| 4 | 4 | `count` |
| 8 | 1 | `flags` |
| 9 | 3 | *(blank dim red padding row)* |

Actual offsets depend on the target ABI and packing settings. In the popup,
known padding is an empty, dim red band between the corresponding members or
after the last member. Other details use the theme's softer info color.
Ctrl+J/K selects a member or gap and shows its offset, size, and byte map.
A whole-object map for this example is:

```text
## -- -- -- ## ## ## ## ## -- -- --
```

`##` means member storage and `--` means known padding. This map describes
layout; it does not contain runtime byte values. Padding bytes are not
necessarily zero. Inherited or hidden ABI storage that cannot be resolved is
not guessed to be padding.

C/C++ probes use Clang and inherit relevant include paths, defines, language
standard, and target flags from a nearby `compile_commands.json`, including
common build folders. Headers use a nearby translation unit when there is no
exact compile command. Library types such as `std::string` use compiler debug
metadata when their members are unavailable to the source parser.

The inspector includes unsaved edits and compiles a temporary source snapshot
without running the program. It does not require a language server.

## Function return sizes

Put the cursor on the function name in a call or definition and use `gs`.
The popup shows **Return value of ...** with the return type's storage size
and member layout. C/C++ calls use their actual arguments to resolve overloads,
templates, and inferred return types.

| Expression being inspected | Storage shown |
| --- | --- |
| `out.size()` | The returned count type |
| `out.append(...)` | The returned string object's layout |
| A function returning a structure | That structure's members and padding |
| A reference return | The referenced object's storage |
| A pointer return | The pointer's storage |
| A `void` return | A message that there is no return-value storage |

Calls are inspected without executing the program or its side effects. A
function pointer inspected without invoking it shows the pointer's own size;
inspecting its call shows the return type. Mlang calls use compiler return-type
metadata as well. These sizes describe returned objects, not function machine
code, stack usage, or allocations performed by a call.

## Live bytes with `:ss`

1. Build the target with debug information, preferably `-g -O0`.
2. In uVim, run `:dbgrun ./build/app` or attach with `:dbgattach PID`.
3. Inside the DEBUG view, set `:b source.cpp:49` at a line after the variable
   has been initialized, then use `:continue` to reach it. Ctrl+C or
   `:interrupt` pauses a running program.
4. At the stop, run `:ss out` to inspect `out` in the selected stack frame.
   For bare `:ss`, first press `v` to focus Variables and select a row with
   `j`/`k`. The Variables `s` and `i` shortcuts open the same inspector.
5. Use Ctrl+J/K to browse members and their hex previews. Use `j`/`k` to scroll
   through layout, map, and raw-memory rows; close with `q` or Esc.

The popup includes both a byte layout map and actual target-memory hex when
available. `??` in the map means unknown ownership. Hex values are the bytes
observed at that stop, so byte order follows the target. Unreadable or
optimized-out values are reported as unavailable. Padding has unspecified
contents and should not be treated as meaningful member data.

The debugger supports LLDB, GDB, and Mlang's `mladbg`. Choose another backend
with, for example, `:dbgrun --backend gdb ./build/app`. To inspect a variable
from another call frame, select that frame with `:frame --` first.

Other debugger commands include `:layout out` and `:p --layout out` for layout,
`:mem &out` for the object's storage in Output, and `:mem pointer` for the memory
a pointer points to. See the [debugger guide](Debugger.md) and
[memory commands](Debugger-Disassembly.md#memory-and-layout).

## Mlang types

`:gs` supports Mlang scalar and member expressions, imported types,
instantiated generic types, nested structures, and packed bit fields. Bit-field
labels describe bit sizes and offsets; partial-byte ownership is not displayed
as whole-byte padding.

Source inspection uses `mlang -g -O0 -emit-llvm`. Set `UVIM_MLANG_COMPILER` when
starting uVim to select a compiler executable:

```sh
UVIM_MLANG_COMPILER=/path/to/mlang uvim source.mla
```

A temporary `.mla` snapshot is created beside the source to preserve module
resolution, so that directory must be writable. Compilation does not run the
program. Mlang's `storage(...)` total describes the compiler's in-memory object
layout, including collection headers; it can differ from a logical element
payload size. Separately allocated collection data is not included. Similarly,
a C++ string's object layout excludes separately allocated character storage.

For actual Mlang bytes, build the target with `-g -O0`, launch or attach through
LLDB or `mladbg`, stop execution, and use `:ss` in the debugger view.

## Popup controls and limits

Both inspector popups keep the same viewport while browsing members, bounded
by the terminal up to 100 columns and 26 rows. Long names and descriptions wrap;
terminal resizing recalculates the viewport.

| Action | Source popup | Debugger popup |
| --- | --- | --- |
| Browse members | Ctrl+J/K, including padding ranges | Ctrl+J/K, jumping to member hex previews when available |
| Scroll wrapped rows | `j` / `k` | `j` / `k`, arrows, Page Up/Down |
| First / last row | Scroll with `j` / `k` | `g` / `G` |
| Close | `q` or `gs` | `q`, Esc, or Enter |

Source maps show at most 128 bytes. Compiler metadata layouts are bounded to
eight nested levels and 256 members. Debugger nested layouts are bounded to
four levels and 128 fields; raw memory is capped at 4096 bytes, each member hex
preview at 32 bytes, and the byte map at 256 bytes. A truncated display does
not change the reported total object size.

Source inspection requires `UVIM_ENABLE_STRUCT_SIZE_POPUP=ON` (the
`struct-size` feature in `uvim-config`). Live inspection requires debugger
support; see [debugger build and runtime settings](Debugger.md#build-and-runtime-settings).

## Troubleshooting

- **`ss: pause the debugger and use :ss [expression]`:** this is the normal
  editor prompt. Enter the DEBUG view with `:dbgrun` or `:dbgattach`, stop the
  target, and run `:ss expression` there. For source-only sizes, use `:gs`.
- **Pause execution before inspecting live storage:** interrupt the running
  target or wait for a breakpoint before using `:ss`.
- **No selected variable for bare `:ss`:** press `v` and select a variable,
  or specify it directly, for example `:ss out`.
- **Variable unavailable or undeclared:** select the frame containing it and
  stop within its scope after initialization. Build with debug information
  and low optimization so values and type information remain available.
- **`gs: clang could not resolve size ...`:** read the compiler diagnostic in
  the status message. Check that the buffer compiles and that a nearby
  `compile_commands.json` supplies its include paths, defines, and language
  standard. For Mlang, check the compiler executable and source-directory
  permissions.
