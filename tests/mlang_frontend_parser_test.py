#!/usr/bin/env python3
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.mlang_frontend.lexer import lex
from tools.mlang_frontend.parser import collect_symbols, parse_module


class MlangFrontendParserTest(unittest.TestCase):
    def test_lexer_emits_keywords_identifiers_and_symbols(self) -> None:
        toks = lex("fn main() { let value = 1 }")
        kinds = [(t.kind, t.text) for t in toks[:8]]
        self.assertEqual(
            kinds,
            [
                ("keyword", "fn"),
                ("identifier", "main"),
                ("symbol", "("),
                ("symbol", ")"),
                ("symbol", "{"),
                ("keyword", "let"),
                ("identifier", "value"),
                ("symbol", "="),
            ],
        )

    def test_parse_module_extracts_function_and_let_symbols(self) -> None:
        src = "let top = 1\nfn main() { let value = top }\n"
        parsed = parse_module(src).module
        self.assertEqual([f.name for f in parsed.functions], ["main"])
        self.assertEqual([l.name for l in parsed.top_level_lets], ["top"])
        self.assertEqual([l.name for l in parsed.functions[0].lets], ["value"])

    def test_collect_symbols_unique_stable_order(self) -> None:
        src = "fn a() { let z = 1 let z = 2 }\nlet g = 1\nfn b() {}\n"
        self.assertEqual(collect_symbols(src), ["a", "z", "b", "g"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
