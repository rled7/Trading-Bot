"""CLI entry point for algo_gen.

Usage:
    python -m algoforge.algo_gen validate <manifest.json>
    python -m algoforge.algo_gen promote  <manifest.json> [--confirm]
    python -m algoforge.algo_gen list     [--tier green]
    python -m algoforge.algo_gen retire   <algo-name>
    python -m algoforge.algo_gen generate ...   # NotImplementedError (Agent 2)
"""
from __future__ import annotations

import argparse
import json
import os
import sys


def _find_registry_dir() -> str:
    """Find the registry directory relative to the repo root."""
    repo_root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.dirname(os.path.abspath(__file__))
    )))
    return os.path.join(repo_root, "registry")


def cmd_validate(args: argparse.Namespace) -> int:
    """Validate a manifest JSON file against all 5 stages."""
    from .fixtures import load_bars
    from .validator import validate

    manifest_path = args.manifest
    if not os.path.exists(manifest_path):
        print(f"Error: manifest file not found: {manifest_path}", file=sys.stderr)
        return 1

    with open(manifest_path) as f:
        manifest_dict = json.load(f)

    # Load canonical bars
    fixture_name = getattr(args, "bars", "bars_eurusd_h1_2y.json")
    try:
        bars = load_bars(fixture_name)
    except FileNotFoundError:
        print(f"Error: bar fixture '{fixture_name}' not found", file=sys.stderr)
        return 1

    seed = getattr(args, "seed", 42)

    print(f"Validating '{manifest_dict.get('name', '?')}' ...")
    result = validate(manifest_dict, bars, seed)

    for stage in result.stages:
        status = "PASS" if stage.passed else "FAIL"
        print(f"  Stage {stage.stage} [{status}] {stage.name}: {stage.reason}")

    if result.passed and result.report:
        r = result.report
        print(f"\nResult: PASS — tier={r.tier.value}  min_score={r.min_score:.2f}")
        print(f"  WF={r.walk_forward:.2f}  MC={r.mc_bootstrap:.2f}  Rob={r.robustness:.2f}")
        print(f"  size_mult={r.size_mult}  experimental={r.experimental}  paper_only={r.paper_only}")
        return 0
    else:
        print(f"\nResult: FAIL")
        return 2


def cmd_promote(args: argparse.Namespace) -> int:
    """Promote a validated manifest into the registry."""
    from .fixtures import load_bars
    from .promote import PromotionError, promote
    from .validator import validate

    manifest_path = args.manifest
    if not os.path.exists(manifest_path):
        print(f"Error: manifest file not found: {manifest_path}", file=sys.stderr)
        return 1

    with open(manifest_path) as f:
        manifest_dict = json.load(f)

    fixture_name = getattr(args, "bars", "bars_eurusd_h1_2y.json")
    try:
        bars = load_bars(fixture_name)
    except FileNotFoundError:
        print(f"Error: bar fixture '{fixture_name}' not found", file=sys.stderr)
        return 1

    seed    = getattr(args, "seed", 42)
    confirm = getattr(args, "confirm", False)

    print(f"Validating '{manifest_dict.get('name', '?')}' ...")
    result = validate(manifest_dict, bars, seed)

    if not result.passed or result.report is None:
        print("Validation FAILED — cannot promote")
        for stage in result.stages:
            if not stage.passed:
                print(f"  Failed at stage {stage.stage}: {stage.reason}")
        return 2

    # Reconstruct manifest for promotion
    from .schema import validate_manifest as _vm
    manifest = _vm(manifest_dict)

    registry_dir = getattr(args, "out", _find_registry_dir())
    try:
        algo = promote(
            result.report, manifest, registry_dir,
            confirm=confirm,
        )
        print(
            f"Promoted '{manifest.name}' → tier={result.report.tier.value}  "
            f"size_mult={result.report.size_mult}"
        )
        print(f"  Manifest written to: {algo.metadata['manifest_path']}")
        return 0
    except PromotionError as e:
        print(f"Promotion refused: {e}", file=sys.stderr)
        return 3


