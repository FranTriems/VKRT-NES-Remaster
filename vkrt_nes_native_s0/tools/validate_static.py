"""Stdlib-only, read-only source gate. Generated artifacts confer no source authority."""
import argparse
import ast
import json
from pathlib import Path
import re
import sys

REQUIRED = (
    "CMakeLists.txt", "README.md", "ARCHITECTURE.md", "CLAIM_BOUNDARY.md",
    "THIRD_PARTY_BOUNDARY.md", "docs/RESEARCH_BASIS.md", "docs/M0_EVIDENCE_CONTRACT.md",
    "include/vkrt/nes_native_s0/core.hpp", "include/vkrt/nes_native_s0/semantic.hpp",
    "include/vkrt/nes_native_s0/remaster.hpp", "src/core.cpp", "src/semantic.cpp",
    "src/remaster.cpp", "app/demo_main.cpp", "tests/test_main.cpp", "tools/validate_static.py",
    "include/vkrt/nes_native_s0/mmc3.hpp", "src/mmc3.cpp", "tests/test_mmc3.cpp",
    "tools/validate_m1_evidence.py", "docs/M1_EVIDENCE_CONTRACT.md",
    "tests/test_m2.cpp", "docs/M2_EVIDENCE_CONTRACT.md",
    "include/vkrt/nes_native_s0/boards.hpp", "src/boards.cpp",
    "tests/test_m3.cpp", "docs/M3_EVIDENCE_CONTRACT.md",
)
ARTIFACTS = (
    "canonical.ppm", "exact_digital.ppm", "authentic_signal.ppm", "semantic_hd.ppm",
    "semantic.jsonl", "audio.wav", "invariance.json", "summary.json",
)
FORBIDDEN = re.compile(r"\b(?:simplenes|mesen|fceux|nestopia|libretro|lakesnes|sdl\d*|sfml|miniaudio|dlss|fsr)\b", re.I)
NETWORK = re.compile(r"https?://|ftp://|git://|FetchContent|ExternalProject|file\s*\(\s*DOWNLOAD|execute_process\s*\(", re.I)
ROM_SUFFIXES = {".nes", ".fds", ".unf", ".unif"}
CLAIMS = (
    "full NES compatibility", "commercial-game compatibility", "all-mapper",
    "cycle-perfect", "electrically exact NTSC", "production", "DMC", "NROM",
    "TEMPORAL_RECONSTRUCTION", "NEURAL_RESIDUAL_EXPERIMENTAL",
    "PASS_VKRT_NES_NATIVE_S0_M0_SOURCE_BUILD_SEMANTIC_REMASTER_VERTICAL",
)


def lint_build(text):
    errors = []
    allowed = {"cmake_minimum_required", "project", "set", "add_library", "target_include_directories",
               "if", "target_compile_options", "else", "endif", "add_executable", "target_link_libraries",
               "enable_testing", "foreach", "add_test", "endforeach", "find_program"}
    if any(command.lower() not in allowed for command in re.findall(r"\b([A-Za-z_]\w*)\s*\(", text)):
        errors.append("unexpected build command")
    if NETWORK.search(text):
        errors.append("network/external execution in build")
    if FORBIDDEN.search(text):
        errors.append("forbidden dependency in build")
    if re.search(r"\.\.[/\\]|[A-Za-z]:[/\\]|CMAKE_SOURCE_DIR|add_subdirectory\s*\(|find_package\s*\(", text, re.I):
        errors.append("nonlocal build boundary")
    for match in re.finditer(r"target_link_libraries\s*\(([^)]*)\)", text, re.I | re.S):
        words = match.group(1).split()
        if any(word not in {"PRIVATE", "PUBLIC", "INTERFACE", "vkrt_nes_m0"} for word in words[1:]):
            errors.append("unexpected link dependency")
    return errors


