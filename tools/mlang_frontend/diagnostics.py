"""Minimal syntax diagnostics for scaffold usage."""

from __future__ import annotations

from dataclasses import dataclass

from .parser import parse_document
from .source_map import offset_to_line_char


@dataclass
class LspDiagnostic:
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


def analyze_text(text: str) -> list[LspDiagnostic]:
    diagnostics: list[LspDiagnostic] = []
    open_stack: list[tuple[str, int]] = []
    pairs = {"(": ")", "[": "]", "{": "}"}
    close_to_open = {")": "(", "]": "[", "}": "{"}

    in_string = False
    string_start = -1
    escaped = False

    for idx, ch in enumerate(text):
        if in_string:
            if escaped:
                escaped = False
                continue
            if ch == "\\":
                escaped = True
                continue
            if ch == '"':
                in_string = False
                continue
            if ch == "\n":
                diagnostics.append(
                    LspDiagnostic(
                        start_offset=string_start,
                        end_offset=idx,
                        message="Unterminated string literal",
                    )
                )
                in_string = False
                string_start = -1
            continue

        if ch == '"':
            in_string = True
            string_start = idx
            escaped = False
            continue

        if ch in pairs:
            open_stack.append((ch, idx))
            continue

        if ch in close_to_open:
            if not open_stack:
                diagnostics.append(
                    LspDiagnostic(
                        start_offset=idx,
                        end_offset=idx + 1,
                        message=f"Unmatched '{ch}'",
                    )
                )
                continue

            open_ch, open_idx = open_stack[-1]
            if open_ch != close_to_open[ch]:
                diagnostics.append(
                    LspDiagnostic(
                        start_offset=idx,
                        end_offset=idx + 1,
                        message=(
                            f"Mismatched delimiter: expected '{pairs[open_ch]}', "
                            f"found '{ch}'"
                        ),
                    )
                )
                continue
            open_stack.pop()

    if in_string and string_start >= 0:
        diagnostics.append(
            LspDiagnostic(
                start_offset=string_start,
                end_offset=len(text),
                message="Unterminated string literal",
            )
        )

    for open_ch, open_idx in open_stack:
        diagnostics.append(
            LspDiagnostic(
                start_offset=open_idx,
                end_offset=open_idx + 1,
                message=f"Unclosed '{open_ch}'",
            )
        )

    # Include parser-level diagnostics for declaration syntax.
    for diag in parse_document(text).diagnostics:
        diagnostics.append(
            LspDiagnostic(
                start_offset=diag.start_offset,
                end_offset=diag.end_offset,
                message=diag.message,
            )
        )

    return diagnostics
