# Fuzzy filtering and regular expressions

Fuzzy pickers accept explicit regular-expression queries in addition to ordinary fuzzy text. This applies to the file and buffer finders, function signatures, command palette, command and history popups, help search, Git log filtering, and debugger process and frame pickers. Existing fuzzy queries keep their ranking and navigation behavior.

| Query | Meaning |
| --- | --- |
| `mcpp` | Ordinary fuzzy subsequence matching |
| `re:^main\.cpp$` | Regex matching: `main.cpp` |
| `/^main\.cpp$/` | The same regex mode using slash delimiters |
| `re:worker_[0-9]+` | Match a numbered worker name |
| `fz:/tmp/` | Force fuzzy text when a query resembles a delimited regex |
| `fz:re:example` | Search for the literal fuzzy text `re:example` |

For alternation, type `re:^main\.(cpp|h)$` to match either extension.

## Matching and highlighting

Regex filters use case-insensitive ECMAScript syntax: anchors, alternation, groups, character classes, and quantifiers. They search within each candidate; add `^` and `$` to anchor the match. The first matched span supplies character highlights in views that highlight matched characters. Zero-width expressions such as `re:^` match without highlighting any characters.

`re:` enables regex matching immediately as you type. A slash query becomes a regex when it has both delimiters, such as `/foo.*bar/`; an unfinished `/foo` remains ordinary fuzzy text. Use `fz:` to force fuzzy interpretation of reserved prefixes or slash-delimited text. Patterns are compiled once per query and reused across candidates. Invalid expressions produce no matches, and picker error messages explain the invalid regex where available; edit or clear the query to recover.

Regex matching filters candidates instead of using the fuzzy subsequence score. Views that prefer filenames, function symbols, or command aliases still search those fields. A frame expression such as `re:^main$` can therefore match the function symbol even when the displayed row also includes a frame number and source path. In the command palette, expressions match action titles or command aliases.

## File finder and source-row jumps

Open Ctrl+P, then type:

```text
re:\.(cpp|h)$
/^main\.cpp$/:42
```

The second query selects a matching file and opens row 42 when accepted. A trailing `:LINE` remains a row suffix. To match a literal colon and digits in a regex, put them inside slash delimiters, for example `/port:123/`. `re:123` is a regex for `123`, rather than a row jump.

The debugger's Ctrl+P file picker uses the same filtering and row suffixes. In views where `/` opens another search view, such as the buffer browser or Git log, use the `re:` form for filtering. In Help, first open search with `/`, then type the `re:` query.

## Debugger pickers

```text
:dbgattach re:(app|worker)
:dbgattach re:^worker_[0-9]+$
:dbgattach re:^[0-9]{4}$
:frame -- re:^(main|worker)$
```

Typing `:dbgattach re:` immediately opens the process list and updates results
as you type; `--` is optional for explicit regex queries. Use
`:dbgattach --backend gdb re:pattern` to choose a backend. Ctrl+U clears a direct
regex query while preserving `re:`. The older `:dbgattach -- re:pattern` form
still works.

The process picker searches the displayed label and each PID, executable name,
and path independently, so anchors such as `^worker_[0-9]+$` match names and
`^[0-9]{4}$` matches PIDs. Character classes `[A-Z]`, `[a-z]`, and `[0-9]` are
supported. Use `[A-Za-z0-9]` to combine them; commas and spaces inside brackets
are literal characters. Matching is case-insensitive.

 The frame picker searches frame labels and function symbols. Ctrl+J/K and Enter keep their usual selection behavior. Regex text does not run a debugger command until a picker selection is accepted. See the [debugger guide](Debugger.md) for the other picker controls.

## Commands, buffers, functions, and help

In the normal command popup, type `:re:^loc(total)?$`, then use Ctrl+J/K to choose and Tab to insert a matching command. A regex query can contain spaces; Tab still chooses a command rather than completing an argument. The command palette also accepts expressions such as `re:^Open.*file`. The command-history picker uses the same explicit query syntax.

Open the buffer or function browser and type `re:\.cpp$` or `re:(load|save).*data` to filter their searchable text. Help search and Git log filtering accept the same prefixes. Keep their existing navigation keys and clear-query shortcuts.

## File browser

The file browser already supports `/regex` and `?regex` searches within its listing. To filter the listing, enable `filebrowser.fuzzy` and use its command prompt:

```text
:set filebrowser.fuzzy
:filter re:\.(cpp|h)$
:filter /^test_/
```

The parent-directory row remains available. Esc clears the active filter, or run `:filter` without an argument to show the full listing. This command also accepts ordinary fuzzy text. It avoids treating regex characters as file-browser action shortcuts.
