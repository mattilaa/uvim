"""Minimal parse-only frontend for extracting declarations."""

from __future__ import annotations

from dataclasses import dataclass

from .ast import FunctionDecl, LetDecl, Module
from .lexer import Token, lex


@dataclass
class ParseResult:
    module: Module
    tokens: list[Token]


class _Parser:
    def __init__(self, text: str) -> None:
        self.text = text
        self.tokens = lex(text)
        self.pos = 0
        self.module = Module()

    def _peek(self) -> Token:
        return self.tokens[self.pos]

    def _at_end(self) -> bool:
        return self._peek().kind == "eof"

    def _advance(self) -> Token:
        tok = self._peek()
        if not self._at_end():
            self.pos += 1
        return tok

    def _match_keyword(self, value: str) -> bool:
        tok = self._peek()
        if tok.kind == "keyword" and tok.text == value:
            self._advance()
            return True
        return False

    def _match_symbol(self, value: str) -> bool:
        tok = self._peek()
        if tok.kind == "symbol" and tok.text == value:
            self._advance()
            return True
        return False

    def _consume_identifier(self, context: str) -> Token | None:
        tok = self._peek()
        if tok.kind == "identifier":
            self._advance()
            return tok
        self.module.diagnostics.append(f"Expected identifier after {context}")
        return None

    def parse(self) -> ParseResult:
        while not self._at_end():
            if self._match_keyword("fn"):
                self._parse_function()
            elif self._match_keyword("let"):
                let = self._parse_let_after_keyword()
                if let is not None:
                    self.module.top_level_lets.append(let)
            else:
                self._advance()
        return ParseResult(module=self.module, tokens=self.tokens)

    def _parse_function(self) -> None:
        name_tok = self._consume_identifier("fn")
        if name_tok is None:
            return

        fn = FunctionDecl(
            name=name_tok.text, start_offset=name_tok.start, end_offset=name_tok.end
        )

        if self._match_symbol("("):
            depth = 1
            while not self._at_end() and depth > 0:
                if self._match_symbol("("):
                    depth += 1
                    continue
                if self._match_symbol(")"):
                    depth -= 1
                    continue
                self._advance()

        if self._match_symbol("{"):
            depth = 1
            while not self._at_end() and depth > 0:
                if self._match_keyword("let"):
                    let = self._parse_let_after_keyword()
                    if let is not None:
                        fn.lets.append(let)
                    continue
                if self._match_symbol("{"):
                    depth += 1
                    continue
                if self._match_symbol("}"):
                    depth -= 1
                    continue
                self._advance()

        fn.end_offset = self.tokens[max(0, self.pos - 1)].end
        self.module.functions.append(fn)

    def _parse_let_after_keyword(self) -> LetDecl | None:
        name_tok = self._consume_identifier("let")
        if name_tok is None:
            return None
        return LetDecl(
            name=name_tok.text, start_offset=name_tok.start, end_offset=name_tok.end
        )


def parse_module(text: str) -> ParseResult:
    return _Parser(text).parse()


def collect_symbols(text: str) -> list[str]:
    result = parse_module(text).module
    seen: set[str] = set()
    out: list[str] = []

    def add(name: str) -> None:
        if name not in seen:
            seen.add(name)
            out.append(name)

    for fn in result.functions:
        add(fn.name)
        for let in fn.lets:
            add(let.name)
    for let in result.top_level_lets:
        add(let.name)
    return out

