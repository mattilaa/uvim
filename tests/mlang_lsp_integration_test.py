#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import unittest
from pathlib import Path
from typing import Any, Dict, List, Optional


ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT / "tools" / "mlang_lsp" / "mlang_lsp.py"


class LspHarness:
    def __init__(self) -> None:
        cmd = self._resolve_server_cmd()
        if not cmd:
            raise unittest.SkipTest(
                "mlang LSP scaffold server not found at tools/mlang_lsp/mlang_lsp.py"
            )
        self.proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=False,
        )
        self._next_id = 1

    @staticmethod
    def _resolve_server_cmd() -> List[str]:
        env_cmd = os.environ.get("UVIM_MLANG_LSP_SERVER", "").strip()
        if env_cmd:
            parts = env_cmd.split()
            if parts:
                return parts

        if SERVER.exists():
            return [sys.executable, str(SERVER)]

        return []

    def close(self) -> None:
        if self.proc.poll() is None:
            try:
                self.request("shutdown", {})
            except Exception:
                pass
            self.notify("exit", {})
            self.proc.wait(timeout=2)
        if self.proc.stdin:
            self.proc.stdin.close()
        if self.proc.stdout:
            self.proc.stdout.close()
        if self.proc.stderr:
            self.proc.stderr.close()

    def _write(self, msg: Dict[str, Any]) -> None:
        assert self.proc.stdin is not None
        payload = json.dumps(msg, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
        header = f"Content-Length: {len(payload)}\r\n\r\n".encode("ascii")
        self.proc.stdin.write(header)
        self.proc.stdin.write(payload)
        self.proc.stdin.flush()

    def _read(self) -> Dict[str, Any]:
        assert self.proc.stdout is not None
        content_length: Optional[int] = None
        while True:
            line = self.proc.stdout.readline()
            if not line:
                details = "LSP server closed stdout unexpectedly"
                if self.proc.stderr is not None:
                    err = self.proc.stderr.read().decode("utf-8", errors="replace").strip()
                    if err:
                        details += f" | stderr: {err}"
                raise RuntimeError(details)
            if line in (b"\r\n", b"\n"):
                break
            head = line.decode("ascii").strip()
            if head.lower().startswith("content-length:"):
                content_length = int(head.split(":", 1)[1].strip())
        if content_length is None:
            raise RuntimeError("Missing Content-Length")
        body = self.proc.stdout.read(content_length)
        if not body:
            raise RuntimeError("Missing payload body")
        return json.loads(body.decode("utf-8"))

    def request(self, method: str, params: Dict[str, Any]) -> Dict[str, Any]:
        req_id = self._next_id
        self._next_id += 1
        self._write({"jsonrpc": "2.0", "id": req_id, "method": method, "params": params})
        return self._read_response_for_id(req_id, method)

    def request_with_id(self, req_id: int, method: str, params: Dict[str, Any]) -> Dict[str, Any]:
        self._write({"jsonrpc": "2.0", "id": req_id, "method": method, "params": params})
        return self._read_response_for_id(req_id, method)

    def _read_response_for_id(self, req_id: int, method: str) -> Dict[str, Any]:
        while True:
            msg = self._read()
            if msg.get("id") == req_id:
                if "error" in msg:
                    raise RuntimeError(f"LSP error for {method}: {msg['error']}")
                return msg.get("result")

    def notify(self, method: str, params: Dict[str, Any]) -> None:
        self._write({"jsonrpc": "2.0", "method": method, "params": params})

    def read_until_notification(self, method: str, max_reads: int = 20) -> Dict[str, Any]:
        for _ in range(max_reads):
            msg = self._read()
            if msg.get("method") == method:
                return msg.get("params", {})
        raise RuntimeError(f"Did not receive notification: {method}")


class MlangLspIntegrationTest(unittest.TestCase):
    def setUp(self) -> None:
        self.h = LspHarness()

    def tearDown(self) -> None:
        self.h.close()

    def test_initialize_hover_completion_and_diagnostics(self) -> None:
        init = self.h.request(
            "initialize",
            {
                "processId": None,
                "rootUri": None,
                "capabilities": {},
                "clientInfo": {"name": "mlang-lsp-test", "version": "1"},
            },
        )
        caps = init.get("capabilities", {})
        self.assertTrue(caps.get("hoverProvider"))
        self.assertIn("completionProvider", caps)

        self.h.notify("initialized", {})

        uri = "file:///tmp/test.mlang"
        source = "fn main() {\n  let value = 1\n}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        open_diag = self.h.read_until_notification("textDocument/publishDiagnostics")
        self.assertEqual(open_diag.get("uri"), uri)
        self.assertEqual(open_diag.get("diagnostics"), [])

        hover = self.h.request(
            "textDocument/hover",
            {"textDocument": {"uri": uri}, "position": {"line": 0, "character": 1}},
        )
        self.assertIn("Define a function", hover["contents"]["value"])

        keyword_completion = self.h.request(
            "textDocument/completion",
            {"textDocument": {"uri": uri}, "position": {"line": 1, "character": 4}},
        )
        labels = {item.get("label") for item in keyword_completion.get("items", [])}
        self.assertIn("let", labels)

        symbol_completion = self.h.request(
            "textDocument/completion",
            {"textDocument": {"uri": uri}, "position": {"line": 1, "character": 10}},
        )
        labels = {item.get("label") for item in symbol_completion.get("items", [])}
        self.assertIn("value", labels)

        bad_uri = "file:///tmp/bad.mlang"
        self.h.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": bad_uri,
                    "languageId": "mlang",
                    "version": 1,
                    "text": "fn main() {\n  let s = \"oops\n}\n",
                }
            },
        )
        bad_diag = self.h.read_until_notification("textDocument/publishDiagnostics")
        self.assertEqual(bad_diag.get("uri"), bad_uri)
        self.assertGreater(len(bad_diag.get("diagnostics", [])), 0)

    def test_incremental_did_change_range_patching(self) -> None:
        self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.h.notify("initialized", {})

        uri = "file:///tmp/inc.mlang"
        self.h.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri,
                    "languageId": "mlang",
                    "version": 1,
                    "text": "fn main() {\n  let valeu = 1\n}\n",
                }
            },
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        # Replace "valeu" -> "value" via ranged incremental edit.
        self.h.notify(
            "textDocument/didChange",
            {
                "textDocument": {"uri": uri, "version": 2},
                "contentChanges": [
                    {
                        "range": {
                            "start": {"line": 1, "character": 6},
                            "end": {"line": 1, "character": 11},
                        },
                        "text": "value",
                    }
                ],
            },
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        completion = self.h.request(
            "textDocument/completion",
            {"textDocument": {"uri": uri}, "position": {"line": 1, "character": 11}},
        )
        labels = {item.get("label") for item in completion.get("items", [])}
        self.assertIn("value", labels)
        self.assertNotIn("valeu", labels)

    def test_completion_includes_workspace_symbols_from_other_open_documents(self) -> None:
        self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.h.notify("initialized", {})

        uri_a = "file:///tmp/a.mlang"
        uri_b = "file:///tmp/b.mlang"

        self.h.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri_a,
                    "languageId": "mlang",
                    "version": 1,
                    "text": "fn helper() {}\n",
                }
            },
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        self.h.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri_b,
                    "languageId": "mlang",
                    "version": 1,
                    "text": "fn main() {\n  hel\n}\n",
                }
            },
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        completion = self.h.request(
            "textDocument/completion",
            {"textDocument": {"uri": uri_b}, "position": {"line": 1, "character": 5}},
        )
        labels = {item.get("label") for item in completion.get("items", [])}
        self.assertIn("helper", labels)

    def test_hover_shows_symbol_type_from_semantic_model(self) -> None:
        self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.h.notify("initialized", {})

        uri = "file:///tmp/types.mlang"
        source = "fn main() {\n  let count = 1;\n}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        hover = self.h.request(
            "textDocument/hover",
            {"textDocument": {"uri": uri}, "position": {"line": 1, "character": 7}},
        )
        self.assertIn("count", hover["contents"]["value"])
        self.assertIn("Int", hover["contents"]["value"])

    def test_semantic_diagnostic_for_unknown_identifier(self) -> None:
        self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.h.notify("initialized", {})

        uri = "file:///tmp/unknown.mlang"
        source = "fn main() {\n  let dst = src;\n}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        diags = self.h.read_until_notification("textDocument/publishDiagnostics")
        messages = [d.get("message", "") for d in diags.get("diagnostics", [])]
        self.assertIn("Unknown identifier 'src'", messages)

    def test_workspace_diagnostic_and_progress_notifications(self) -> None:
        self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.h.notify("initialized", {})

        uri = "file:///tmp/workspace.mlang"
        source = "import core.io;\nfn main() { let dst = src; }\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        req_id = 77
        token = "wk-1"
        self.h._write(
            {
                "jsonrpc": "2.0",
                "id": req_id,
                "method": "workspace/diagnostic",
                "params": {"workDoneToken": token},
            }
        )

        saw_begin = False
        saw_end = False
        result: Dict[str, Any] = {}
        for _ in range(20):
            msg = self.h._read()
            if msg.get("method") == "$/progress":
                params = msg.get("params", {})
                if params.get("token") == token:
                    kind = params.get("value", {}).get("kind")
                    if kind == "begin":
                        saw_begin = True
                    if kind == "end":
                        saw_end = True
                continue
            if msg.get("id") == req_id:
                self.assertNotIn("error", msg)
                result = msg.get("result", {})
            if saw_begin and saw_end and result:
                break

        self.assertTrue(saw_begin)
        self.assertTrue(saw_end)
        self.assertGreaterEqual(len(result.get("items", [])), 1)
        item_diags = result["items"][0].get("items", [])
        self.assertTrue(any("Unknown identifier 'src'" == d.get("message") for d in item_diags))

    def test_cancel_request_returns_cancelled_error(self) -> None:
        self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.h.notify("initialized", {})

        uri = "file:///tmp/cancel.mlang"
        source = "fn main() { let count = 1; }\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        req_id = 123
        self.h.notify("$/cancelRequest", {"id": req_id})
        self.h._write(
            {
                "jsonrpc": "2.0",
                "id": req_id,
                "method": "textDocument/diagnostic",
                "params": {"textDocument": {"uri": uri}},
            }
        )
        msg = self.h._read()
        self.assertEqual(msg.get("id"), req_id)
        self.assertIn("error", msg)
        self.assertEqual(msg["error"].get("code"), -32800)

    def test_phase5_navigation_references_and_rename(self) -> None:
        init = self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        caps = init.get("capabilities", {})
        self.assertTrue(caps.get("definitionProvider"))
        self.assertTrue(caps.get("declarationProvider"))
        self.assertTrue(caps.get("referencesProvider"))
        self.assertTrue(caps.get("renameProvider"))
        self.h.notify("initialized", {})

        uri = "file:///tmp/nav.mlang"
        source = (
            "fn helper(x, y) { return x; }\n"
            "fn main() {\n"
            "  let count = 1;\n"
            "  let out = helper(count, 2);\n"
            "  return count;\n"
            "}\n"
        )
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        definition = self.h.request(
            "textDocument/definition",
            {"textDocument": {"uri": uri}, "position": {"line": 3, "character": 13}},
        )
        self.assertEqual(definition["uri"], uri)
        self.assertEqual(definition["range"]["start"]["line"], 0)

        declaration = self.h.request(
            "textDocument/declaration",
            {"textDocument": {"uri": uri}, "position": {"line": 4, "character": 10}},
        )
        self.assertEqual(declaration["range"]["start"]["line"], 2)

        refs = self.h.request(
            "textDocument/references",
            {
                "textDocument": {"uri": uri},
                "position": {"line": 2, "character": 7},
                "context": {"includeDeclaration": True},
            },
        )
        self.assertGreaterEqual(len(refs), 3)

        refs_no_decl = self.h.request(
            "textDocument/references",
            {
                "textDocument": {"uri": uri},
                "position": {"line": 2, "character": 7},
                "context": {"includeDeclaration": False},
            },
        )
        self.assertLess(len(refs_no_decl), len(refs))

        prep = self.h.request(
            "textDocument/prepareRename",
            {"textDocument": {"uri": uri}, "position": {"line": 2, "character": 7}},
        )
        self.assertEqual(prep["start"]["line"], 2)

        rename = self.h.request(
            "textDocument/rename",
            {
                "textDocument": {"uri": uri},
                "position": {"line": 2, "character": 7},
                "newName": "total",
            },
        )
        edits = rename.get("changes", {}).get(uri, [])
        self.assertGreaterEqual(len(edits), 3)

    def test_phase5_signature_help_semantic_tokens_and_code_action(self) -> None:
        init = self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        caps = init.get("capabilities", {})
        self.assertIn("signatureHelpProvider", caps)
        self.assertIn("semanticTokensProvider", caps)
        self.assertTrue(caps.get("codeActionProvider"))
        self.h.notify("initialized", {})

        uri = "file:///tmp/phase5.mlang"
        source = (
            "fn add(a, b) { return a; }\n"
            "fn main() {\n"
            "  let srcVal = 1;\n"
            "  let value = add(srcVal, 2);\n"
            "  let bad = src;\n"
            "  return value;\n"
            "}\n"
        )
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        diag_params = self.h.read_until_notification("textDocument/publishDiagnostics")

        sig = self.h.request(
            "textDocument/signatureHelp",
            {"textDocument": {"uri": uri}, "position": {"line": 3, "character": 22}},
        )
        self.assertIn("add(a, b)", sig["signatures"][0]["label"])

        tokens = self.h.request(
            "textDocument/semanticTokens/full",
            {"textDocument": {"uri": uri}},
        )
        self.assertGreater(len(tokens.get("data", [])), 0)

        actions = self.h.request(
            "textDocument/codeAction",
            {
                "textDocument": {"uri": uri},
                "range": {"start": {"line": 4, "character": 12}, "end": {"line": 4, "character": 15}},
                "context": {"diagnostics": diag_params.get("diagnostics", [])},
            },
        )
        self.assertGreaterEqual(len(actions), 1)
        self.assertIn("quickfix", actions[0].get("kind", ""))

    def test_symbol_queries_document_and_workspace(self) -> None:
        init = self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        caps = init.get("capabilities", {})
        self.assertTrue(caps.get("documentSymbolProvider"))
        self.assertTrue(caps.get("workspaceSymbolProvider"))
        self.h.notify("initialized", {})

        uri_a = "file:///tmp/symbol_a.mlang"
        src_a = "fn helper() {}\nconst PI = 3;\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri_a, "languageId": "mlang", "version": 1, "text": src_a}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        uri_b = "file:///tmp/symbol_b.mlang"
        src_b = "fn main() {\n  let value = 1;\n}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri_b, "languageId": "mlang", "version": 1, "text": src_b}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        doc_symbols = self.h.request(
            "textDocument/documentSymbol",
            {"textDocument": {"uri": uri_b}},
        )
        names = {s.get("name") for s in doc_symbols}
        self.assertIn("main", names)
        self.assertIn("value", names)
        self.assertNotIn("helper", names)

        ws_symbols = self.h.request("workspace/symbol", {"query": "he"})
        ws_names = {s.get("name") for s in ws_symbols}
        self.assertIn("helper", ws_names)


if __name__ == "__main__":
    os.environ.setdefault("PYTHONUNBUFFERED", "1")
    unittest.main(verbosity=2)
