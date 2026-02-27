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


if __name__ == "__main__":
    os.environ.setdefault("PYTHONUNBUFFERED", "1")
    unittest.main(verbosity=2)
