"""Minimal parser utilities for scaffolded mlang frontend features.

This parser is intentionally lightweight: it extracts top-level declarations
and reports basic syntax diagnostics useful for editor features.
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class ParserDiagnostic:
    start_offset: int
    end_offset: int
    message: str


@dataclass(frozen=True)
class FunctionDecl:
    name: str
    start_offset: int
    end_offset: int


@dataclass(frozen=True)
class VariableDecl:
    kind: str
    name: str
    start_offset: int
    end_offset: int


@dataclass(frozen=True)
class StaticAssertDecl:
    expression: str
    start_offset: int
    end_offset: int


@dataclass(frozen=True)
class StaticMessageDecl:
    message: str
    start_offset: int
    end_offset: int


@dataclass(frozen=True)
class ParseResult:
    declarations: list[object]
    diagnostics: list[ParserDiagnostic]


def parse_document(text: str) -> ParseResult:
    declarations: list[object] = []
    diagnostics: list[ParserDiagnostic] = []
    i = 0
    n = len(text)

    def skip_ws(idx: int) -> int:
        while idx < n and text[idx].isspace():
            idx += 1
        return idx

    def is_ident_start(ch: str) -> bool:
        return ch.isalpha() or ch == "_"

    def is_ident_part(ch: str) -> bool:
        return ch.isalnum() or ch == "_"

    def read_ident(idx: int) -> tuple[str, int]:
        if idx >= n or not is_ident_start(text[idx]):
            return "", idx
        start = idx
        idx += 1
        while idx < n and is_ident_part(text[idx]):
            idx += 1
        return text[start:idx], idx

    def skip_line_comment(idx: int) -> int:
        idx += 2
        while idx < n and text[idx] != "\n":
            idx += 1
        return idx

    def skip_block_comment(idx: int) -> int:
        idx += 2
        while idx + 1 < n:
            if text[idx] == "*" and text[idx + 1] == "/":
                return idx + 2
            idx += 1
        diagnostics.append(
            ParserDiagnostic(idx - 2, n, "Unterminated block comment")
        )
        return n

    def skip_string(idx: int) -> int:
        quote = text[idx]
        idx += 1
        escaped = False
        while idx < n:
            ch = text[idx]
            if escaped:
                escaped = False
                idx += 1
                continue
            if ch == "\\":
                escaped = True
                idx += 1
                continue
            if ch == quote:
                return idx + 1
            if ch == "\n":
                diagnostics.append(
                    ParserDiagnostic(idx - 1, idx, "Unterminated string literal")
                )
                return idx
            idx += 1
        diagnostics.append(
            ParserDiagnostic(idx - 1, n, "Unterminated string literal")
        )
        return n

    def skip_balanced(idx: int, open_ch: str, close_ch: str) -> tuple[int, int]:
        depth = 0
        start = idx
        while idx < n:
            ch = text[idx]
            nxt = text[idx + 1] if idx + 1 < n else ""
            if ch == "/" and nxt == "/":
                idx = skip_line_comment(idx)
                continue
            if ch == "/" and nxt == "*":
                idx = skip_block_comment(idx)
                continue
            if ch in ('"', "'"):
                idx = skip_string(idx)
                continue
            if ch == open_ch:
                depth += 1
            elif ch == close_ch:
                depth -= 1
                if depth == 0:
                    return idx + 1, idx
            idx += 1
        diagnostics.append(
            ParserDiagnostic(start, n, f"Unclosed '{open_ch}'")
        )
        return n, n

    def consume_until_semicolon(idx: int) -> int:
        while idx < n:
            ch = text[idx]
            nxt = text[idx + 1] if idx + 1 < n else ""
            if ch == ";":
                return idx + 1
            if ch == "/" and nxt == "/":
                idx = skip_line_comment(idx)
                continue
            if ch == "/" and nxt == "*":
                idx = skip_block_comment(idx)
                continue
            if ch in ('"', "'"):
                idx = skip_string(idx)
                continue
            if ch == "(":
                idx, _ = skip_balanced(idx, "(", ")")
                continue
            if ch == "{":
                idx, _ = skip_balanced(idx, "{", "}")
                continue
            idx += 1
        return idx

    while i < n:
        i = skip_ws(i)
        if i >= n:
            break

        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if ch == "/" and nxt == "/":
            i = skip_line_comment(i)
            continue
        if ch == "/" and nxt == "*":
            i = skip_block_comment(i)
            continue
        if ch in ('"', "'"):
            i = skip_string(i)
            continue

        ident, j = read_ident(i)
        if not ident:
            i += 1
            continue

        if ident == "fn":
            k = skip_ws(j)
            name, k2 = read_ident(k)
            if not name:
                diagnostics.append(
                    ParserDiagnostic(k, min(k + 1, n), "Expected function name")
                )
                i = j
                continue
            # Skip parameter list if present.
            p = skip_ws(k2)
            if p < n and text[p] == "(":
                p, _ = skip_balanced(p, "(", ")")
            p = skip_ws(p)
            end = p
            if p < n and text[p] == "{":
                end, _ = skip_balanced(p, "{", "}")
            declarations.append(FunctionDecl(name=name, start_offset=i, end_offset=end))
            i = max(end, p)
            continue

        if ident in ("let", "const"):
            k = skip_ws(j)
            name, k2 = read_ident(k)
            if not name:
                diagnostics.append(
                    ParserDiagnostic(k, min(k + 1, n), "Expected variable name")
                )
                i = j
                continue
            end = consume_until_semicolon(k2)
            if end <= k2 or text[end - 1] != ";":
                diagnostics.append(
                    ParserDiagnostic(k2, min(k2 + 1, n), "Expected ';' after declaration")
                )
            declarations.append(
                VariableDecl(kind=ident, name=name, start_offset=i, end_offset=end)
            )
            i = end
            continue

        if ident in ("static_assert", "static_message"):
            k = skip_ws(j)
            if k >= n or text[k] != "(":
                diagnostics.append(
                    ParserDiagnostic(k, min(k + 1, n), "Expected '('")
                )
                i = j
                continue
            end_paren, close_idx = skip_balanced(k, "(", ")")
            inside = text[k + 1 : close_idx] if close_idx >= k + 1 else ""
            end = skip_ws(end_paren)
            if end >= n or text[end] != ";":
                diagnostics.append(
                    ParserDiagnostic(end, min(end + 1, n), "Expected ';' after call")
                )
                end = consume_until_semicolon(end)
            else:
                end += 1
            if ident == "static_assert":
                declarations.append(
                    StaticAssertDecl(
                        expression=inside.strip(), start_offset=i, end_offset=end
                    )
                )
            else:
                declarations.append(
                    StaticMessageDecl(
                        message=inside.strip(), start_offset=i, end_offset=end
                    )
                )
            i = end
            continue

        i = j

    return ParseResult(declarations=declarations, diagnostics=diagnostics)


def collect_symbols(text: str) -> list[str]:
    """Collect unique symbol names from declaration keywords across the file."""

    n = len(text)
    i = 0
    seen: set[str] = set()
    out: list[str] = []

    def skip_string(idx: int) -> int:
        quote = text[idx]
        idx += 1
        escaped = False
        while idx < n:
            ch = text[idx]
            if escaped:
                escaped = False
                idx += 1
                continue
            if ch == "\\":
                escaped = True
                idx += 1
                continue
            if ch == quote:
                return idx + 1
            idx += 1
        return n

    def skip_line_comment(idx: int) -> int:
        idx += 2
        while idx < n and text[idx] != "\n":
            idx += 1
        return idx

    def skip_block_comment(idx: int) -> int:
        idx += 2
        while idx + 1 < n:
            if text[idx] == "*" and text[idx + 1] == "/":
                return idx + 2
            idx += 1
        return n

    def is_ident_start(ch: str) -> bool:
        return ch.isalpha() or ch == "_"

    def is_ident_part(ch: str) -> bool:
        return ch.isalnum() or ch == "_"

    def read_ident(idx: int) -> tuple[str, int]:
        if idx >= n or not is_ident_start(text[idx]):
            return "", idx
        start = idx
        idx += 1
        while idx < n and is_ident_part(text[idx]):
            idx += 1
        return text[start:idx], idx

    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if ch == "/" and nxt == "/":
            i = skip_line_comment(i)
            continue
        if ch == "/" and nxt == "*":
            i = skip_block_comment(i)
            continue
        if ch in ('"', "'"):
            i = skip_string(i)
            continue
        ident, j = read_ident(i)
        if not ident:
            i += 1
            continue
        if ident in ("fn", "let", "const"):
            k = j
            while k < n and text[k].isspace():
                k += 1
            name, k2 = read_ident(k)
            if name and name not in seen:
                seen.add(name)
                out.append(name)
            i = max(j, k2)
            continue
        i = j

    return out
