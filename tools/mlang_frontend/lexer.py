"""Minimal lexer for a parse-only mlang frontend."""

from __future__ import annotations

from dataclasses import dataclass


KEYWORDS = {"fn", "let", "mut", "return", "if", "else", "while", "for"}


@dataclass(frozen=True)
class Token:
    kind: str
    text: str
    start: int
    end: int


def _is_ident_start(ch: str) -> bool:
    return ch == "_" or ch.isalpha()


def _is_ident_part(ch: str) -> bool:
    return ch == "_" or ch.isalnum()


def lex(text: str) -> list[Token]:
    tokens: list[Token] = []
    i = 0
    n = len(text)

    while i < n:
        ch = text[i]
        if ch.isspace():
            i += 1
            continue

        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            i += 2
            while i < n and text[i] != "\n":
                i += 1
            continue

        if _is_ident_start(ch):
            start = i
            i += 1
            while i < n and _is_ident_part(text[i]):
                i += 1
            value = text[start:i]
            kind = "keyword" if value in KEYWORDS else "identifier"
            tokens.append(Token(kind, value, start, i))
            continue

        if ch.isdigit():
            start = i
            i += 1
            while i < n and text[i].isdigit():
                i += 1
            tokens.append(Token("number", text[start:i], start, i))
            continue

        if ch == '"':
            start = i
            i += 1
            escaped = False
            while i < n:
                c = text[i]
                if escaped:
                    escaped = False
                    i += 1
                    continue
                if c == "\\":
                    escaped = True
                    i += 1
                    continue
                if c == '"':
                    i += 1
                    break
                i += 1
            tokens.append(Token("string", text[start:i], start, i))
            continue

        if ch in "{}()[],;=:+-*/":
            tokens.append(Token("symbol", ch, i, i + 1))
            i += 1
            continue

        tokens.append(Token("unknown", ch, i, i + 1))
        i += 1

    tokens.append(Token("eof", "", n, n))
    return tokens

