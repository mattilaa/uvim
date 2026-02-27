"""Minimal frontend utilities used by the mlang LSP scaffold."""

from .parser import collect_symbols, parse_document

__all__ = ["collect_symbols", "parse_document"]
