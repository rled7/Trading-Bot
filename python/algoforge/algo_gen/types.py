"""Dataclasses and enums for the S1 algorithm generator.

This module defines the canonical types used throughout algo_gen:
  - AlgoManifest         — JSON manifest describing a generated algorithm
  - IndicatorDecl        — one indicator declaration within the manifest
  - EntryRule / ExitRule — DSL-based trading rules
  - RiskSpec             — risk parameters
  - ValidationResult     — output of the 5-stage validator
  - StageResult          — output of a single validation stage
  - ConfidenceScore      — individual axis score
  - TierReport           — composite tier output
  - TierName             — red/orange/yellow/green/white enum
"""
from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Optional


class TierName(str, Enum):
    red    = "red"
    orange = "orange"
    yellow = "yellow"
    green  = "green"
    white  = "white"

    def __str__(self) -> str:          # type: ignore[override]
        return self.value


@dataclass
class IndicatorDecl:
    id:     str
    kind:   str
    params: dict[str, Any] = field(default_factory=dict)


@dataclass
class EntryRule:
    side: str          # "long" | "short"
    when: str          # DSL expression string


@dataclass
class ExitRule:
    side:    str
    when:    Optional[str]   = None   # DSL expression (may be None for bracket-only)
    sl_atr:  Optional[float] = None   # ATR multiplier for stop-loss
    tp_atr:  Optional[float] = None   # ATR multiplier for take-profit


@dataclass
class RiskSpec:
    size:           str   = "atr"     # "atr" | "fixed"
    atr_mult:       float = 1.5
    fixed_lots:     float = 0.01
    max_concurrent: int   = 1
    hedge:          bool  = False
    cool_down_bars: int   = 0


@dataclass
class AlgoManifest:
    schema_version: str
    name:           str
    description:    str
    rationale:      str
    timeframes:     list[str]
    symbols:        Any                       # list[str] | "any"
    indicators:     list[IndicatorDecl]
    entries:        list[EntryRule]
    exits:          list[ExitRule]
    risk:           RiskSpec
    code:           Optional[str] = None      # escape-hatch Python code


@dataclass
class StageResult:
    stage:   int
    name:    str
    passed:  bool
    reason:  str       = ""
    metrics: dict[str, Any] = field(default_factory=dict)


@dataclass
class ConfidenceScore:
    axis:  str
    score: float       # 0–100


@dataclass
class TierReport:
    tier:             TierName
    min_score:        float
    walk_forward:     float
    mc_bootstrap:     float
    robustness:       float
    size_mult:        float
    experimental:     bool
    paper_only:       bool
    stages:           list[StageResult]  = field(default_factory=list)


@dataclass
class ValidationResult:
    passed:  bool
    stages:  list[StageResult]
    report:  Optional[TierReport] = None
    manifest_name: str = ""
