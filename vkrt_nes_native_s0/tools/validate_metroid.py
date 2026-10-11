"""Source hygiene and optional offline lab/replay gate. No ROM access or subprocesses."""
import argparse
import hashlib
import json
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate(root, lab=None, replay=None):
    errors = []
    frozen = json.loads((root / "validation/METROID_FROZEN_SURFACES.json").read_text())
    # The product-policy task explicitly changes controller binding and the old
    # four-mode test expectations. Preserve the historical receipt verbatim;
    # bind just those five authorized paths to their new, reviewable bytes.
    product = json.loads((root / "validation/TWO_MODE_FROZEN_SURFACES.json").read_text())
    frontend_paths = {
        "app/windows/frontend_input.cpp", "app/windows/frontend_input.hpp",
        "tests/test_m5_ui.cpp", "tests/test_m5r1_product.cpp", "tests/test_m5r11_polish.cpp",
    }
    replacements = product["frontend_sha256"]
    if set(replacements) != frontend_paths:
        errors.append("product-policy replacement scope changed")
    if digest(root / "validation/METROID_FROZEN_SURFACES.json") != product["historical_manifest_sha256"]:
        errors.append("historical Metroid receipt changed")
    for name, expected in frozen["sha256"].items():
        if name in frontend_paths:
            expected = replacements.get(name, expected)
        if digest(root / name) != expected:
            errors.append("frozen surface changed: " + name)
    for name, expected in product["frozen_sha256"].items():
        if digest(root / name) != expected:
            errors.append("accepted rendering/core/lab surface changed: " + name)
    for path in root.rglob("*"):
        if not path.is_file() or any(p.startswith(("build", "evidence", "__pycache__")) for p in path.relative_to(root).parts):
            continue
        if path.suffix.lower() in {".nes", ".fds", ".chr", ".sav", ".png", ".ppm", ".bmp"}:
            errors.append("content artifact in product source: " + path.name)
        if path.suffix in {".cpp", ".hpp", ".json", ".md", ".py"}:
            text = path.read_text(encoding="utf-8")
            # Assemble the lab filename fragment so the validator is not its own hit.
            if "[VKRT-" + "LAB-CANONICAL]" in text or "X:" + "\\Downloads" in text:
                errors.append("local input path leaked: " + path.name)
    thresholds = json.loads((root / "validation/METROID_F4_THRESHOLDS.json").read_text())
    if any(thresholds["exact_gates"].values()) or thresholds["perceptual_improvement_threshold"] is not None:
        errors.append("exact gates weakened or perceptual threshold invented")
    if lab:
        report = json.loads((lab / "report.json").read_text())
        if report["schema"] != "vkrt.nes.metroid_f4_lab.v2" or not report["canonical_invariance"]:
            errors.append("lab contract/invariance failed")
        old = {r["frame"]: r for r in thresholds["baseline_measurements"]}
        checkpoints = report["checkpoints"]
        if len(checkpoints) < 3 or sum(r["different_pixels"] > 0 for r in checkpoints) < 2:
            errors.append("insufficient actual checkpoints")
        for row in checkpoints:
            frame = row["frame"]
            if row["canonical_digest_f1"] != row["canonical_digest_f4"] or row["coverage_xor_pixels"] or row["owner_mismatch_pixels"]:
                errors.append("canonical/coverage/owner failed: " + str(frame))
            for field in ("f1_sha256", "baseline_sha256"):
                if row[field] != old[frame][field]:
                    errors.append("pre-change baseline changed: " + field)
            for name in report["diagnostics"]:
                path = lab / f"{frame}-{name}.png"
                if not path.is_file() or path.read_bytes()[:8] != bytes([137, 80, 78, 71, 13, 10, 26, 10]):
                    errors.append("missing PNG diagnostic: " + path.name)
            for mode in ("f1", "f4"):
                if digest(lab / f"{frame}-{mode}.png") != row[mode + "_sha256"]:
                    errors.append("image hash mismatch")
        if replay:
            for path in sorted(lab.iterdir()):
                if path.suffix == ".png" or path.name == "report.json":
                    other = replay / path.name
                    if not other.is_file() or digest(path) != digest(other):
                        errors.append("replay mismatch: " + path.name)
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--lab", type=Path)
    parser.add_argument("--replay", type=Path)
    args = parser.parse_args()
    errors = validate(args.root, args.lab, args.replay)
    print(json.dumps({"result": "FAIL" if errors else "PASS", "errors": errors}, sort_keys=True))
    return bool(errors)


if __name__ == "__main__":
    raise SystemExit(main())
