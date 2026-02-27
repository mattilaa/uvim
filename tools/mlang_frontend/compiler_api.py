"""Compiler frontend facade used by the scaffolded LSP server.

Phase 3 target: route editor features through one semantic API surface instead
of ad-hoc regex/lexical logic.
"""

from __future__ import annotations

import re
from dataclasses import dataclass

from .diagnostics import analyze_text
from .parser import collect_symbols
from .source_map import offset_to_line_char


@dataclass(frozen=True)
class SemanticDiagnostic:
    start_offset: int
    end_offset: int
    message: str
    severity: int = 1
    source: str = "mlang"

    def to_lsp(self, text: str) -> dict:
        start_line, start_char = offset_to_line_char(text, self.start_offset)
        end_line, end_char = offset_to_line_char(text, self.end_offset)
        return {
            "range": {
                "start": {"line": start_line, "character": start_char},
                "end": {"line": end_line, "character": end_char},
            },
            "severity": self.severity,
            "source": self.source,
            "message": self.message,
        }


@dataclass(frozen=True)
class SymbolInfo:
    name: str
    kind: str
    type_name: str


@dataclass(frozen=True)
class SemanticResult:
    symbols: list[SymbolInfo]
    diagnostics: list[SemanticDiagnostic]


def _infer_expr_type(expr: str, known_types: dict[str, str]) -> str:
    expr = expr.strip()
    if not expr:
        return "Unknown"
    if re.fullmatch(r"[0-9]+", expr):
        return "Int"
    if re.fullmatch(r'"(?:\\.|[^"\\])*"', expr):
        return "String"
    if expr in ("true", "false"):
        return "Bool"
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", expr):
        return known_types.get(expr, "Unknown")
    return "Unknown"


def analyze_document(text: str) -> SemanticResult:
    diagnostics = [
        SemanticDiagnostic(
            start_offset=d.start_offset,
            end_offset=d.end_offset,
            message=d.message,
            severity=d.severity,
            source=d.source,
        )
        for d in analyze_text(text)
    ]

    symbols: list[SymbolInfo] = []
    known_names: set[str] = set()
    known_types: dict[str, str] = {}

    for fn_name in re.findall(r"\bfn\s+([A-Za-z_][A-Za-z0-9_]*)", text):
        if fn_name not in known_names:
            known_names.add(fn_name)
            known_types[fn_name] = "Function"
            symbols.append(SymbolInfo(name=fn_name, kind="function", type_name="Function"))

    decl_pat = re.compile(
        r"\b(let|const)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*([^;\n]+))?\s*;"
    )
    for m in decl_pat.finditer(text):
        kind, name, expr = m.group(1), m.group(2), m.group(3) or ""
        type_name = _infer_expr_type(expr, known_types)
        if name not in known_names:
            known_names.add(name)
            known_types[name] = type_name
            symbols.append(SymbolInfo(name=name, kind=kind, type_name=type_name))

        expr_ident = expr.strip()
        if expr_ident and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", expr_ident):
            if expr_ident not in known_types and expr_ident not in ("true", "false"):
                start = m.start(3) if m.start(3) >= 0 else m.start(2)
                diagnostics.append(
                    SemanticDiagnostic(
                        start_offset=start,
                        end_offset=start + len(expr_ident),
                        message=f"Unknown identifier '{expr_ident}'",
                    )
                )

    # Keep parser symbol discovery as a fallback for declarations we do not
    # model semantically yet.
    for name in collect_symbols(text):
        if name not in known_names:
            known_names.add(name)
            symbols.append(SymbolInfo(name=name, kind="symbol", type_name="Unknown"))

    return SemanticResult(symbols=symbols, diagnostics=diagnostics)

