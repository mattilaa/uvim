# Search and replace

Use sed-style substitution expressions in the `:` prompt:

| Command | Scope |
| --- | --- |
| `:s/old/new/` | First match on the current line |
| `:s/old/new/g` | Every match on the current line |
| `:%s/old/new/g` | Every match in the current buffer |
| `:2,10s/old/new/g` | Lines 2 through 10, inclusive |
| `:.,$s/old/new/g` | Current line through the end of the buffer |
| `:.-1,.+1s/old/new/g` | Current line and its neighbors |
| `:'<,'>s/old/new/g` | Lines covered by the last visual selection |
| `:%%s/old/new/g` | Text files under the project root |

`:substitute` is the long spelling of `:s`. Use another punctuation delimiter
when convenient, for example `:%s#/old/path#/new/path#g`. Escape a delimiter
inside either argument, such as `:s/old\/path/new\/path/`.

## Patterns and replacement text

Patterns use case-sensitive ECMAScript regular expressions and match each line
independently. For example:

```text
:%s/(name)=([0-9]+)/\1: \2/g
:%s/word/[&]/g
:%s/^/prefix: /
```

In replacements, `&` inserts the whole match and `\1` through `\9` insert
captured groups. `\&` inserts a literal ampersand, `\\` a backslash, `\t` a tab,
and `\n` or `\r` a new line. An empty pattern reuses the current buffer's last
search pattern, or the editor's last search when the buffer has none.

Both closing delimiters are required. Invalid regexes, invalid ranges, and
unsupported flags report an error without changing text. The command popup and
Tab completion leave substitution expressions intact while you type.

## Flags

| Flag | Effect |
| --- | --- |
| `g` | Replace all matches on each line instead of just the first |
| `c` | Ask before each replacement |
| `p` | Pick replacements from a selectable match list |
| `i` | Ignore case |
| `I` | Use case-sensitive matching; the last `i` or `I` wins |
| `n` | Count matches without modifying text |
| `e` | Treat a pattern with no matches as a successful no-op |

Combine flags, for example `:%s/old/new/gi` or `:%%s/old/new/gin`.
The status line reports match/replacement counts and affected lines or files.

## Confirming replacements

Add `c` to review each occurrence in a colored popup before replacing it:

```text
:%s/old/new/gc
:%%s/old/new/gc
```

The popup shows the file, line and column, source context, matched text, and
expanded replacement, including captures. Choose:

| Key | Action |
| --- | --- |
| `y` | Replace this occurrence |
| `n` | Skip this occurrence |
| `a` | Replace this and all remaining occurrences, across files |
| `l` | Replace this occurrence and stop |
| `q`, Esc, Ctrl+C | Stop without replacing this occurrence |

Stopping preserves earlier approvals and leaves remaining text unchanged.
Each changed buffer still gets one undo step. Rejecting every occurrence
creates no undo entries and does not open project buffers. Without `g`, only
the first match per line is considered. Combining `n` with `c` only counts
matches and does not ask questions. Commands without `c` or `p` replace immediately.

## Selecting matches from a list

Use `p` to select several occurrences and apply them together:

```text
:%s/old/new/gp
:%%s/old/new/gp
```

The popup lists each match with its file, line, column, and source excerpt.
The focused row shows the expanded replacement; wide terminals also show a
preview pane. Line numbers, columns, directories, filenames, and matched text
use distinct colors. The inspection pane uses the same colors and shows the
replacement in green. Initially no matches are selected.

| Key | Action |
| --- | --- |
| `j` / `k`, arrows | Move between matches |
| Ctrl+D / Ctrl+U | Move down / up half a page |
| Space | Toggle the focused match |
| `r` | Reset by clearing all selections |
| Enter | Apply all selected replacements at once |
| Esc, Ctrl+C, `q` | Cancel and discard the selections |
| Page Up / Page Down, Home / End | Browse longer lists |

Selected rows have a green background; the focused selected row uses a brighter
green. A scrollbar on the popup's right edge marks the visible part of long
lists. Selecting matches does not change text. Enter creates one undo step per
changed buffer. Project files remain unsaved until `:w` or `:wa`, and files with
no selected changes are not opened. Confirming an empty selection changes
nothing. Cancelling leaves all text untouched.

`p` takes precedence over `c` when combined. `n` takes precedence over both
interactive modes and only counts matches. Without `g`, the list contains only
the first match on each line. Ranges and capture replacements work as usual.

## Project-wide replacement

`:%%s` uses the editor's project root, falling back to the editor's working
directory when no project root is set. It traverses subdirectories, honors
`.gitignore` when enabled, and skips hidden paths, symlinks, and binary files.
Open buffers supply their current contents, including unsaved changes; new
named buffers under the root also participate.

```text
:%%s/old_name/new_name/gn
:%%s/old_name/new_name/g
:wa
```

The first command counts matches without opening or changing buffers. The
second stages replacements in modified buffers, opening only files that need
changes. Files on disk stay unchanged until you save. Review the modified
buffers and use `:wa` to save all modified buffers, or `:w` to save individually.
The current buffer remains selected.

Each changed buffer gets one undo step. `u` undoes the replacement in that
buffer, and Ctrl+R redoes it; project changes can be undone separately in each
file. Identical replacements and count-only commands create no undo entries.
