"""AST nodes for the minimal mlang parse-only frontend."""

from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class LetDecl:
    name: str
    start_offset: int
    end_offset: int


@dataclass
class FunctionDecl:
    name: str
    start_offset: int
    end_offset: int
    lets: list[LetDecl] = field(default_factory=list)


@dataclass
class Module:
    functions: list[FunctionDecl] = field(default_factory=list)
    top_level_lets: list[LetDecl] = field(default_factory=list)
    diagnostics: list[str] = field(default_factory=list)

