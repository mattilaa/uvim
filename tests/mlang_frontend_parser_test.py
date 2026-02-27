#!/usr/bin/env python3
import unittest

from tools.mlang_frontend.diagnostics import analyze_text
from tools.mlang_frontend.parser import (
    FunctionDecl,
    StaticAssertDecl,
    StaticMessageDecl,
    VariableDecl,
    collect_symbols,
    parse_document,
)


class MlangFrontendParserTest(unittest.TestCase):
    def test_collect_symbols_from_parser(self) -> None:
        source = """
fn main() {
}
let alpha = 1;
const beta = 2;
"""
        symbols = collect_symbols(source)
        self.assertEqual(symbols, ["main", "alpha", "beta"])

    def test_parse_static_declarations(self) -> None:
        source = """
static_assert(1 + 1 == 2);
static_message("N = 4");
"""
        parsed = parse_document(source)
        self.assertEqual(parsed.diagnostics, [])

        kinds = [type(d) for d in parsed.declarations]
        self.assertEqual(kinds, [StaticAssertDecl, StaticMessageDecl])
        self.assertEqual(parsed.declarations[0].expression, "1 + 1 == 2")
        self.assertEqual(parsed.declarations[1].message, '"N = 4"')

    def test_parser_reports_missing_semicolon(self) -> None:
        source = "let value = 10\n"
        parsed = parse_document(source)
        self.assertTrue(any("Expected ';'" in d.message for d in parsed.diagnostics))

    def test_analyze_text_includes_parser_diagnostics(self) -> None:
        source = "const count = 3\n"
        diags = analyze_text(source)
        self.assertTrue(any("Expected ';' after declaration" == d.message for d in diags))

    def test_parse_declaration_shapes(self) -> None:
        source = "fn entry() {}\nlet a = 1;\n"
        parsed = parse_document(source)
        self.assertIsInstance(parsed.declarations[0], FunctionDecl)
        self.assertIsInstance(parsed.declarations[1], VariableDecl)


if __name__ == "__main__":
    unittest.main(verbosity=2)