def validate(root):
    errors = []
    if not root.is_dir() or root.name != "vkrt_nes_native_s0":
        return ["root must be the vkrt_nes_native_s0 product directory"], 0
    for name in REQUIRED:
        if not (root / name).is_file():
            errors.append("missing required path: " + name)
    source_files = []
    for path in sorted(root.rglob("*")):
        rel = path.relative_to(root)
        if path.is_symlink():
            errors.append("symlink boundary: " + rel.as_posix())
            continue
        if path.suffix.lower() in ROM_SUFFIXES:
            errors.append("forbidden binary ROM: " + rel.as_posix())
        if not path.is_file() or rel.parts[0].startswith(("build", "evidence")) or "__pycache__" in rel.parts:
            continue
        if not path.resolve().is_relative_to(root):
            errors.append("source path escapes product: " + rel.as_posix())
            continue
        source_files.append(path)
        if path.suffix.lower() in {".exe", ".dll", ".so", ".a", ".lib", ".o", ".obj"}:
            errors.append("prebuilt binary in source: " + rel.as_posix())
        if path.suffix in {".cpp", ".hpp", ".h", ".cmake", ".py", ".md"} or path.name in {"CMakeLists.txt", ".gitignore"}:
            data = path.read_bytes()
            text = data.decode("utf-8")
            if data.startswith(b"\xef\xbb\xbf") or b"\r" in data or not data.endswith(b"\n"):
                errors.append("source must be UTF-8 without BOM and LF terminated: " + rel.as_posix())
            if any(line.rstrip(" \t") != line for line in text.splitlines()):
                errors.append("trailing whitespace: " + rel.as_posix())
        if path.suffix in {".cpp", ".hpp", ".h", ".cmake"} or path.name == "CMakeLists.txt":
            text = path.read_text(encoding="utf-8")
            if FORBIDDEN.search(text) or NETWORK.search(text):
                errors.append("forbidden source dependency/network: " + rel.as_posix())
            if rel.parts[0] in {"src", "app", "include"} and re.search(r"SMB3|Super Mario|4377a7f5|SMB3_REV1", text, re.I):
                errors.append("title-specific implementation identity: " + rel.as_posix())
            for include in re.findall(r'#include\s*"([^"]+)"', text):
                target = root / "include" / include
                if not target.resolve().is_relative_to(root) or not target.is_file():
                    errors.append("nonlocal/missing include: " + include)
        if path.suffix == ".py":
            tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(rel))
            for node in ast.walk(tree):
                names = [alias.name for alias in node.names] if isinstance(node, ast.Import) else ([node.module or ""] if isinstance(node, ast.ImportFrom) else [])
                for name in names:
                    base = name.split(".")[0]
                    if base not in sys.stdlib_module_names or base in {"socket", "urllib", "http", "subprocess"}:
                        errors.append("non-stdlib/network/process Python import: " + name)
    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8") if (root / "CMakeLists.txt").is_file() else ""
    errors.extend(lint_build(cmake))
    for target in ("vkrt_nes_m0_demo", "vkrt_nes_m0_tests", "vkrt_nes_m1_tests", "vkrt_nes_m2_tests", "vkrt_nes_m3_tests"):
        if not re.search(r"add_executable\s*\(\s*" + target + r"\b", cmake):
            errors.append("missing CMake executable: " + target)
    if "add_test(" not in cmake or "COMMAND vkrt_nes_m0_tests" not in cmake:
        errors.append("missing CTest registration")
    if not re.search(r"add_test\s*\(\s*NAME\s+vkrt_nes_m3_tests\s+COMMAND\s+vkrt_nes_m3_tests\s*\)", cmake):
        errors.append("missing M3 CTest registration")
    for source in re.findall(r"\b(?:src|app|tests)/[\w/]+\.cpp\b", cmake):
        if not (root / source).is_file():
            errors.append("missing build source: " + source)
    claim = (root / "CLAIM_BOUNDARY.md").read_text(encoding="utf-8") if (root / "CLAIM_BOUNDARY.md").exists() else ""
    for term in CLAIMS:
        if term not in claim:
            errors.append("missing claim boundary: " + term)
    app = (root / "app/demo_main.cpp").read_text(encoding="utf-8") if (root / "app/demo_main.cpp").exists() else ""
    for token in ARTIFACTS + ("--self-test", "--evidence-dir", "core_state_hash_equal", "canonical_frame_hash_equal", "canonical_audio_hash_equal", "iterations", "synthetic_rom_only", "commercial_roms_used"):
        if token not in app:
            errors.append("missing deterministic witness contract: " + token)
    if "synthetic_nrom()" not in app or "is_absolute()" not in app:
        errors.append("missing synthetic witness/absolute path boundary")
    for token in ("--rom", "--frames", "run_summary.json", "rom_sha256", "rom_bytes", "frames_completed",
                  "mapper_irq_assertions", "mapper_irq_acknowledgements", "mapper_prg_bank_writes",
                  "mapper_chr_bank_writes", "unique_canonical_frames", "non_backdrop_pixels",
                  "semantic_samples", "tracked_semantic_samples", "pcm_samples", "claim_scope"):
        if token not in app:
            errors.append("missing M1 ROM runner contract: " + token)
    for name in ("src/remaster.cpp", "src/semantic.cpp", "include/vkrt/nes_native_s0/remaster.hpp", "include/vkrt/nes_native_s0/semantic.hpp"):
        path = root / name
        if path.exists() and re.search(r"#include[^\n]*core\.hpp|\bMachine\s*[*&]", path.read_text(encoding="utf-8")):
            errors.append("presentation-to-machine dependency: " + name)
    # Negative controls keep the dependency gate from being a token-presence placeholder.
    probes = ("FetchContent_Declare(x)", "ExternalProject_Add(x)", 'file(DOWNLOAD "https://example.invalid/a" a)',
              "add_subdirectory(../other)", "find_package(SDL2)", "target_link_libraries(x PRIVATE unknown)")
    if not all(lint_build(probe) for probe in probes):
        errors.append("validator negative control failed")
    return errors, len(source_files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    options = parser.parse_args()
    try:
        errors, count = validate(options.root.resolve())
    except (OSError, ValueError, SyntaxError) as exc:
        errors, count = [str(exc)], 0
    print(json.dumps({"result": "FAIL" if errors else "PASS", "source_files": count,
                      "source_rom_files_allowed": False, "errors": errors}, sort_keys=True))
    return bool(errors)


if __name__ == "__main__":
    sys.exit(main())
