"""Offline M4-R2 receipt and four-rung contact sheet; Python standard library only."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import struct
import zlib

LEGACY = ("canonical.ppm", "exact_digital.ppm", "authentic_signal.ppm", "semantic_hd.ppm",
          "audio.wav", "semantic.jsonl", "invariance.json")
MODERN = ("modern_remaster.ppm", "modern_material.ppm", "modern_height.ppm",
          "modern_normal.ppm", "modern_masks.ppm", "modern_repeat.json")
RUNGS = ("exact_digital", "authentic_signal", "semantic_hd", "modern_remaster")
# First-party five-column lettering for generated comparison headings only.
FONT = {
    "A": (14,17,17,31,17,17,17), "C": (14,17,16,16,16,17,14),
    "D": (30,17,17,17,17,17,30), "E": (31,16,16,30,16,16,31),
    "G": (14,17,16,23,17,17,15), "H": (17,17,17,31,17,17,17),
    "I": (31,4,4,4,4,4,31), "L": (16,16,16,16,16,16,31),
    "M": (17,27,21,21,17,17,17), "N": (17,25,21,19,17,17,17),
    "O": (14,17,17,17,17,17,14), "R": (30,17,17,30,20,18,17),
    "S": (15,16,16,14,1,1,30), "T": (31,4,4,4,4,4,4),
    "U": (17,17,17,17,17,17,14), "X": (17,17,10,4,10,17,17),
    " ": (0,0,0,0,0,0,0),
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def ppm(path):
    data = path.read_bytes()
    magic, size, maximum, rgb = data.split(b"\n", 3)
    width, height = map(int, size.split())
    if magic != b"P6" or maximum != b"255" or len(rgb) != width * height * 3:
        raise ValueError("invalid bounded PPM: " + path.name)
    return width, height, rgb


def png(path, width, height, rgb):
    def chunk(kind, payload):
        return struct.pack("!I", len(payload)) + kind + payload + struct.pack("!I", zlib.crc32(kind + payload) & 0xffffffff)
    scanlines = b"".join(b"\0" + rgb[y*width*3:(y+1)*width*3] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack("!2I5B", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b""))


def comparison(candidate):
    images = [ppm(candidate / (name + ".ppm")) for name in RUNGS]
    width, height = images[0][:2]
    if any(im[:2] != (width, height) for im in images):
        raise ValueError("comparison extents differ")
    gap, header = 8, 44
    wide = 4 * width + 3 * gap
    data = bytearray(bytes((19,23,30)) * wide * (height + header))
    for column, (name, (_, _, rgb)) in enumerate(zip(RUNGS, images)):
        left = column * (width + gap)
        png(candidate / (name + ".png"), width, height, rgb)
        for y in range(height):
            start = ((y + header) * wide + left) * 3
            data[start:start+width*3] = rgb[y*width*3:(y+1)*width*3]
        title = name.replace("_", " ").upper()
        origin = left + (width - len(title)*12) // 2
        for n, letter in enumerate(title):
            for yy, row in enumerate(FONT[letter]):
                for xx in range(5):
                    if row & (1 << (4-xx)):
                        for a in range(2):
                            for b in range(2):
                                k = ((15 + yy*2+a)*wide + origin + n*12 + xx*2+b)*3
                                data[k:k+3] = bytes((235,241,248))
    png(candidate / "comparison.png", wide, height + header, data)
    for name in MODERN[:5]:
        w, h, rgb = ppm(candidate / name)
        png(candidate / Path(name).with_suffix(".png"), w, h, rgb)
    figures = "".join('<figure><figcaption>' + name.replace("_", " ").upper()
                      + '</figcaption><img src="' + name + '.png" alt="' + name
                      + '"></figure>' for name in RUNGS)
    diagnostics = "".join('<figure><figcaption>' + name.replace("_", " ")
                          + '</figcaption><img src="' + name + '.png" alt="' + name + '"></figure>'
                          for name in ("modern_material", "modern_height", "modern_normal", "modern_masks"))
    page = '''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>VKRT M4-R2 four-rung comparison</title>
<style>body{background:#13171e;color:#edf1f8;font:15px system-ui;margin:24px}
h1{font-size:22px}p{color:#b5c2d4}main{display:grid;grid-template-columns:repeat(4,minmax(240px,1fr));gap:8px;overflow:auto}
figure{margin:0}figcaption{font-size:12px;letter-spacing:.07em;margin:12px 0}img{display:block;width:100%;height:auto}
a{color:#9ed1ff}details{margin-top:24px}summary{cursor:pointer}</style>
<h1>Illustrated semantic material remaster</h1>
<p>Same immutable frame. Default structure = 1, tone = 1. Lighting and materials are presentation only.</p>
<main>''' + figures + '''</main><p><a href="comparison.png">Full-size four-column image</a>
 &middot; <a href="m4r2_receipt.json">Local verification receipt</a></p>
<details><summary>Material, height, normal and semantic masks</summary><main>''' + diagnostics + '''</main>
<p>Masks: red = protected/UI; green = actor or structure strength; blue = world or tone strength.
Material colors are categorical hypotheses, not recovered physical materials.</p></details>
<p>Candidate scope: ''' + html.escape(candidate.name) + '''. Human visual acceptance remains separate from deterministic tests.</p></html>
'''
    (candidate / "comparison.html").write_text(page, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--repeat", type=Path)
    parser.add_argument("--source-manifest", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    errors, files, repeat = [], {}, {}
    for name in LEGACY + tuple(n for n in ("summary.json", "run_summary.json") if (args.baseline / n).exists()):
        before, after = (args.baseline / name).read_bytes(), (args.candidate / name).read_bytes()
        files[name] = {"equal": before == after, "baseline_sha256": digest(before), "candidate_sha256": digest(after)}
        if before != after:
            errors.append("legacy bytes changed: " + name)
    for name in MODERN:
        data = (args.candidate / name).read_bytes()
        files[name] = {"candidate_sha256": digest(data)}
        if args.repeat:
            repeat[name] = data == (args.repeat / name).read_bytes()
            if not repeat[name]:
                errors.append("repeat bytes changed: " + name)
    manifest = json.loads(args.source_manifest.read_text())
    allowed = {"CMakeLists.txt", "README.md", "ARCHITECTURE.md", "CLAIM_BOUNDARY.md", "THIRD_PARTY_BOUNDARY.md",
               "app/demo_main.cpp", "include/vkrt/nes_native_s0/remaster.hpp", "src/remaster.cpp"}
    source_changes, frozen = [], 0
    for name, expected in manifest.items():
        actual = digest((args.root / name).read_bytes())
        if actual != expected:
            source_changes.append(name)
            if name not in allowed:
                errors.append("frozen source changed: " + name)
        elif name not in allowed:
            frozen += 1
    renderer = (args.root / "src/remaster.cpp").read_bytes()
    renderer = renderer.replace(b'#include "vkrt/nes_native_s0/modern_assets.hpp"\n', b'')
    renderer = renderer.replace(b'    if(mode==Mode::MODERN_REMASTER)return render_modern(f,{},w,h);\n', b'')
    renderer_equal = digest(renderer) == manifest["src/remaster.cpp"]
    enum_header = (args.root / "include/vkrt/nes_native_s0/remaster.hpp").read_bytes().replace(b', MODERN_REMASTER };', b' };')
    enum_equal = digest(enum_header) == manifest["include/vkrt/nes_native_s0/remaster.hpp"]
    if not renderer_equal or not enum_equal:
        errors.append("historical renderer/header changed beyond additive dispatch and enum append")
    source_bytes = {}
    for path in sorted(args.root.rglob("*")):
        relative = path.relative_to(args.root)
        if not path.is_file() or any(p.startswith(("build", "evidence")) or p == "__pycache__" for p in relative.parts):
            continue
        name = relative.as_posix()
        source_bytes[name] = digest(path.read_bytes())
        if name not in manifest and not (name.startswith(("include/vkrt/nes_native_s0/modern_", "src/modern_",
                                                         "tests/test_m4r2", "docs/M4R2_", "tools/m4r2_", "shaders/m4r2_"))):
            errors.append("new source outside allowed surface: " + name)
    _, _, hd = ppm(args.candidate / "semantic_hd.ppm")
    _, _, modern = ppm(args.candidate / "modern_remaster.ppm")
    differences = [abs(a-b) for a, b in zip(hd, modern)]
    receipt = {"schema": "VKRT_M4R2_LOCAL_COMPARISON", "result": "FAIL" if errors else "PASS",
               "files": files, "independent_repeat": repeat, "errors": errors,
               "incoming_source_manifest_sha256": digest(args.source_manifest.read_bytes()),
               "frozen_source_files_verified": frozen, "modified_incoming_source_paths": source_changes,
               "legacy_renderer_body_equal": renderer_equal, "legacy_enum_prefix_equal": enum_equal,
               "candidate_source_sha256": source_bytes,
               "visual_observations_not_acceptance": {"mean_absolute_channel_delta": sum(differences)/len(differences),
                                                       "maximum_channel_delta": max(differences)},
               "claim_scope": "exact controlled witness only; no population compatibility or human visual acceptance"}
    (args.candidate / "m4r2_receipt.json").write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    comparison(args.candidate)
    print(json.dumps({"result": receipt["result"], "legacy_files": len(files)-len(MODERN),
                      "repeat_files": len(repeat), "frozen_source_files": frozen, "errors": errors}, sort_keys=True))
    return bool(errors)


if __name__ == "__main__":
    raise SystemExit(main())
