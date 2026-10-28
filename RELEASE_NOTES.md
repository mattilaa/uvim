# uVim v0.3.5 (unreleased)

These notes cover changes since the last tagged release, v0.3.3. Version 0.3.4
was a development version and was not tagged as a release.

## Fixed

- Fixed several syntax highlighting errors, improved C++ concept highlighting,
  and removed the obsolete `requires` keyword from Mlang highlighting.
- Fixed indentation around paired delimiters and square brackets. Multiline
  lists, maps, and argument lists now continue at the expected indent, and a
  closing delimiter aligns with its opening line.
- Fixed debugger navigation after attaching to a process or stepping from a
  caller frame. The debugger view no longer flickers during those transitions;
  quick steps also retain stable titles and command hints.
- Fixed Windows test failures caused by line endings and made assembly tests
  portable across Windows paths and line endings. Cleaned up cross-platform
  compiler warnings and the Windows assembly build.
- Fixed the substitution picker so selection marks appear only on chosen
  matches, and stabilized its related tests.

## New and improved

- **Assembly browser:** `:asm` compiles C, C++, or Mlang source, including
  unsaved edits, into a read-only assembly view. It can show linked source and
  assembly side by side, limit the view to selected source lines, use a matching
  `compile_commands.json` entry, and accept compiler flags from a popup.
  Instruction help appears immediately from built-in data while richer
  documentation loads. Assembly rows show source origins, target architecture,
  and clearer highlighting for unrelated or external code.
- **Debugger:** `:dbgrun` and `:dbgattach` provide a terminal debugger view
  using LLDB, GDB, or mladbg where available. Browse threads, stack frames,
  source and disassembly; manage breakpoints from source rows or `:bps`; and
  inspect variable values, memory, disassembly, sizes, and padding. Frame and
  process pickers support fuzzy or regex filtering and source/assembly previews.
  Debugger support can be excluded from smaller builds.
- **Search and replace:** sed-style `:s` commands now support line ranges,
  visual selections, capture groups, and project-wide `:%%s`. Use `c` to
  confirm matches one by one, `p` to select matches from a navigable list, or
  `n` to count matches without changing text. Project changes stay in buffers
  until saved.
- **Fuzzy filtering:** file, buffer, command, help, Git log, and debugger
  pickers share improved matching, including compact out-of-order character
  matches. Use `re:` for regular-expression filtering or `fz:` to force fuzzy
  text. Matching terms receive clearer highlights.
- **Git tools:** `:git status` shows a colored, scrollable overview of staged,
  unstaged, and untracked changes. The file browser also marks staged changes
  with stronger color. `:git rebase --` offers a fuzzy-searchable option popup;
  interactive rebase opens the commit tree at a relevant fixup target when one
  exists. `:git push` asks before publishing a branch without an upstream.
- **Editing:** Ctrl+T reindents the enclosing multiline collection in Normal
  or Insert mode without a formatter. Split-pane commands and visual selection
  colors were refined.

## Development

- Added debugger and assembly example programs, documentation, and regression
  coverage. CI cancels superseded runs on the same branch and has more portable
  Windows tests. Updated the C++ formatting rules and applied them to the code.
