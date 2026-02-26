"""UTF-8/UTF-16 source position helpers for LSP operations."""

from __future__ import annotations


def _line_start_offsets(text: str) -> list[int]:
    starts = [0]
    for idx, ch in enumerate(text):
        if ch == "\n":
            starts.append(idx + 1)
    return starts


def utf16_units(s: str) -> int:
    # UTF-16 code units = bytes/2 in utf-16-le representation.
    return len(s.encode("utf-16-le")) // 2


def line_char_to_offset(text: str, line: int, character_utf16: int) -> int:
    line = max(line, 0)
    character_utf16 = max(character_utf16, 0)

    starts = _line_start_offsets(text)
    if line >= len(starts):
        return len(text)

    start = starts[line]
    end = starts[line + 1] if line + 1 < len(starts) else len(text)
    segment = text[start:end]

    units = 0
    for i, ch in enumerate(segment):
        ch_units = utf16_units(ch)
        if units + ch_units > character_utf16:
            return start + i
        units += ch_units
    return end


def offset_to_line_char(text: str, offset: int) -> tuple[int, int]:
    offset = max(0, min(offset, len(text)))
    starts = _line_start_offsets(text)

    line = 0
    for i, s in enumerate(starts):
        if s > offset:
            break
        line = i

    line_start = starts[line]
    character = utf16_units(text[line_start:offset])
    return line, character

