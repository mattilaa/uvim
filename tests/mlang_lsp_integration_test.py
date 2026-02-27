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
        self.assertTrue(caps.get("completionProvider", {}).get("resolveProvider"))

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
        self.assertIn("range", hover)

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

        # Repeat with previousResultIds and expect unchanged for the same doc.
        prev_ids = []
        for it in result.get("items", []):
            if it.get("uri") and it.get("resultId"):
                prev_ids.append({"uri": it["uri"], "value": it["resultId"]})
        second = self.h.request(
            "workspace/diagnostic",
            {"previousResultIds": prev_ids},
        )
        self.assertGreaterEqual(len(second.get("items", [])), 1)
        self.assertTrue(any(i.get("kind") == "unchanged" for i in second.get("items", [])))

    def test_document_diagnostic_supports_previous_result_id(self) -> None:
        self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.h.notify("initialized", {})

        uri = "file:///tmp/diag_result_id.mlang"
        source = "fn main() { let dst = src; }\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        first = self.h.request("textDocument/diagnostic", {"textDocument": {"uri": uri}})
        self.assertEqual(first.get("kind"), "full")
        self.assertIn("resultId", first)

        second = self.h.request(
            "textDocument/diagnostic",
            {
                "textDocument": {"uri": uri},
                "previousResultId": first.get("resultId"),
            },
        )
        self.assertEqual(second.get("kind"), "unchanged")
        self.assertEqual(second.get("resultId"), first.get("resultId"))

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
        self.assertTrue(caps.get("implementationProvider"))
        self.assertTrue(caps.get("typeDefinitionProvider"))
        self.assertTrue(caps.get("typeHierarchyProvider"))
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

        implementation = self.h.request(
            "textDocument/implementation",
            {"textDocument": {"uri": uri}, "position": {"line": 3, "character": 13}},
        )
        self.assertEqual(implementation["uri"], uri)
        self.assertEqual(implementation["range"]["start"]["line"], 0)

        type_def = self.h.request(
            "textDocument/typeDefinition",
            {"textDocument": {"uri": uri}, "position": {"line": 2, "character": 8}},
        )
        self.assertEqual(type_def["uri"], "mlang:///types/Int.mla")

        prepared = self.h.request(
            "textDocument/prepareTypeHierarchy",
            {"textDocument": {"uri": uri}, "position": {"line": 2, "character": 8}},
        )
        self.assertEqual(len(prepared), 1)
        self.assertEqual(prepared[0].get("name"), "Int")

        supers = self.h.request("typeHierarchy/supertypes", {"item": prepared[0]})
        self.assertEqual(len(supers), 1)
        self.assertEqual(supers[0].get("name"), "Any")

        subs = self.h.request("typeHierarchy/subtypes", {"item": supers[0]})
        sub_names = {s.get("name") for s in subs}
        self.assertIn("Int", sub_names)

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
        self.assertEqual(prep["range"]["start"]["line"], 2)
        self.assertEqual(prep["placeholder"], "count")

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
        self.assertTrue(caps.get("semanticTokensProvider", {}).get("range"))
        self.assertTrue(caps.get("codeActionProvider", {}).get("resolveProvider"))
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

        range_tokens = self.h.request(
            "textDocument/semanticTokens/range",
            {
                "textDocument": {"uri": uri},
                "range": {
                    "start": {"line": 3, "character": 0},
                    "end": {"line": 3, "character": 40},
                },
            },
        )
        self.assertGreater(len(range_tokens.get("data", [])), 0)
        self.assertLessEqual(len(range_tokens.get("data", [])), len(tokens.get("data", [])))

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
        resolved = self.h.request("codeAction/resolve", actions[0])
        self.assertIn("documentation", resolved)
        self.assertTrue(resolved.get("isPreferred"))

    def test_symbol_queries_document_and_workspace(self) -> None:
        init = self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        caps = init.get("capabilities", {})
        self.assertTrue(caps.get("documentSymbolProvider"))
        self.assertTrue(caps.get("workspaceSymbolProvider", {}).get("resolveProvider"))
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
        resolved = self.h.request("workspaceSymbol/resolve", ws_symbols[0])
        self.assertIn("detail", resolved)
        self.assertIn("containerName", resolved)

    def test_inlay_hints_for_inferred_types(self) -> None:
        init = self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.assertTrue(init.get("capabilities", {}).get("inlayHintProvider", {}).get("resolveProvider"))
        self.assertTrue(init.get("capabilities", {}).get("inlineValueProvider"))
        self.h.notify("initialized", {})

        uri = "file:///tmp/hints.mlang"
        source = (
            "fn main() {\n"
            "  let count = 1;\n"
            "  let name = \"x\";\n"
            "  let ok = true;\n"
            "}\n"
        )
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        hints = self.h.request(
            "textDocument/inlayHint",
            {
                "textDocument": {"uri": uri},
                "range": {
                    "start": {"line": 0, "character": 0},
                    "end": {"line": 10, "character": 0},
                },
            },
        )
        labels = {h.get("label") for h in hints}
        self.assertIn(": Int", labels)
        self.assertIn(": String", labels)
        self.assertIn(": Bool", labels)
        resolved = self.h.request("inlayHint/resolve", hints[0])
        self.assertIn("tooltip", resolved)

    def test_inline_values_for_local_bindings(self) -> None:
        self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.h.notify("initialized", {})

        uri = "file:///tmp/inline_values.mlang"
        source = (
            "fn main() {\n"
            "  let count = 1;\n"
            "  const name = \"x\";\n"
            "}\n"
        )
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        values = self.h.request(
            "textDocument/inlineValue",
            {
                "textDocument": {"uri": uri},
                "range": {
                    "start": {"line": 0, "character": 0},
                    "end": {"line": 10, "character": 0},
                },
                "context": {
                    "frameId": 1,
                    "stoppedLocation": {
                        "start": {"line": 1, "character": 0},
                        "end": {"line": 2, "character": 0},
                    },
                },
            },
        )
        texts = {v.get("text", "") for v in values}
        self.assertTrue(any("count: Int = 1" in t for t in texts))
        self.assertTrue(any('name: String = "x"' in t for t in texts))

    def test_moniker_for_symbol(self) -> None:
        init = self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.assertTrue(init.get("capabilities", {}).get("monikerProvider"))
        self.h.notify("initialized", {})

        uri = "file:///tmp/moniker.mlang"
        source = "fn helper() {}\nfn main() { let value = 1; return value; }\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        mons = self.h.request(
            "textDocument/moniker",
            {"textDocument": {"uri": uri}, "position": {"line": 0, "character": 4}},
        )
        self.assertEqual(len(mons), 1)
        self.assertEqual(mons[0].get("scheme"), "mlang")
        self.assertIn("helper", mons[0].get("identifier", ""))

    def test_document_color_and_color_presentation(self) -> None:
        init = self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.assertTrue(init.get("capabilities", {}).get("colorProvider"))
        self.h.notify("initialized", {})

        uri = "file:///tmp/colors.mlang"
        source = 'fn main() { let c = "#FF00AA"; }\n'
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        colors = self.h.request("textDocument/documentColor", {"textDocument": {"uri": uri}})
        self.assertEqual(len(colors), 1)
        color = colors[0].get("color", {})
        self.assertAlmostEqual(color.get("red", 0), 1.0, places=4)
        self.assertAlmostEqual(color.get("green", 1), 0.0, places=4)

        presentations = self.h.request(
            "textDocument/colorPresentation",
            {
                "textDocument": {"uri": uri},
                "color": color,
                "range": colors[0].get("range"),
            },
        )
        self.assertGreaterEqual(len(presentations), 1)
        self.assertEqual(presentations[0].get("label"), "#FF00AA")

    def test_document_formatting_returns_full_text_edit(self) -> None:
        init = self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.assertTrue(init.get("capabilities", {}).get("documentFormattingProvider"))
        self.assertTrue(init.get("capabilities", {}).get("documentRangeFormattingProvider"))
        self.assertIn("documentOnTypeFormattingProvider", init.get("capabilities", {}))
        self.h.notify("initialized", {})

        uri = "file:///tmp/fmt.mlang"
        source = "fn main(){\nlet x = 1\nreturn x\n}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        edits = self.h.request(
            "textDocument/formatting",
            {
                "textDocument": {"uri": uri},
                "options": {"tabSize": 2, "insertSpaces": True},
            },
        )
        self.assertEqual(len(edits), 1)
        new_text = edits[0].get("newText", "")
        self.assertIn("  let x = 1;", new_text)
        self.assertIn("  return x;", new_text)

    def test_document_formatting_honors_tab_size_option(self) -> None:
        self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.h.notify("initialized", {})

        uri = "file:///tmp/fmt_tabsize.mlang"
        source = "fn main(){\nlet x = 1\n}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        edits = self.h.request(
            "textDocument/formatting",
            {
                "textDocument": {"uri": uri},
                "options": {"tabSize": 4, "insertSpaces": True},
            },
        )
        self.assertEqual(len(edits), 1)
        self.assertIn("    let x = 1;", edits[0].get("newText", ""))

    def test_on_type_formatting_fixes_current_line(self) -> None:
        self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.h.notify("initialized", {})

        uri = "file:///tmp/on_type.mlang"
        source = "fn main() {\nlet x = 1\n}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        edits = self.h.request(
            "textDocument/onTypeFormatting",
            {
                "textDocument": {"uri": uri},
                "position": {"line": 1, "character": 9},
                "ch": ";",
                "options": {"tabSize": 2, "insertSpaces": True},
            },
        )
        self.assertEqual(len(edits), 1)
        self.assertEqual(edits[0].get("newText"), "  let x = 1;")

    def test_range_formatting_only_updates_selected_block(self) -> None:
        self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.h.notify("initialized", {})

        uri = "file:///tmp/range_fmt.mlang"
        source = "fn main() {\nlet x = 1\nlet y = 2\n}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        edits = self.h.request(
            "textDocument/rangeFormatting",
            {
                "textDocument": {"uri": uri},
                "range": {
                    "start": {"line": 1, "character": 0},
                    "end": {"line": 2, "character": 10},
                },
                "options": {"tabSize": 2, "insertSpaces": True},
            },
        )
        self.assertEqual(len(edits), 1)
        block = edits[0].get("newText", "")
        self.assertIn("let x = 1;", block)
        self.assertIn("let y = 2;", block)

    def test_code_lens_for_functions(self) -> None:
        init = self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.assertTrue(init.get("capabilities", {}).get("codeLensProvider", {}).get("resolveProvider"))
        self.h.notify("initialized", {})

        uri = "file:///tmp/lens.mlang"
        source = "fn alpha() {}\nfn beta() {}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        lenses = self.h.request("textDocument/codeLens", {"textDocument": {"uri": uri}})
        self.assertEqual(len(lenses), 2)
        titles = {l.get("command", {}).get("title") for l in lenses}
        self.assertIn("Run alpha", titles)
        self.assertIn("Run beta", titles)

        resolved = self.h.request("codeLens/resolve", lenses[0])
        self.assertIn("(resolved)", resolved.get("command", {}).get("title", ""))
        self.assertIn("tooltip", resolved.get("command", {}))

    def test_completion_item_resolve_returns_documentation(self) -> None:
        self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.h.notify("initialized", {})

        uri = "file:///tmp/resolve.mlang"
        source = "fn main() {\n  let value = 1;\n  val\n}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        completion = self.h.request(
            "textDocument/completion",
            {"textDocument": {"uri": uri}, "position": {"line": 2, "character": 5}},
        )
        items = completion.get("items", [])
        self.assertGreaterEqual(len(items), 1)
        target = next((i for i in items if i.get("label") == "value"), items[0])

        resolved = self.h.request("completionItem/resolve", target)
        self.assertIn("documentation", resolved)
        doc = resolved.get("documentation", {}).get("value", "")
        self.assertIn("value", doc)

    def test_function_completion_uses_snippet_insert_text(self) -> None:
        self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.h.notify("initialized", {})

        uri = "file:///tmp/fn_completion.mlang"
        source = "fn helper() {}\nfn main() {\n  hel\n}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        completion = self.h.request(
            "textDocument/completion",
            {"textDocument": {"uri": uri}, "position": {"line": 2, "character": 5}},
        )
        helper = next((i for i in completion.get("items", []) if i.get("label") == "helper"), None)
        self.assertIsNotNone(helper)
        self.assertEqual(helper.get("insertTextFormat"), 2)
        self.assertEqual(helper.get("insertText"), "helper($1)")
        self.assertIn("labelDetails", helper)
        self.assertIn("(", helper.get("labelDetails", {}).get("detail", ""))
        resolved = self.h.request("completionItem/resolve", helper)
        self.assertIn("documentation", resolved)
        self.assertIn("(", resolved.get("documentation", {}).get("value", ""))

    def test_selection_range_returns_nested_ranges(self) -> None:
        init = self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.assertTrue(init.get("capabilities", {}).get("selectionRangeProvider"))
        self.h.notify("initialized", {})

        uri = "file:///tmp/selection.mlang"
        source = "fn main() {\n  let value = 1;\n}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        ranges = self.h.request(
            "textDocument/selectionRange",
            {
                "textDocument": {"uri": uri},
                "positions": [{"line": 1, "character": 7}],
            },
        )
        self.assertEqual(len(ranges), 1)
        root = ranges[0]
        self.assertIn("range", root)
        self.assertIn("parent", root)
        self.assertIn("parent", root["parent"])

    def test_linked_editing_range_for_identifier_occurrences(self) -> None:
        init = self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.assertTrue(init.get("capabilities", {}).get("linkedEditingRangeProvider"))
        self.h.notify("initialized", {})

        uri = "file:///tmp/linked.mlang"
        source = "fn main() {\n  let value = 1;\n  return value;\n}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        linked = self.h.request(
            "textDocument/linkedEditingRange",
            {
                "textDocument": {"uri": uri},
                "position": {"line": 1, "character": 7},
            },
        )
        self.assertIn("ranges", linked)
        self.assertGreaterEqual(len(linked.get("ranges", [])), 2)

    def test_document_highlight_marks_read_and_write_occurrences(self) -> None:
        init = self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.assertTrue(init.get("capabilities", {}).get("documentHighlightProvider"))
        self.h.notify("initialized", {})

        uri = "file:///tmp/highlight.mlang"
        source = "fn main() {\n  let value = 1;\n  return value;\n}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        highlights = self.h.request(
            "textDocument/documentHighlight",
            {"textDocument": {"uri": uri}, "position": {"line": 2, "character": 10}},
        )
        self.assertGreaterEqual(len(highlights), 2)
        kinds = {h.get("kind") for h in highlights}
        self.assertIn(3, kinds)  # declaration/write
        self.assertIn(2, kinds)  # read usage

    def test_folding_range_for_blocks_and_comment_runs(self) -> None:
        init = self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.assertTrue(init.get("capabilities", {}).get("foldingRangeProvider"))
        self.h.notify("initialized", {})

        uri = "file:///tmp/fold.mlang"
        source = (
            "// head1\n"
            "// head2\n"
            "fn main() {\n"
            "  if true {\n"
            "    let v = 1;\n"
            "  }\n"
            "}\n"
        )
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        ranges = self.h.request("textDocument/foldingRange", {"textDocument": {"uri": uri}})
        self.assertGreaterEqual(len(ranges), 2)
        kinds = {r.get("kind") for r in ranges}
        self.assertIn("comment", kinds)
        self.assertIn("region", kinds)

    def test_call_hierarchy_incoming_and_outgoing(self) -> None:
        init = self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.assertTrue(init.get("capabilities", {}).get("callHierarchyProvider"))
        self.h.notify("initialized", {})

        uri = "file:///tmp/call_hierarchy.mlang"
        source = (
            "fn callee() { return 1; }\n"
            "fn helper() { return callee(); }\n"
            "fn main() {\n"
            "  return helper();\n"
            "}\n"
        )
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        prepared = self.h.request(
            "textDocument/prepareCallHierarchy",
            {"textDocument": {"uri": uri}, "position": {"line": 1, "character": 4}},
        )
        self.assertEqual(len(prepared), 1)
        helper_item = prepared[0]
        self.assertEqual(helper_item.get("name"), "helper")

        outgoing = self.h.request("callHierarchy/outgoingCalls", {"item": helper_item})
        self.assertGreaterEqual(len(outgoing), 1)
        self.assertIn("callee", {c.get("to", {}).get("name") for c in outgoing})

        incoming = self.h.request("callHierarchy/incomingCalls", {"item": helper_item})
        self.assertGreaterEqual(len(incoming), 1)
        self.assertIn("main", {c.get("from", {}).get("name") for c in incoming})

    def test_document_link_for_import_statements(self) -> None:
        init = self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.assertIn("documentLinkProvider", init.get("capabilities", {}))
        self.assertIn("executeCommandProvider", init.get("capabilities", {}))
        commands = init.get("capabilities", {}).get("executeCommandProvider", {}).get("commands", [])
        self.assertIn("mlang.sortImports", commands)
        self.assertIn("mlang.addMissingSemicolons", commands)
        self.h.notify("initialized", {})

        uri = "file:///tmp/doclinks.mlang"
        source = "import core.io;\nimport pkg.math;\nfn main() {}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        links = self.h.request("textDocument/documentLink", {"textDocument": {"uri": uri}})
        self.assertEqual(len(links), 2)
        targets = {l.get("target", "") for l in links}
        self.assertIn("mlang:///modules/core/io.mla", targets)
        self.assertIn("mlang:///modules/pkg/math.mla", targets)

    def test_execute_command_sort_imports_returns_workspace_edit(self) -> None:
        self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.h.notify("initialized", {})

        uri = "file:///tmp/sort_imports.mlang"
        source = "import zeta.mod;\nimport alpha.mod;\nfn main() {}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        result = self.h.request(
            "workspace/executeCommand",
            {"command": "mlang.sortImports", "arguments": [uri]},
        )
        changes = result.get("changes", {}).get(uri, [])
        self.assertEqual(len(changes), 1)
        self.assertIn("import alpha.mod;", changes[0].get("newText", ""))

    def test_execute_command_add_missing_semicolons(self) -> None:
        self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.h.notify("initialized", {})

        uri = "file:///tmp/add_semis.mlang"
        source = "fn main() {\n  let x = 1\n  return x\n}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        result = self.h.request(
            "workspace/executeCommand",
            {"command": "mlang.addMissingSemicolons", "arguments": [uri]},
        )
        changes = result.get("changes", {}).get(uri, [])
        self.assertEqual(len(changes), 1)
        new_text = changes[0].get("newText", "")
        self.assertIn("let x = 1;", new_text)
        self.assertIn("return x;", new_text)

    def test_will_rename_files_updates_import_paths(self) -> None:
        init = self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        ws = init.get("capabilities", {}).get("workspace", {})
        self.assertIn("fileOperations", ws)
        self.assertIn("willCreate", ws.get("fileOperations", {}))
        self.assertIn("willDelete", ws.get("fileOperations", {}))
        self.h.notify("initialized", {})

        uri = "file:///tmp/rename_imports.mlang"
        source = "import alpha.mod;\nfn main() {}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        result = self.h.request(
            "workspace/willRenameFiles",
            {
                "files": [
                    {
                        "oldUri": "mlang:///modules/alpha/mod.mla",
                        "newUri": "mlang:///modules/alpha/newmod.mla",
                    }
                ]
            },
        )
        edits = result.get("changes", {}).get(uri, [])
        self.assertEqual(len(edits), 1)
        self.assertEqual(edits[0].get("newText"), "alpha.newmod")

    def test_will_create_files_suggests_import_insert(self) -> None:
        self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.h.notify("initialized", {})

        uri = "file:///tmp/will_create_imports.mlang"
        source = "import alpha.mod;\nfn main() {}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        result = self.h.request(
            "workspace/willCreateFiles",
            {"files": [{"uri": "mlang:///modules/beta/newmod.mla"}]},
        )
        edits = result.get("changes", {}).get(uri, [])
        self.assertEqual(len(edits), 1)
        self.assertIn("import beta.newmod;", edits[0].get("newText", ""))

    def test_will_delete_files_removes_matching_imports(self) -> None:
        self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.h.notify("initialized", {})

        uri = "file:///tmp/will_delete_imports.mlang"
        source = "import alpha.mod;\nimport beta.mod;\nfn main() {}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        result = self.h.request(
            "workspace/willDeleteFiles",
            {"files": [{"uri": "mlang:///modules/beta/mod.mla"}]},
        )
        edits = result.get("changes", {}).get(uri, [])
        self.assertEqual(len(edits), 1)
        self.assertEqual(edits[0].get("newText"), "")


if __name__ == "__main__":
    os.environ.setdefault("PYTHONUNBUFFERED", "1")
    unittest.main(verbosity=2)
