"""algo_gen — S1 AI Algorithm Generator (Python reference implementation).

Public API (LLM-free portion):
  Types:
    AlgoManifest, IndicatorDecl, EntryRule, ExitRule, RiskSpec
    ValidationResult, StageResult, ConfidenceScore
    TierReport, TierName

  Schema:
    validate_manifest(d: dict) -> AlgoManifest

  DSL:
    compile_expr(expr, indicator_ids) -> Callable
    PATTERN_NAMES, BAR_FIELDS

  Runtime:
    ManifestAlgo(manifest) -> IAlgorithm

  Sandbox:
    run_escape_hatch(manifest, bars) -> BTResult
    check_code_security(code) -> None
    SandboxSecurityError

  Validator:
    validate(manifest_dict, bars, seed) -> ValidationResult

  Tier:
    score_to_tier(walk_forward, mc, robustness) -> TierReport

  Promote:
    promote(report, manifest, registry_dir) -> ManifestAlgo
    PromotionError

  Robustness:
    parameter_robustness(manifest, bars, seed) -> float

  Fixtures:
    load_bars, load_manifest, load_expected_tier_report
    load_llm_response, MultiTurnMockTransport

  Generator (LLM-driven):
    generate_fast(brief, provider, seed) -> (AlgoManifest, GenerationTrace)
    generate_balanced(brief, provider, seed) -> (AlgoManifest, GenerationTrace)
    generate_max(brief, provider, seed, n_candidates) -> (AlgoManifest, GenerationTrace)
    GenerationTrace, GenerationError
"""
from .types import (
    AlgoManifest,
    ConfidenceScore,
    EntryRule,
    ExitRule,
    IndicatorDecl,
    RiskSpec,
    StageResult,
    TierName,
    TierReport,
    ValidationResult,
)
from .schema import validate_manifest, INDICATOR_KINDS, VALID_TIMEFRAMES
from .dsl import compile_expr, PATTERN_NAMES, BAR_FIELDS
from .runtime import ManifestAlgo
from .sandbox import (
    run_escape_hatch,
    check_code_security,
    SandboxSecurityError,
    FORBIDDEN_NAMES,
)
from .validator import validate
from .tier import score_to_tier
from .promote import promote, PromotionError
from .robustness import parameter_robustness
from .fixtures import (
    load_bars,
    load_manifest,
    load_expected_tier_report,
    load_llm_response,
    MultiTurnMockTransport,
)
from .generator import (
    generate_fast,
    generate_balanced,
    generate_max,
    GenerationTrace,
    GenerationError,
    CandidateSummary,
    CritiquePair,
)

__all__ = [
    # types
    "AlgoManifest", "ConfidenceScore", "EntryRule", "ExitRule",
    "IndicatorDecl", "RiskSpec", "StageResult", "TierName", "TierReport",
    "ValidationResult",
    # schema
    "validate_manifest", "INDICATOR_KINDS", "VALID_TIMEFRAMES",
    # dsl
    "compile_expr", "PATTERN_NAMES", "BAR_FIELDS",
    # runtime
    "ManifestAlgo",
    # sandbox
    "run_escape_hatch", "check_code_security", "SandboxSecurityError",
    "FORBIDDEN_NAMES",
    # validator
    "validate",
    # tier
    "score_to_tier",
    # promote
    "promote", "PromotionError",
    # robustness
    "parameter_robustness",
    # fixtures
    "load_bars", "load_manifest", "load_expected_tier_report",
    "load_llm_response", "MultiTurnMockTransport",
    # generator
    "generate_fast", "generate_balanced", "generate_max",
    "GenerationTrace", "GenerationError", "CandidateSummary", "CritiquePair",
]
