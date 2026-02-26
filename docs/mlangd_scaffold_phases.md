# mlangd Scaffold Plan

This document describes a minimal `mlangd` scaffold and the compiler APIs
required to evolve it into a real language server.

## Current Scaffold Scope

Implemented in `tools/mlang_lsp/mlang_lsp.py`:

1. LSP lifecycle:
- `initialize`
- `initialized`
- `shutdown`
- `exit`

2. Text document sync:
- `textDocument/didOpen`
- `textDocument/didChange` (full-text behavior)
- `textDocument/didSave`
- `textDocument/didClose`

3. Language features:
- `textDocument/hover`
- `textDocument/completion`
- diagnostics via:
  - `textDocument/publishDiagnostics` notifications
  - `textDocument/diagnostic` request

4. Diagnostics quality:
- delimiter matching (`()[]{}`)
- unterminated string literals

This is editor-usable, but not compiler-accurate.

Status (repo scaffold):
- [x] server entrypoint exists at `tools/mlang_lsp/mlang_lsp.py`
- [x] integration test runs against in-repo server
- [x] CI verifies scaffold file presence

## Phased Implementation

## Phase 1: Transport + Session (Done in scaffold)
- JSON-RPC over stdio.
- LSP capability negotiation.
- Open document state and versions.
- Basic UTF-16 position handling.

## Phase 2: Syntax-Only Features (Partially done)
- Hover from lexical token lookup.
- Completion from keywords + in-file symbols.
- Syntax-like diagnostics from lightweight checks.

Remaining for Phase 2:
- Incremental text edits with `range` application.
- Better UTF-16/UTF-8 mapping edge cases.
- Multi-file symbol collection without type analysis.

## Phase 3: Compiler-Backed Semantics (Needs compiler APIs)
- Real parse tree and semantic model per document.
- Name resolution across modules.
- Type inference/checking for hover/completion detail.
- Accurate diagnostics from parser + typer.

## Phase 4: Workspace + Incremental Engine (Needs compiler APIs)
- Dependency graph and import graph updates.
- Incremental parse/typecheck on edits.
- Background indexing and cancellation.
- Stable symbol IDs and location mapping.

## Phase 5: Advanced IDE Features (Needs compiler APIs)
- Go-to definition/declaration.
- References and rename.
- Signature help and semantic tokens.
- Code actions and quick fixes.

## Missing Compiler APIs (Exact Requirements)

To move from scaffold to real `mlangd`, these APIs are needed from the
compiler/runtime side.

1. Parse and diagnostics APIs
- `parse_file(path, text, options) -> ParseResult`
- `ParseResult` must include:
  - AST/root node
  - syntax diagnostics with stable ranges
  - token stream or trivia-aware ranges

2. Incremental document update APIs
- `update_document(file_id, edit_set) -> UpdateResult`
- Incremental reparse with changed ranges, not full recompilation.
- Range mapping utilities between UTF-8 byte offsets, codepoints, UTF-16 units.

3. Semantic analysis APIs
- `analyze_module(module_id) -> SemanticResult`
- Symbol table with:
  - symbol kind
  - declared type
  - declaration/definition ranges
  - doc comments
- Type query:
  - `type_at(file_id, position) -> TypeInfo`

4. Name-resolution/query APIs
- `symbol_at(file_id, position) -> SymbolRef`
- `definition_of(symbol) -> Location`
- `references_of(symbol) -> [Location]`
- `completions_at(file_id, position, context) -> [CompletionItem]`

5. Workspace/index APIs
- `load_workspace(root_paths, config) -> WorkspaceHandle`
- `update_file(path, text, version)`
- `remove_file(path)`
- `reindex(paths)` with progress and cancellation.

6. Concurrency and cancellation APIs
- Safe multi-threaded read/write over compiler state.
- Request cancellation hooks for long-running queries.
- Snapshot model (query against immutable analysis snapshot).

7. Metadata/doc APIs
- Extract doc comments associated with symbols.
- Signature model for callable symbols:
  - parameter names/types/defaults
  - return type

8. Config and build graph APIs
- Resolve imports/modules using project config.
- Honor target/feature flags and conditional compilation modes.

## Maturity Gate (When to Rewrite `mlangd` in mlang)

Implementing `mlangd` in `mlang` is practical when all are true:

1. Compiler APIs above exist and are tested.
2. Incremental analysis is performant for editor latency targets.
3. Runtime has reliable stdio + JSON + file IO + threading primitives.
4. Error recovery in parser/typer is robust under incomplete code.

Until then, keeping a thin Python/C++ LSP front-end over compiler libraries is
usually lower risk and faster to iterate.
