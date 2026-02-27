"""Minimal frontend utilities used by the mlang LSP scaffold."""

from .compiler_api import analyze_document
from .parser import collect_symbols, parse_document

__all__ = ["collect_symbols", "parse_document", "analyze_document"]
