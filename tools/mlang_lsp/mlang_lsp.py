#!/usr/bin/env python3
from __future__ import annotations

import json
import hashlib
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

TOOLS_DIR = Path(__file__).resolve().parents[1]
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from mlang_frontend.compiler_api import SemanticResult, analyze_document
from mlang_frontend.source_map import line_char_to_offset, offset_to_line_char


@dataclass
class Document:
    uri: str
    text: str
    version: int
    language_id: str


class JsonRpcServer:
    TOKEN_TYPES = [
        "keyword",
        "function",
        "variable",
        "number",
        "string",
        "comment",
    ]

    def __init__(self) -> None:
        self._documents: dict[str, Document] = {}
        self._semantic_cache: dict[str, tuple[int, str, SemanticResult]] = {}
        self._dependency_graph: dict[str, list[str]] = {}
        self._canceled_request_ids: set[int] = set()
        self._running = True
        self._shutdown_requested = False

    def _read_message(self) -> dict[str, Any] | None:
        content_length = None
        while True:
            line = sys.stdin.buffer.readline()
            if not line:
                return None
            if line in (b"\r\n", b"\n"):
                break
            header = line.decode("ascii", errors="replace").strip()
            if header.lower().startswith("content-length:"):
                content_length = int(header.split(":", 1)[1].strip())
        if content_length is None:
            return None
        body = sys.stdin.buffer.read(content_length)
        if not body:
            return None
        return json.loads(body.decode("utf-8"))

    def _write(self, payload: dict[str, Any]) -> None:
        raw = json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode(
            "utf-8"
        )
        header = f"Content-Length: {len(raw)}\r\n\r\n".encode("ascii")
        sys.stdout.buffer.write(header)
        sys.stdout.buffer.write(raw)
        sys.stdout.buffer.flush()

    def _respond(self, req_id: Any, result: Any) -> None:
        self._write({"jsonrpc": "2.0", "id": req_id, "result": result})

    def _error(self, req_id: Any, code: int, message: str) -> None:
        self._write(
            {
                "jsonrpc": "2.0",
                "id": req_id,
                "error": {"code": code, "message": message},
            }
        )

    def _notify(self, method: str, params: dict[str, Any]) -> None:
        self._write({"jsonrpc": "2.0", "method": method, "params": params})

    def _publish_diagnostics(self, uri: str) -> None:
        doc = self._documents.get(uri)
        if doc is None:
            self._notify(
                "textDocument/publishDiagnostics", {"uri": uri, "diagnostics": []}
            )
            return

        diagnostics = [d.to_lsp(doc.text) for d in self._analyze(doc).diagnostics]
        self._notify(
            "textDocument/publishDiagnostics",
            {"uri": uri, "diagnostics": diagnostics, "version": doc.version},
        )

    def _doc_hash(self, text: str) -> str:
        return hashlib.sha1(text.encode("utf-8")).hexdigest()

    def _analyze(self, doc: Document) -> SemanticResult:
        cached = self._semantic_cache.get(doc.uri)
        text_hash = self._doc_hash(doc.text)
        if cached and cached[0] == doc.version and cached[1] == text_hash:
            return cached[2]
        result = analyze_document(doc.text)
        self._semantic_cache[doc.uri] = (doc.version, text_hash, result)
        return result

    def _invalidate_doc(self, uri: str) -> None:
        self._semantic_cache.pop(uri, None)
        self._dependency_graph.pop(uri, None)

    def _update_dependency_graph(self, uri: str) -> None:
        doc = self._documents.get(uri)
        if doc is None:
            self._dependency_graph.pop(uri, None)
            return
        self._dependency_graph[uri] = self._analyze(doc).imports

    @staticmethod
    def _get_line(text: str, line: int) -> str:
        if line < 0:
            return ""
        lines = text.splitlines()
        if line >= len(lines):
            return ""
        return lines[line]

    @staticmethod
    def _word_at(text: str, offset: int) -> str:
        if not text:
            return ""
        offset = max(0, min(offset, len(text)))
        start = offset
        end = offset
        while start > 0 and (text[start - 1].isalnum() or text[start - 1] == "_"):
            start -= 1
        while end < len(text) and (text[end].isalnum() or text[end] == "_"):
            end += 1
        return text[start:end]

    @staticmethod
    def _keyword_items() -> list[dict[str, Any]]:
        keywords = [
            "fn",
            "let",
            "mut",
            "return",
            "if",
            "else",
            "while",
            "for",
            "true",
            "false",
        ]
        return [{"label": k, "kind": 14, "detail": "keyword"} for k in keywords]

    def _workspace_symbols(self) -> list[str]:
        seen: set[str] = set()
        symbols: list[str] = []
        for doc in self._documents.values():
            for symbol in self._analyze(doc).symbols:
                name = symbol.name
                if name in seen:
                    continue
                seen.add(name)
                symbols.append(name)
        return symbols

    def _work_begin(self, token: Any, title: str) -> None:
        if token is None:
            return
        self._notify(
            "$/progress",
            {"token": token, "value": {"kind": "begin", "title": title}},
        )

    def _work_end(self, token: Any, message: str = "done") -> None:
        if token is None:
            return
        self._notify(
            "$/progress",
            {"token": token, "value": {"kind": "end", "message": message}},
        )

    def _is_canceled(self, req_id: Any) -> bool:
        return isinstance(req_id, int) and req_id in self._canceled_request_ids

    def _handle_workspace_diagnostic(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return

        token = params.get("workDoneToken")
        self._work_begin(token, "workspace diagnostics")
        items: list[dict[str, Any]] = []
        for uri, doc in self._documents.items():
            if self._is_canceled(req_id):
                self._work_end(token, "cancelled")
                self._error(req_id, -32800, "Request cancelled")
                return
            result = self._analyze(doc)
            self._dependency_graph[uri] = result.imports
            items.append(
                {
                    "uri": uri,
                    "version": doc.version,
                    "kind": "full",
                    "items": [d.to_lsp(doc.text) for d in result.diagnostics],
                }
            )
        self._work_end(token)
        self._respond(req_id, {"items": items})

    def _handle_initialize(self, req_id: Any) -> None:
        self._respond(
            req_id,
            {
                "serverInfo": {"name": "mlang-lsp-scaffold", "version": "0.1.0"},
                "capabilities": {
                    "textDocumentSync": 2,
                    "hoverProvider": True,
                    "definitionProvider": True,
                    "declarationProvider": True,
                    "referencesProvider": True,
                    "documentHighlightProvider": True,
                    "foldingRangeProvider": True,
                    "callHierarchyProvider": True,
                    "documentLinkProvider": {"resolveProvider": False},
                    "renameProvider": {"prepareProvider": True},
                    "documentSymbolProvider": True,
                    "workspaceSymbolProvider": True,
                    "selectionRangeProvider": True,
                    "linkedEditingRangeProvider": True,
                    "inlayHintProvider": True,
                    "documentFormattingProvider": True,
                    "codeLensProvider": {"resolveProvider": False},
                    "codeActionProvider": True,
                    "signatureHelpProvider": {"triggerCharacters": ["(", ","]},
                    "semanticTokensProvider": {
                        "legend": {"tokenTypes": self.TOKEN_TYPES, "tokenModifiers": []},
                        "full": True,
                        "range": True,
                    },
                    "completionProvider": {
                        "resolveProvider": True,
                        "triggerCharacters": [".", ":"],
                    },
                    "workDoneProgress": True,
                    "diagnosticProvider": {
                        "interFileDependencies": False,
                        "workspaceDiagnostics": True,
                    },
                },
            },
        )

    @staticmethod
    def _location_for_range(uri: str, text: str, start: int, end: int) -> dict[str, Any]:
        sl, sc = offset_to_line_char(text, start)
        el, ec = offset_to_line_char(text, end)
        return {
            "uri": uri,
            "range": {
                "start": {"line": sl, "character": sc},
                "end": {"line": el, "character": ec},
            },
        }

    @staticmethod
    def _token_at(text: str, line: int, character: int) -> tuple[str, int, int]:
        offset = line_char_to_offset(text, line, character)
        if not text:
            return "", 0, 0
        offset = max(0, min(offset, len(text)))
        start = offset
        end = offset
        while start > 0 and (text[start - 1].isalnum() or text[start - 1] == "_"):
            start -= 1
        while end < len(text) and (text[end].isalnum() or text[end] == "_"):
            end += 1
        return text[start:end], start, end

    def _find_definition(self, name: str) -> dict[str, Any] | None:
        if not name:
            return None
        pat = re.compile(rf"\b(fn|let|const)\s+({re.escape(name)})\b")
        for uri, doc in self._documents.items():
            m = pat.search(doc.text)
            if m:
                start = m.start(2)
                end = m.end(2)
                return self._location_for_range(uri, doc.text, start, end)
        return None

    def _find_references(self, name: str) -> list[dict[str, Any]]:
        if not name:
            return []
        out: list[dict[str, Any]] = []
        pat = re.compile(rf"\b{re.escape(name)}\b")
        for uri, doc in self._documents.items():
            for m in pat.finditer(doc.text):
                out.append(self._location_for_range(uri, doc.text, m.start(), m.end()))
        return out

    def _find_function_declaration(self, name: str) -> tuple[str, Document, re.Match[str]] | None:
        if not name:
            return None
        pat = re.compile(rf"\bfn\s+({re.escape(name)})\s*\(")
        for uri, doc in self._documents.items():
            m = pat.search(doc.text)
            if m:
                return (uri, doc, m)
        return None

    @staticmethod
    def _extract_function_body(text: str, fn_name: str) -> tuple[int, int] | None:
        m = re.search(rf"\bfn\s+{re.escape(fn_name)}\s*\([^)]*\)\s*\{{", text)
        if not m:
            return None
        start = m.end() - 1  # at '{'
        depth = 0
        i = start
        while i < len(text):
            ch = text[i]
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return (start + 1, i)
            i += 1
        return None

    @staticmethod
    def _call_hierarchy_item(
        uri: str, text: str, name: str, start: int, end: int
    ) -> dict[str, Any]:
        rng = JsonRpcServer._location_for_range(uri, text, start, end)["range"]
        return {
            "name": name,
            "kind": 12,  # function
            "uri": uri,
            "range": rng,
            "selectionRange": rng,
        }

    @staticmethod
    def _declared_symbols(text: str) -> list[tuple[str, str, int, int]]:
        out: list[tuple[str, str, int, int]] = []
        for m in re.finditer(r"\bfn\s+([A-Za-z_][A-Za-z0-9_]*)\b", text):
            out.append((m.group(1), "Function", m.start(1), m.end(1)))
        for m in re.finditer(r"\b(let|const)\s+([A-Za-z_][A-Za-z0-9_]*)\b", text):
            kind = "Constant" if m.group(1) == "const" else "Variable"
            out.append((m.group(2), kind, m.start(2), m.end(2)))
        return out

    def _handle_definition(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, None)
            return
        pos = params.get("position", {})
        token, _, _ = self._token_at(
            doc.text, int(pos.get("line", 0)), int(pos.get("character", 0))
        )
        self._respond(req_id, self._find_definition(token))

    def _handle_declaration(self, req_id: Any, params: dict[str, Any]) -> None:
        self._handle_definition(req_id, params)

    def _handle_references(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, [])
            return
        pos = params.get("position", {})
        token, _, _ = self._token_at(
            doc.text, int(pos.get("line", 0)), int(pos.get("character", 0))
        )
        refs = self._find_references(token)
        include_decl = bool(params.get("context", {}).get("includeDeclaration", False))
        if not include_decl:
            decl = self._find_definition(token)
            if decl is not None:
                refs = [
                    r
                    for r in refs
                    if not (
                        r.get("uri") == decl.get("uri")
                        and r.get("range", {}).get("start") == decl.get("range", {}).get("start")
                        and r.get("range", {}).get("end") == decl.get("range", {}).get("end")
                    )
                ]
        self._respond(req_id, refs)

    def _handle_prepare_rename(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, None)
            return
        pos = params.get("position", {})
        token, start, end = self._token_at(
            doc.text, int(pos.get("line", 0)), int(pos.get("character", 0))
        )
        if not token:
            self._respond(req_id, None)
            return
        if self._find_definition(token) is None:
            self._respond(req_id, None)
            return
        self._respond(req_id, self._location_for_range(uri, doc.text, start, end)["range"])

    def _handle_rename(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, {"changes": {}})
            return
        new_name = params.get("newName", "")
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", new_name):
            self._error(req_id, -32602, "Invalid newName")
            return
        pos = params.get("position", {})
        token, _, _ = self._token_at(
            doc.text, int(pos.get("line", 0)), int(pos.get("character", 0))
        )
        if self._find_definition(token) is None:
            self._error(req_id, -32602, "Symbol is not renameable")
            return
        refs = self._find_references(token)
        changes: dict[str, list[dict[str, Any]]] = {}
        for loc in refs:
            changes.setdefault(loc["uri"], []).append(
                {"range": loc["range"], "newText": new_name}
            )
        self._respond(req_id, {"changes": changes})

    def _handle_signature_help(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, None)
            return
        pos = params.get("position", {})
        line = int(pos.get("line", 0))
        char = int(pos.get("character", 0))
        line_text = self._get_line(doc.text, line)
        prefix = line_text[: min(char, len(line_text))]
        m = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*\([^()]*$", prefix)
        if not m:
            self._respond(req_id, None)
            return
        fn_name = m.group(1)
        signature = f"{fn_name}(...)"
        for open_doc in self._documents.values():
            dm = re.search(
                rf"\bfn\s+{re.escape(fn_name)}\s*\(([^)]*)\)", open_doc.text
            )
            if dm:
                params_src = dm.group(1).strip()
                signature = f"{fn_name}({params_src})"
                break
        comma_count = prefix.rsplit("(", 1)[-1].count(",")
        self._respond(
            req_id,
            {
                "signatures": [{"label": signature}],
                "activeSignature": 0,
                "activeParameter": comma_count,
            },
        )

    def _encode_semantic_tokens(
        self, text: str, start_line: int | None = None, end_line: int | None = None
    ) -> list[int]:
        token_map = {name: idx for idx, name in enumerate(self.TOKEN_TYPES)}
        rows: list[tuple[int, int, int, int, int]] = []
        for line_idx, line in enumerate(text.splitlines()):
            if start_line is not None and line_idx < start_line:
                continue
            if end_line is not None and line_idx > end_line:
                continue
            for m in re.finditer(r"\b(fn|let|const|return|if|else|while|for|import)\b", line):
                rows.append((line_idx, m.start(), len(m.group(1)), token_map["keyword"], 0))
            for m in re.finditer(r"\bfn\s+([A-Za-z_][A-Za-z0-9_]*)", line):
                rows.append((line_idx, m.start(1), len(m.group(1)), token_map["function"], 0))
            for m in re.finditer(r"\b(let|const)\s+([A-Za-z_][A-Za-z0-9_]*)", line):
                rows.append((line_idx, m.start(2), len(m.group(2)), token_map["variable"], 0))
            for m in re.finditer(r"\b[0-9]+\b", line):
                rows.append((line_idx, m.start(), len(m.group(0)), token_map["number"], 0))
            for m in re.finditer(r'"(?:\\.|[^"\\])*"', line):
                rows.append((line_idx, m.start(), len(m.group(0)), token_map["string"], 0))
            c = line.find("//")
            if c >= 0:
                rows.append((line_idx, c, len(line) - c, token_map["comment"], 0))
        rows.sort()
        out: list[int] = []
        prev_line = 0
        prev_char = 0
        for line, char, length, tok_type, mods in rows:
            dline = line - prev_line
            dchar = char - prev_char if dline == 0 else char
            out.extend([dline, dchar, length, tok_type, mods])
            prev_line = line
            prev_char = char
        return out

    def _handle_semantic_tokens_full(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, {"data": []})
            return
        self._respond(req_id, {"data": self._encode_semantic_tokens(doc.text)})

    def _handle_semantic_tokens_range(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, {"data": []})
            return
        rng = params.get("range", {})
        start = rng.get("start", {})
        end = rng.get("end", {})
        start_line = int(start.get("line", 0))
        end_line = int(end.get("line", start_line))
        self._respond(
            req_id,
            {"data": self._encode_semantic_tokens(doc.text, start_line=start_line, end_line=end_line)},
        )

    def _handle_code_action(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, [])
            return
        diags = params.get("context", {}).get("diagnostics", [])
        known_symbols: set[str] = set()
        for open_doc in self._documents.values():
            for sym in self._analyze(open_doc).symbols:
                known_symbols.add(sym.name)

        actions: list[dict[str, Any]] = []
        for d in diags:
            msg = d.get("message", "")
            m = re.match(r"Unknown identifier '([A-Za-z_][A-Za-z0-9_]*)'", msg)
            if not m:
                continue
            missing = m.group(1)
            replacement = next((s for s in sorted(known_symbols) if s[:1] == missing[:1]), None)
            if not replacement:
                continue
            actions.append(
                {
                    "title": f"Replace '{missing}' with '{replacement}'",
                    "kind": "quickfix",
                    "edit": {
                        "changes": {
                            uri: [
                                {
                                    "range": d.get("range"),
                                    "newText": replacement,
                                }
                            ]
                        }
                    },
                }
            )
        self._respond(req_id, actions)

    def _handle_inlay_hint(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, [])
            return
        semantic = self._analyze(doc)
        type_by_name = {s.name: s.type_name for s in semantic.symbols}
        hints: list[dict[str, Any]] = []
        for m in re.finditer(
            r"\b(let|const)\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^;\n]+)\s*;",
            doc.text,
        ):
            name = m.group(2)
            inferred = type_by_name.get(name, "Unknown")
            if inferred == "Unknown":
                continue
            line, char = offset_to_line_char(doc.text, m.end(2))
            hints.append(
                {
                    "position": {"line": line, "character": char},
                    "label": f": {inferred}",
                    "kind": 1,
                    "paddingLeft": True,
                    "paddingRight": False,
                }
            )
        self._respond(req_id, hints)

    @staticmethod
    def _format_text(text: str) -> str:
        lines = text.splitlines()
        out: list[str] = []
        indent = 0
        for raw in lines:
            stripped = raw.strip()
            if not stripped:
                out.append("")
                continue
            if stripped.startswith("}"):
                indent = max(0, indent - 1)
            line = ("  " * indent) + stripped
            # Add semicolon for simple let/const/return declarations.
            if re.match(r"^(let|const|return)\b", stripped):
                if not line.endswith(";") and not line.endswith("{") and not line.endswith("}"):
                    line += ";"
            out.append(line.rstrip())
            if stripped.endswith("{"):
                indent += 1
        formatted = "\n".join(out)
        if text.endswith("\n"):
            formatted += "\n"
        return formatted

    def _handle_formatting(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, [])
            return
        new_text = self._format_text(doc.text)
        if new_text == doc.text:
            self._respond(req_id, [])
            return
        end_line, end_char = offset_to_line_char(doc.text, len(doc.text))
        self._respond(
            req_id,
            [
                {
                    "range": {
                        "start": {"line": 0, "character": 0},
                        "end": {"line": end_line, "character": end_char},
                    },
                    "newText": new_text,
                }
            ],
        )

    def _handle_code_lens(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, [])
            return
        items: list[dict[str, Any]] = []
        for m in re.finditer(r"\bfn\s+([A-Za-z_][A-Za-z0-9_]*)\b", doc.text):
            fn_name = m.group(1)
            rng = self._location_for_range(uri, doc.text, m.start(1), m.end(1))["range"]
            items.append(
                {
                    "range": rng,
                    "command": {
                        "title": f"Run {fn_name}",
                        "command": "mlang.runFunction",
                        "arguments": [uri, fn_name],
                    },
                }
            )
        self._respond(req_id, items)

    def _handle_selection_range(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, [])
            return

        lines = doc.text.splitlines()
        end_line, end_char = offset_to_line_char(doc.text, len(doc.text))
        doc_range = {
            "start": {"line": 0, "character": 0},
            "end": {"line": end_line, "character": end_char},
        }

        out: list[dict[str, Any]] = []
        for pos in params.get("positions", []):
            line = int(pos.get("line", 0))
            char = int(pos.get("character", 0))
            token, start_off, end_off = self._token_at(doc.text, line, char)

            if token:
                token_range = self._location_for_range(uri, doc.text, start_off, end_off)[
                    "range"
                ]
            else:
                token_range = {
                    "start": {"line": line, "character": char},
                    "end": {"line": line, "character": char},
                }

            if 0 <= line < len(lines):
                line_len = len(lines[line])
            else:
                line_len = 0
            line_range = {
                "start": {"line": max(0, line), "character": 0},
                "end": {"line": max(0, line), "character": line_len},
            }

            out.append(
                {
                    "range": token_range,
                    "parent": {
                        "range": line_range,
                        "parent": {"range": doc_range},
                    },
                }
            )

        self._respond(req_id, out)

    def _handle_linked_editing_range(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, None)
            return
        pos = params.get("position", {})
        token, _, _ = self._token_at(
            doc.text, int(pos.get("line", 0)), int(pos.get("character", 0))
        )
        if not token or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", token):
            self._respond(req_id, None)
            return

        ranges: list[dict[str, Any]] = []
        for m in re.finditer(rf"\b{re.escape(token)}\b", doc.text):
            ranges.append(self._location_for_range(uri, doc.text, m.start(), m.end())["range"])
            if len(ranges) >= 32:
                break
        if len(ranges) <= 1:
            self._respond(req_id, None)
            return
        self._respond(req_id, {"ranges": ranges, "wordPattern": r"[A-Za-z_][A-Za-z0-9_]*"})

    def _handle_document_highlight(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, [])
            return
        pos = params.get("position", {})
        token, _, _ = self._token_at(
            doc.text, int(pos.get("line", 0)), int(pos.get("character", 0))
        )
        if not token:
            self._respond(req_id, [])
            return
        decl = self._find_definition(token)
        out: list[dict[str, Any]] = []
        for m in re.finditer(rf"\b{re.escape(token)}\b", doc.text):
            rng = self._location_for_range(uri, doc.text, m.start(), m.end())["range"]
            kind = 2  # read
            if decl and decl.get("uri") == uri and decl.get("range") == rng:
                kind = 3  # write
            out.append({"range": rng, "kind": kind})
            if len(out) >= 64:
                break
        self._respond(req_id, out)

    def _handle_folding_range(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, [])
            return
        lines = doc.text.splitlines()
        out: list[dict[str, Any]] = []

        # Brace-based folding for code blocks.
        stack: list[int] = []
        for i, line in enumerate(lines):
            for ch in line:
                if ch == "{":
                    stack.append(i)
                elif ch == "}" and stack:
                    start = stack.pop()
                    end = i
                    if end > start:
                        out.append(
                            {
                                "startLine": start,
                                "endLine": end,
                                "kind": "region",
                            }
                        )

        # Consecutive line-comment blocks folding.
        i = 0
        while i < len(lines):
            if lines[i].lstrip().startswith("//"):
                start = i
                while i + 1 < len(lines) and lines[i + 1].lstrip().startswith("//"):
                    i += 1
                end = i
                if end > start:
                    out.append(
                        {
                            "startLine": start,
                            "endLine": end,
                            "kind": "comment",
                        }
                    )
            i += 1

        self._respond(req_id, out)

    def _handle_document_link(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, [])
            return

        out: list[dict[str, Any]] = []
        for m in re.finditer(r"\bimport\s+([A-Za-z_][A-Za-z0-9_\.]*)\s*;", doc.text):
            module_name = m.group(1)
            start = m.start(1)
            end = m.end(1)
            rng = self._location_for_range(uri, doc.text, start, end)["range"]
            target = "mlang:///modules/" + module_name.replace(".", "/") + ".mla"
            out.append(
                {
                    "range": rng,
                    "target": target,
                    "tooltip": f"Open module {module_name}",
                }
            )
        self._respond(req_id, out)

    def _handle_prepare_call_hierarchy(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, [])
            return
        pos = params.get("position", {})
        token, _, _ = self._token_at(
            doc.text, int(pos.get("line", 0)), int(pos.get("character", 0))
        )
        found = self._find_function_declaration(token)
        if not found:
            self._respond(req_id, [])
            return
        f_uri, f_doc, match = found
        item = self._call_hierarchy_item(f_uri, f_doc.text, token, match.start(1), match.end(1))
        self._respond(req_id, [item])

    def _handle_outgoing_calls(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        item = params.get("item", {})
        uri = item.get("uri", "")
        fn_name = item.get("name", "")
        doc = self._documents.get(uri)
        if doc is None or not fn_name:
            self._respond(req_id, [])
            return
        body = self._extract_function_body(doc.text, fn_name)
        if not body:
            self._respond(req_id, [])
            return
        body_start, body_end = body
        body_text = doc.text[body_start:body_end]
        out: list[dict[str, Any]] = []
        seen: set[str] = set()
        for m in re.finditer(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", body_text):
            callee = m.group(1)
            if callee in ("if", "while", "for", "return"):
                continue
            target = self._find_function_declaration(callee)
            if not target or callee in seen:
                continue
            seen.add(callee)
            t_uri, t_doc, t_match = target
            from_range = self._location_for_range(
                uri,
                doc.text,
                body_start + m.start(1),
                body_start + m.end(1),
            )["range"]
            to_item = self._call_hierarchy_item(
                t_uri, t_doc.text, callee, t_match.start(1), t_match.end(1)
            )
            out.append({"to": to_item, "fromRanges": [from_range]})
        self._respond(req_id, out)

    def _handle_incoming_calls(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        item = params.get("item", {})
        target_name = item.get("name", "")
        if not target_name:
            self._respond(req_id, [])
            return
        out: list[dict[str, Any]] = []
        for uri, doc in self._documents.items():
            for fn_match in re.finditer(r"\bfn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(", doc.text):
                caller_name = fn_match.group(1)
                body = self._extract_function_body(doc.text, caller_name)
                if not body:
                    continue
                body_start, body_end = body
                body_text = doc.text[body_start:body_end]
                refs = list(re.finditer(rf"\b{re.escape(target_name)}\s*\(", body_text))
                if not refs:
                    continue
                from_item = self._call_hierarchy_item(
                    uri, doc.text, caller_name, fn_match.start(1), fn_match.end(1)
                )
                from_ranges = [
                    self._location_for_range(
                        uri,
                        doc.text,
                        body_start + r.start(),
                        body_start + r.start() + len(target_name),
                    )["range"]
                    for r in refs
                ]
                out.append({"from": from_item, "fromRanges": from_ranges})
        self._respond(req_id, out)

    def _handle_document_symbol(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, [])
            return
        kind_map = {"Function": 12, "Variable": 13, "Constant": 14}
        items: list[dict[str, Any]] = []
        for name, kind, start, end in self._declared_symbols(doc.text):
            loc = self._location_for_range(uri, doc.text, start, end)["range"]
            items.append(
                {
                    "name": name,
                    "kind": kind_map.get(kind, 13),
                    "range": loc,
                    "selectionRange": loc,
                }
            )
        self._respond(req_id, items)

    def _handle_workspace_symbol(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        query = str(params.get("query", "")).strip()
        kind_map = {"Function": 12, "Variable": 13, "Constant": 14}
        items: list[dict[str, Any]] = []
        for uri, doc in self._documents.items():
            for name, kind, start, end in self._declared_symbols(doc.text):
                if query and query.lower() not in name.lower():
                    continue
                items.append(
                    {
                        "name": name,
                        "kind": kind_map.get(kind, 13),
                        "location": self._location_for_range(uri, doc.text, start, end),
                    }
                )
        self._respond(req_id, items)

    def _handle_did_open(self, params: dict[str, Any]) -> None:
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        if not uri:
            return
        self._documents[uri] = Document(
            uri=uri,
            text=text_doc.get("text", ""),
            version=int(text_doc.get("version", 0)),
            language_id=text_doc.get("languageId", "mlang"),
        )
        self._invalidate_doc(uri)
        self._update_dependency_graph(uri)
        self._publish_diagnostics(uri)

    @staticmethod
    def _apply_change(text: str, change: dict[str, Any]) -> str:
        if "range" not in change:
            return change.get("text", "")
        rng = change["range"]
        start = rng.get("start", {})
        end = rng.get("end", {})
        start_off = line_char_to_offset(
            text, int(start.get("line", 0)), int(start.get("character", 0))
        )
        end_off = line_char_to_offset(
            text, int(end.get("line", 0)), int(end.get("character", 0))
        )
        if end_off < start_off:
            end_off = start_off
        new_text = change.get("text", "")
        return text[:start_off] + new_text + text[end_off:]

    def _handle_did_change(self, params: dict[str, Any]) -> None:
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            return
        changes = params.get("contentChanges", [])
        text = doc.text
        for change in changes:
            text = self._apply_change(text, change)
        doc.text = text
        doc.version = int(text_doc.get("version", doc.version))
        self._invalidate_doc(uri)
        self._update_dependency_graph(uri)
        self._publish_diagnostics(uri)

    def _handle_hover(self, req_id: Any, params: dict[str, Any]) -> None:
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, None)
            return
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return

        pos = params.get("position", {})
        offset = line_char_to_offset(
            doc.text, int(pos.get("line", 0)), int(pos.get("character", 0))
        )
        token = self._word_at(doc.text, offset)
        semantic = self._analyze(doc)
        symbol = next((s for s in semantic.symbols if s.name == token), None)
        if token == "fn":
            value = "Define a function."
        elif symbol is not None:
            value = f"Symbol `{symbol.name}`: `{symbol.type_name}`."
        elif token:
            value = f"Symbol `{token}`."
        else:
            self._respond(req_id, None)
            return
        self._respond(req_id, {"contents": {"kind": "markdown", "value": value}})

    def _handle_completion(self, req_id: Any, params: dict[str, Any]) -> None:
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, {"isIncomplete": False, "items": []})
            return
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return

        pos = params.get("position", {})
        line = int(pos.get("line", 0))
        character = int(pos.get("character", 0))
        line_text = self._get_line(doc.text, line)
        prefix = line_text[: min(character, len(line_text))]
        m = re.search(r"([A-Za-z_][A-Za-z0-9_]*)$", prefix)
        typed = m.group(1) if m else ""

        items = self._keyword_items()
        all_symbols: dict[str, str] = {}
        for open_doc in self._documents.values():
            for sym in self._analyze(open_doc).symbols:
                all_symbols.setdefault(sym.name, sym.type_name)
        for symbol, type_name in all_symbols.items():
            items.append({"label": symbol, "kind": 6, "detail": f"symbol: {type_name}"})

        if typed:
            items = [it for it in items if it["label"].startswith(typed)]
        self._respond(req_id, {"isIncomplete": False, "items": items})

    def _handle_completion_resolve(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        item = dict(params or {})
        label = str(item.get("label", ""))
        detail = str(item.get("detail", ""))
        if detail == "keyword":
            item["documentation"] = {
                "kind": "markdown",
                "value": f"`{label}` keyword",
            }
        elif detail.startswith("symbol:"):
            item["documentation"] = {
                "kind": "markdown",
                "value": f"Declared symbol `{label}` ({detail.split(':', 1)[1].strip()}).",
            }
        else:
            item["documentation"] = {
                "kind": "markdown",
                "value": f"Completion for `{label}`.",
            }
        return self._respond(req_id, item)

    def _handle_diagnostic(self, req_id: Any, params: dict[str, Any]) -> None:
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, {"kind": "full", "items": []})
            return
        if self._is_canceled(req_id):
            self._error(req_id, -32800, "Request cancelled")
            return
        token = params.get("workDoneToken")
        self._work_begin(token, "document diagnostics")
        items = [d.to_lsp(doc.text) for d in self._analyze(doc).diagnostics]
        self._work_end(token)
        self._respond(req_id, {"kind": "full", "items": items})

    def _dispatch_request(self, req_id: Any, method: str, params: dict[str, Any]) -> None:
        if method == "initialize":
            self._handle_initialize(req_id)
        elif method == "shutdown":
            self._shutdown_requested = True
            self._respond(req_id, None)
        elif method == "textDocument/hover":
            self._handle_hover(req_id, params)
        elif method == "textDocument/definition":
            self._handle_definition(req_id, params)
        elif method == "textDocument/declaration":
            self._handle_declaration(req_id, params)
        elif method == "textDocument/references":
            self._handle_references(req_id, params)
        elif method == "textDocument/prepareRename":
            self._handle_prepare_rename(req_id, params)
        elif method == "textDocument/rename":
            self._handle_rename(req_id, params)
        elif method == "textDocument/signatureHelp":
            self._handle_signature_help(req_id, params)
        elif method == "textDocument/semanticTokens/full":
            self._handle_semantic_tokens_full(req_id, params)
        elif method == "textDocument/semanticTokens/range":
            self._handle_semantic_tokens_range(req_id, params)
        elif method == "textDocument/codeAction":
            self._handle_code_action(req_id, params)
        elif method == "textDocument/inlayHint":
            self._handle_inlay_hint(req_id, params)
        elif method == "textDocument/formatting":
            self._handle_formatting(req_id, params)
        elif method == "textDocument/codeLens":
            self._handle_code_lens(req_id, params)
        elif method == "textDocument/selectionRange":
            self._handle_selection_range(req_id, params)
        elif method == "textDocument/linkedEditingRange":
            self._handle_linked_editing_range(req_id, params)
        elif method == "textDocument/documentHighlight":
            self._handle_document_highlight(req_id, params)
        elif method == "textDocument/foldingRange":
            self._handle_folding_range(req_id, params)
        elif method == "textDocument/documentLink":
            self._handle_document_link(req_id, params)
        elif method == "textDocument/prepareCallHierarchy":
            self._handle_prepare_call_hierarchy(req_id, params)
        elif method == "callHierarchy/incomingCalls":
            self._handle_incoming_calls(req_id, params)
        elif method == "callHierarchy/outgoingCalls":
            self._handle_outgoing_calls(req_id, params)
        elif method == "textDocument/documentSymbol":
            self._handle_document_symbol(req_id, params)
        elif method == "workspace/symbol":
            self._handle_workspace_symbol(req_id, params)
        elif method == "textDocument/completion":
            self._handle_completion(req_id, params)
        elif method == "completionItem/resolve":
            self._handle_completion_resolve(req_id, params)
        elif method == "textDocument/diagnostic":
            self._handle_diagnostic(req_id, params)
        elif method == "workspace/diagnostic":
            self._handle_workspace_diagnostic(req_id, params)
        elif method == "window/workDoneProgress/create":
            self._respond(req_id, None)
        else:
            self._error(req_id, -32601, f"Method not found: {method}")

    def _dispatch_notification(self, method: str, params: dict[str, Any]) -> None:
        if method == "exit":
            self._running = False
            return
        if method == "initialized":
            return
        if method == "textDocument/didOpen":
            self._handle_did_open(params)
            return
        if method == "textDocument/didChange":
            self._handle_did_change(params)
            return
        if method == "textDocument/didSave":
            text_doc = params.get("textDocument", {})
            uri = text_doc.get("uri", "")
            if uri:
                self._publish_diagnostics(uri)
            return
        if method == "textDocument/didClose":
            text_doc = params.get("textDocument", {})
            uri = text_doc.get("uri", "")
            if uri in self._documents:
                del self._documents[uri]
                self._invalidate_doc(uri)
                self._publish_diagnostics(uri)
            return
        if method == "$/cancelRequest":
            req_id = params.get("id")
            if isinstance(req_id, int):
                self._canceled_request_ids.add(req_id)
            return

    def run(self) -> int:
        while self._running:
            msg = self._read_message()
            if msg is None:
                break
            method = msg.get("method")
            if not method:
                continue
            params = msg.get("params", {})
            if "id" in msg:
                self._dispatch_request(msg["id"], method, params)
            else:
                self._dispatch_notification(method, params)
        return 0 if self._shutdown_requested else 1


def main() -> int:
    return JsonRpcServer().run()


if __name__ == "__main__":
    raise SystemExit(main())
