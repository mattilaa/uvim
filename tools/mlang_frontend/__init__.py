"""Minimal frontend utilities used by the mlang LSP scaffold."""

from .parser import collect_symbols, parse_module

__all__ = ["collect_symbols", "parse_module"]