def cmd_list(args: argparse.Namespace) -> int:
    """List all registered algorithms with tier info."""
    registry_dir = getattr(args, "registry", _find_registry_dir())
    tier_filter  = getattr(args, "tier", None)

    if not os.path.isdir(registry_dir):
        print(f"Registry directory not found: {registry_dir}")
        return 0

    found = 0
    for fname in sorted(os.listdir(registry_dir)):
        if not fname.endswith(".json"):
            continue
        fpath = os.path.join(registry_dir, fname)
        try:
            with open(fpath) as f:
                d = json.load(f)
            meta = d.get("_metadata", {})
            tier = meta.get("tier", "?")
            if tier_filter and tier != tier_filter:
                continue
            name      = d.get("name", fname[:-5])
            score     = meta.get("min_score", "?")
            exp_flag  = "(experimental)" if meta.get("experimental") else ""
            paper_flag= "(paper-only)"   if meta.get("paper_only")   else ""
            print(f"  {name:40s}  tier={tier:<8s}  min_score={score!s:<7}  {exp_flag}{paper_flag}")
            found += 1
        except Exception as e:
            print(f"  [{fname}] error reading: {e}")

    if found == 0:
        msg = f"No algorithms" + (f" with tier={tier_filter}" if tier_filter else "")
        print(f"{msg} found in {registry_dir}")
    return 0


def cmd_retire(args: argparse.Namespace) -> int:
    """Retire (disable) an algorithm by setting is_enabled=false."""
    algo_name    = args.name
    registry_dir = getattr(args, "registry", _find_registry_dir())

    fpath = os.path.join(registry_dir, f"{algo_name}.json")
    if not os.path.exists(fpath):
        # Try case-insensitive search
        for fname in os.listdir(registry_dir) if os.path.isdir(registry_dir) else []:
            if fname.lower() == f"{algo_name.lower()}.json":
                fpath = os.path.join(registry_dir, fname)
                break
        else:
            print(f"Error: algorithm '{algo_name}' not found in {registry_dir}", file=sys.stderr)
            return 1

    with open(fpath) as f:
        d = json.load(f)

    if d.get("is_enabled") is False:
        print(f"'{algo_name}' is already retired.")
        return 0

    d["is_enabled"] = False
    with open(fpath, "w") as f:
        json.dump(d, f, indent=2)

    print(f"Retired '{algo_name}' (is_enabled=false written to {fpath})")
    return 0


def cmd_generate(args: argparse.Namespace) -> int:
    """Generate a new algorithm using the LLM pipeline. (Not yet implemented.)"""
    raise NotImplementedError("S1 Phase 1 Agent 2 will add this")


# ── Argument parser ───────────────────────────────────────────────────────────

def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="python -m algoforge.algo_gen",
        description="AlgoForge S1 — AI Algorithm Generator CLI",
    )
    sub = parser.add_subparsers(dest="command", metavar="COMMAND")

    # validate
    p_val = sub.add_parser("validate", help="Validate a manifest JSON file")
    p_val.add_argument("manifest", help="Path to manifest JSON file")
    p_val.add_argument("--bars",   default="bars_eurusd_h1_2y.json",
                       help="Bar fixture filename (default: bars_eurusd_h1_2y.json)")
    p_val.add_argument("--seed",   type=int, default=42, help="RNG seed")

    # promote
    p_pro = sub.add_parser("promote", help="Promote a validated manifest into the registry")
    p_pro.add_argument("manifest", help="Path to manifest JSON file")
    p_pro.add_argument("--confirm", action="store_true",
                       help="Required for orange/yellow tier promotion")
    p_pro.add_argument("--out",  default=None,
                       help="Registry directory (default: repo_root/registry/)")
    p_pro.add_argument("--bars", default="bars_eurusd_h1_2y.json")
    p_pro.add_argument("--seed", type=int, default=42)

    # list
    p_lst = sub.add_parser("list", help="List registered algorithms")
    p_lst.add_argument("--tier",     default=None, help="Filter by tier name")
    p_lst.add_argument("--registry", default=None, help="Registry directory path")

    # retire
    p_ret = sub.add_parser("retire", help="Retire (disable) an algorithm")
    p_ret.add_argument("name", help="Algorithm name to retire")
    p_ret.add_argument("--registry", default=None, help="Registry directory path")

    # generate (stub)
    p_gen = sub.add_parser("generate", help="[Not yet implemented] Generate via LLM")
    p_gen.add_argument("--brief",  default="", help="Natural-language brief")
    p_gen.add_argument("--mode",   default="balanced", choices=["fast", "balanced", "max"])
    p_gen.add_argument("--seed",   type=int, default=42)
    p_gen.add_argument("--out",    default=None)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = _build_parser()
    args   = parser.parse_args(argv)

    if args.command is None:
        parser.print_help()
        return 0

    dispatch = {
        "validate": cmd_validate,
        "promote":  cmd_promote,
        "list":     cmd_list,
        "retire":   cmd_retire,
        "generate": cmd_generate,
    }

    fn = dispatch.get(args.command)
    if fn is None:
        print(f"Unknown command: {args.command}", file=sys.stderr)
        return 1

    return fn(args)


if __name__ == "__main__":
    sys.exit(main())
