#!/usr/bin/env python3
from __future__ import annotations

import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

TOOLS_DIR = Path(__file__).resolve().parents[1]
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from mlang_frontend.diagnostics import analyze_text
from mlang_frontend.parser import collect_symbols
from mlang_frontend.source_map import line_char_to_offset


@dataclass
class Document:
    uri: str
    text: str
    version: int
    language_id: str


class JsonRpcServer:
    def __init__(self) -> None:
        self._documents: dict[str, Document] = {}
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

        diagnostics = [d.to_lsp(doc.text) for d in analyze_text(doc.text)]
        self._notify(
            "textDocument/publishDiagnostics",
            {"uri": uri, "diagnostics": diagnostics, "version": doc.version},
        )

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
            for symbol in collect_symbols(doc.text):
                if symbol in seen:
                    continue
                seen.add(symbol)
                symbols.append(symbol)
        return symbols

    def _handle_initialize(self, req_id: Any) -> None:
        self._respond(
            req_id,
            {
                "serverInfo": {"name": "mlang-lsp-scaffold", "version": "0.1.0"},
                "capabilities": {
                    "textDocumentSync": 2,
                    "hoverProvider": True,
                    "completionProvider": {
                        "resolveProvider": False,
                        "triggerCharacters": [".", ":"],
                    },
                    "diagnosticProvider": {
                        "interFileDependencies": False,
                        "workspaceDiagnostics": False,
                    },
                },
            },
        )

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
        self._publish_diagnostics(uri)

    def _handle_hover(self, req_id: Any, params: dict[str, Any]) -> None:
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, None)
            return

        pos = params.get("position", {})
        offset = line_char_to_offset(
            doc.text, int(pos.get("line", 0)), int(pos.get("character", 0))
        )
        token = self._word_at(doc.text, offset)
        if token == "fn":
            value = "Define a function."
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

        pos = params.get("position", {})
        line = int(pos.get("line", 0))
        character = int(pos.get("character", 0))
        line_text = self._get_line(doc.text, line)
        prefix = line_text[: min(character, len(line_text))]
        m = re.search(r"([A-Za-z_][A-Za-z0-9_]*)$", prefix)
        typed = m.group(1) if m else ""

        items = self._keyword_items()
        for symbol in self._workspace_symbols():
            items.append({"label": symbol, "kind": 6, "detail": "symbol"})

        if typed:
            items = [it for it in items if it["label"].startswith(typed)]
        self._respond(req_id, {"isIncomplete": False, "items": items})

    def _handle_diagnostic(self, req_id: Any, params: dict[str, Any]) -> None:
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, {"kind": "full", "items": []})
            return
        items = [d.to_lsp(doc.text) for d in analyze_text(doc.text)]
        self._respond(req_id, {"kind": "full", "items": items})

    def _dispatch_request(self, req_id: Any, method: str, params: dict[str, Any]) -> None:
        if method == "initialize":
            self._handle_initialize(req_id)
        elif method == "shutdown":
            self._shutdown_requested = True
            self._respond(req_id, None)
        elif method == "textDocument/hover":
            self._handle_hover(req_id, params)
        elif method == "textDocument/completion":
            self._handle_completion(req_id, params)
        elif method == "textDocument/diagnostic":
            self._handle_diagnostic(req_id, params)
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
                self._publish_diagnostics(uri)
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
