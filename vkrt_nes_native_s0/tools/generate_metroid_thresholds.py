"""Convert a measured pre-change baseline receipt into reviewable acceptance data."""
import argparse
import hashlib
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    raw = args.baseline.read_bytes()
    baseline = json.loads(raw)
    rows = baseline["checkpoints"]
    if len(rows) < 3 or len({r["frame"] for r in rows}) != len(rows):
        raise ValueError("Need three or more distinct measured checkpoints")
    result = {
        "schema": "vkrt.nes.metroid_f4_thresholds.v1",
        "baseline_source_head": "831e03987195b1eda167145ca731560e8fac2f1d",
        "baseline_receipt_sha256": hashlib.sha256(raw).hexdigest(),
        "baseline_renderer": "unmodified retained R5.2; 512x480; GNU 15.2 Release",
        "baseline_measurements": rows,
        "baseline_distribution": {
            "different_pixels_min": min(r["different_pixels"] for r in rows),
            "different_pixels_max": max(r["different_pixels"] for r in rows),
            "mean_channel_delta_min": min(r["mean_abs_channel_delta"] for r in rows),
            "mean_channel_delta_max": max(r["mean_abs_channel_delta"] for r in rows),
            "shade_median_ms_min": min(r["median_ms"] for r in rows),
            "shade_median_ms_max": max(r["median_ms"] for r in rows),
        },
        "exact_gates": {
            "canonical_digest_mismatches": 0,
            "coverage_xor_pixels": 0,
            "owner_mismatch_pixels": 0,
            "replay_byte_mismatches": 0,
            "f1_baseline_hash_mismatches": 0,
            "retained_baseline_hash_mismatches": 0,
            "steady_geometry_rebuilds": 0,
        },
        "task_contract_minimum": {"actual_checkpoints": 3, "nonidentical_checkpoints": 2},
        "perceptual_improvement_threshold": None,
        "perceptual_acceptance": "Inspect F1/baseline/F4 and diagnostic distributions; human adjudication required. Pixel difference is not a quality score.",
        "performance_acceptance": "Report paired local timings, no fabricated universal performance threshold.",
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
