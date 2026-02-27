#!/usr/bin/env python3
import unittest

from tools.mlang_frontend.compiler_api import analyze_document


class MlangFrontendCompilerApiTest(unittest.TestCase):
    def test_analyze_document_infers_basic_types(self) -> None:
        result = analyze_document('let count = 1; const name = "x"; let ok = true;')
        types = {s.name: s.type_name for s in result.symbols}
        self.assertEqual(types.get("count"), "Int")
        self.assertEqual(types.get("name"), "String")
        self.assertEqual(types.get("ok"), "Bool")

    def test_analyze_document_reports_unknown_identifier(self) -> None:
        result = analyze_document("let dst = src;")
        messages = [d.message for d in result.diagnostics]
        self.assertTrue(any("Unknown identifier 'src'" == m for m in messages))

    def test_analyze_document_extracts_imports(self) -> None:
        result = analyze_document("import core.io;\nimport pkg.math;\n")
        self.assertEqual(result.imports, ["core.io", "pkg.math"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
