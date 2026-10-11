"""Parse the witness formats and compare all eight outputs from independent demo processes."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import wave

NAMES = ("canonical.ppm", "exact_digital.ppm", "authentic_signal.ppm", "semantic_hd.ppm",
         "semantic.jsonl", "audio.wav", "invariance.json", "summary.json")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def ppm(path):
    magic, extent, maximum, rgb = path.read_bytes().split(b"\n", 3)
    w, h = map(int, extent.split())
    require(magic == b"P6" and maximum == b"255", "invalid PPM header")
    require(0 < w <= 2048 and 0 < h <= 1920 and len(rgb) == w * h * 3, "invalid PPM body")
    return w, h, rgb


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def validate(first, second):
    hashes = {}
    for name in NAMES:
        a, b = first / name, second / name
        require(a.is_file() and b.is_file(), "missing artifact: " + name)
        require(a.stat().st_size <= 128 * 1024 * 1024, "artifact size bound")
        hashes[name] = digest(a)
        require(hashes[name] == digest(b) and a.stat().st_size == b.stat().st_size, "repeated-run mismatch: " + name)
    invariant = json.loads((first / "invariance.json").read_text(encoding="utf-8"))
    summary = json.loads((first / "summary.json").read_text(encoding="utf-8"))
    require(summary["result"] == "PASS" and summary["synthetic_rom_only"] is True and summary["commercial_roms_used"] is False, "summary contract")
    for flag in ("core_state_hash_equal", "canonical_frame_hash_equal", "canonical_audio_hash_equal", "presentation_repeat_equal"):
        require(invariant[flag] is True, "invariance failure: " + flag)
    require(invariant["iterations"] >= 2 and invariant["mode_runs"] >= 6, "repeat count")
    cw, ch, canonical = ppm(first / "canonical.ppm")
    ew, eh, exact = ppm(first / "exact_digital.ppm")
    aw, ah, signal = ppm(first / "authentic_signal.ppm")
    hw, hh, hd = ppm(first / "semantic_hd.ppm")
    require((cw, ch) == (256, 240) and (ew, eh) == (768, 720), "canonical/exact dimensions")
    require((aw, ah) == (768, 720) and hw > 256 and hh > 240, "remaster dimensions")
    require(exact != signal and exact != hd, "distinct presentation references")
    for y in range(240):
        row = b"".join(canonical[(y * 256 + x) * 3:(y * 256 + x + 1) * 3] * 3 for x in range(256))
        for offset in range(3):
            require(exact[(y * 3 + offset) * 768 * 3:(y * 3 + offset + 1) * 768 * 3] == row, "nearest integer oracle")
    with wave.open(str(first / "audio.wav"), "rb") as audio:
        require((audio.getnchannels(), audio.getsampwidth(), audio.getframerate()) == (1, 2, 44100), "WAV format")
        pcm = audio.readframes(audio.getnframes())
    require(len(pcm) > 1000 and len(set(struct.unpack("<" + "h" * (len(pcm) // 2), pcm))) > 8, "meaningful PCM")
    require(hashlib.sha256(pcm).hexdigest() == invariant["canonical_audio_sha256"], "WAV binds canonical audio hash")
    indices, effects = bytearray(), bytearray()
    regions, area, layers, tracked, behind, count = {}, 0, set(), 0, 0, 0
    with (first / "semantic.jsonl").open(encoding="utf-8") as stream:
        header = json.loads(next(stream))
        require(header["type"] == "frame" and header["sample_count"] == 61440, "semantic frame header")
        for line in stream:
            record = json.loads(line)
            if record["type"] == "region":
                require(record["id"] == len(regions) + 1 and record["area"] > 0, "region cardinality")
                regions[record["id"]] = record
                area += record["area"]
                continue
            require(record["type"] == "sample" and record["x"] == count % 256 and record["scanline"] == count // 256, "sample ordering/coverage")
            require(record["dot"] == record["x"] + 1 and record["frame"] == header["frame"], "sample timing provenance")
            require(record["region"] in regions and regions[record["region"]]["color"] == record["palette_index"], "sample region binding")
            require(int(record["asset_key"]) != 0 and int(record["surface_key"]) != 0, "real semantic keys")
            for source in (record["background"], record["sprite"]):
                require(all(field in source for field in ("nametable", "attribute", "pattern", "tile", "palette", "local", "oam_slot", "flags", "scroll_v", "fine_x", "chr_source", "chr_epoch", "high_chr_epoch", "fetch_epoch", "high_fetch_epoch")), "source packet fields")
            indices.append(record["palette_index"])
            effects.append((record["emphasis"] << 5) | int(record["grayscale"]))
            layers.add(record["winner"])
            tracked += record["confidence"] > 0
            behind += bool(record["bg_opaque"] and record["sprite_opaque"] and record["sprite_behind"])
            count += 1
    require(count == area == 61440 and len(regions) > 10 and {1, 2} <= layers and tracked > 100 and behind > 0, "meaningful semantic composition/occlusion")
    require(hashlib.sha256(struct.pack("<HH", 256, 240) + indices + effects).hexdigest() == invariant["canonical_frame_sha256"], "semantic packet binds canonical frame hash")
    return {"result": "PASS", "compared_files": len(hashes), "semantic_samples": count,
            "regions": len(regions), "tracked_samples": tracked, "behind_overlap_samples": behind, "sha256": hashes}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--first", type=Path, required=True)
    parser.add_argument("--second", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = validate(args.first, args.second)
    except (OSError, ValueError, KeyError, StopIteration, wave.Error) as exc:
        print(json.dumps({"result": "FAIL", "error": str(exc)}, sort_keys=True))
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
