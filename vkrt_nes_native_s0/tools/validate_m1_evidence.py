"""Read-only M1 artifact gate; expected ROM identity is supplied by the operator."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import wave

NAMES = ("canonical.ppm", "exact_digital.ppm", "authentic_signal.ppm", "semantic_hd.ppm",
         "semantic.jsonl", "audio.wav", "invariance.json", "run_summary.json")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def ppm(path, extent):
    magic, dimensions, maximum, rgb = path.read_bytes().split(b"\n", 3)
    require(magic == b"P6" and maximum == b"255", "PPM header")
    require(tuple(map(int, dimensions.split())) == extent, "PPM dimensions")
    require(len(rgb) == extent[0] * extent[1] * 3, "PPM byte count")
    return rgb


def summary_gate(summary, expected_sha, expected_bytes, frames):
    require(summary["result"] == "PASS" and summary["commercial_rom_used"] is True, "ROM result")
    require(summary["rom_sha256"] == expected_sha and summary["rom_bytes"] == expected_bytes, "operator ROM identity")
    require(summary["mapper"] == 4 and summary["frames_requested"] == summary["frames_completed"] == frames, "mapper/frame completion")
    require(summary["cpu_halted"] is False, "CPU must remain running")
    for field in ("mapper_irq_assertions", "mapper_irq_acknowledgements", "mapper_prg_bank_writes", "mapper_chr_bank_writes"):
        require(type(summary[field]) is int and summary[field] > 0, "mapper activity: " + field)
    require(2 <= summary["unique_canonical_frames"] <= frames, "unique canonical frames")
    require(summary["non_backdrop_pixels"] > 1000 and summary["semantic_samples"] == 61440, "meaningful final frame")
    require(0 <= summary["tracked_semantic_samples"] <= 61440 and summary["pcm_samples"] > 0, "semantic/PCM counts")
    require(bool(summary["claim_scope"]), "bounded claim scope")


def validate(first, second, expected_sha, expected_bytes, frames):
    require(first.resolve() != second.resolve(), "two independent output directories required")
    require(re.fullmatch(r"[0-9a-f]{64}", expected_sha) is not None, "expected SHA-256 syntax")
    require(1 <= frames <= 600 and expected_bytes > 16, "expected run bounds")
    hashes = {}
    for directory in (first, second):
        require({p.name for p in directory.iterdir()} == set(NAMES), "exact eight-file ROM evidence contract")
    for name in NAMES:
        a, b = first / name, second / name
        require(a.is_file() and b.is_file() and a.stat().st_size <= 128 * 1024 * 1024, "artifact bound: " + name)
        hashes[name] = digest(a)
        require(a.stat().st_size == b.stat().st_size and hashes[name] == digest(b), "repeat mismatch: " + name)
    summary = json.loads((first / "run_summary.json").read_text(encoding="utf-8"))
    summary_gate(summary, expected_sha, expected_bytes, frames)
    # Negative controls for the summary gate, independent of output generation.
    for key, bad in {"result": "FAIL", "rom_sha256": "0" * 64, "mapper": 0, "frames_completed": 0,
                     "cpu_halted": True, "mapper_irq_assertions": 0, "mapper_irq_acknowledgements": 0,
                     "mapper_prg_bank_writes": 0, "mapper_chr_bank_writes": 0,
                     "unique_canonical_frames": 1, "non_backdrop_pixels": 0, "pcm_samples": 0}.items():
        probe = dict(summary, **{key: bad})
        rejected = False
        try:
            summary_gate(probe, expected_sha, expected_bytes, frames)
        except ValueError:
            rejected = True
        require(rejected, "gate negative control: " + key)
    invariant = json.loads((first / "invariance.json").read_text(encoding="utf-8"))
    for field in ("core_state_hash_equal", "canonical_frame_hash_equal", "canonical_audio_hash_equal", "presentation_repeat_equal"):
        require(invariant[field] is True, "invariance: " + field)
    for field in ("core_state_sha256", "canonical_frame_sha256", "canonical_audio_sha256"):
        require(re.fullmatch(r"[0-9a-f]{64}", invariant[field]) is not None, "canonical hash syntax")
    canonical = ppm(first / "canonical.ppm", (256, 240))
    exact = ppm(first / "exact_digital.ppm", (768, 720))
    signal = ppm(first / "authentic_signal.ppm", (768, 720))
    hd = ppm(first / "semantic_hd.ppm", (768, 720))
    require(len(set(canonical[i:i+3] for i in range(0, len(canonical), 3))) > 1, "nonconstant canonical image")
    require(exact != signal and exact != hd, "distinct presentation rungs")
    for y in range(240):
        row = b"".join(canonical[(y*256+x)*3:(y*256+x+1)*3] * 3 for x in range(256))
        for offset in range(3):
            require(exact[(y*3+offset)*768*3:(y*3+offset+1)*768*3] == row, "independent nearest-scaling oracle")
    with wave.open(str(first / "audio.wav"), "rb") as audio:
        require((audio.getnchannels(), audio.getsampwidth(), audio.getframerate()) == (1, 2, 44100), "WAV format")
        require(audio.getnframes() == summary["pcm_samples"] <= 441000, "PCM count/bound")
        pcm = audio.readframes(audio.getnframes())
    require(hashlib.sha256(pcm).hexdigest() == invariant["canonical_audio_sha256"], "PCM canonical hash binding")
    require(len(pcm) > 0, "nonempty PCM witness")
    indices, effects = bytearray(), bytearray()
    regions, sources = {}, set()
    tracked = non_backdrop = count = 0
    with (first / "semantic.jsonl").open(encoding="utf-8") as stream:
        header = json.loads(next(stream))
        require(header["type"] == "frame" and header["mapper_domain"] == 4 and header["frame"] == frames, "semantic mapper/frame")
        require(header["width"] == 256 and header["height"] == 240 and header["sample_count"] == 61440, "semantic extent")
        for line in stream:
            item = json.loads(line)
            if item["type"] == "region":
                require(item["id"] == len(regions)+1 and item["area"] > 0, "region ordering")
                regions[item["id"]] = item
                continue
            require(item["type"] == "sample" and item["x"] == count % 256 and item["scanline"] == count // 256, "sample coverage")
            require(item["dot"] == item["x"]+1 and item["frame"] == frames, "sample timing")
            region = regions[item["region"]]
            require(region["color"] == item["palette_index"] and region["asset_key"] == item["asset_key"], "region provenance")
            require(int(item["asset_key"]) != 0 and int(item["surface_key"]) != 0, "semantic keys")
            require(item["winner"] in (0, 1, 2), "semantic layer")
            if item["winner"]:
                source = item["background"] if item["winner"] == 1 else item["sprite"]
                sources.add(source["chr_source"])
                require(int(source["chr_source"]) != 0 and source["high_fetch_epoch"] >= source["fetch_epoch"] > 0, "mapped CHR fetch provenance")
            non_backdrop += item["winner"] != 0
            tracked += item["confidence"] > 0
            indices.append(item["palette_index"])
            effects.append((item["emphasis"] << 5) | int(item["grayscale"]))
            count += 1
    require(count == sum(r["area"] for r in regions.values()) == summary["semantic_samples"], "sample/region cardinality")
    require(non_backdrop == summary["non_backdrop_pixels"] and tracked == summary["tracked_semantic_samples"], "summary semantic counts")
    require(len(sources) > 1, "multiple actually mapped CHR sources")
    require(hashlib.sha256(struct.pack("<HH", 256, 240)+indices+effects).hexdigest() == invariant["canonical_frame_sha256"], "semantic/canonical hash binding")
    return {"result": "PASS", "compared_files": len(NAMES), "frames": frames,
            "mapped_chr_sources": len(sources), "semantic_samples": count, "sha256": hashes,
            "pcm_nonzero_samples": sum(v != 0 for v in struct.unpack("<" + "h" * (len(pcm)//2), pcm)),
            "claim_scope": "local evidence gate; independent source/build adjudication remains separate"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--first", type=Path, required=True)
    parser.add_argument("--second", type=Path, required=True)
    parser.add_argument("--expected-sha256", required=True)
    parser.add_argument("--expected-bytes", type=int, required=True)
    parser.add_argument("--frames", type=int, required=True)
    args = parser.parse_args()
    try:
        result = validate(args.first, args.second, args.expected_sha256, args.expected_bytes, args.frames)
    except (OSError, ValueError, KeyError, StopIteration, TypeError, wave.Error) as exc:
        print(json.dumps({"result": "FAIL", "error": str(exc)}, sort_keys=True))
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
