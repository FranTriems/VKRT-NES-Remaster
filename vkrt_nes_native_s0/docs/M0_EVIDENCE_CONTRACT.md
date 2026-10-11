# M0 evidence contract

All source/build/test changes and local generated outputs for this pass stay
under `products/vkrt_nes_native_s0/`. No Git mutation, network, package fetch,
host provisioning, commercial-ROM access or external agent is needed.

## Required gates

1. Offline standalone CMake/Ninja C++20 build.
2. CTest executable `vkrt_nes_m0_tests`: cartridge, CPU, bus, PPU, APU, semantic,
   presentation, invariance and bounds suites. The CPU oracle exhausts both
   carries/all operands for binary ADC and SBC, and checks official/unofficial
   decode coverage separately. Static checks are also registered when a local
   Python interpreter is found.
3. `python tools/validate_static.py --root <PRODUCT_ROOT>`.
4. `vkrt_nes_m0_demo --self-test --evidence-dir <ABSOLUTE_DIR>`.
5. Repeat the demo in a second product-local directory and run the stdlib-only
   evidence validator. It parses the formats and compares every artifact byte.

Every gate must report its own result; a failed test must not be converted to a
PASS by an image or build. The independent runner owns final adjudication. Maximum
allowed adjudicated label is
`PASS_VKRT_NES_NATIVE_S0_M0_SOURCE_BUILD_SEMANTIC_REMASTER_VERTICAL`.

## Deterministic artifacts

| File | Contract |
| --- | --- |
| `canonical.ppm` | P6 RGB, 256x240, canonical palette/effect mapping |
| `exact_digital.ppm` | P6 RGB, 768x720, exact 3x nearest replication |
| `authentic_signal.ppm` | P6 RGB, 768x720, bounded integer signal-inspired reference |
| `semantic_hd.ppm` | P6 RGB, 768x720, source-region contour reference |
| `semantic.jsonl` | one frame header, deterministic row-major regions, all 61440 visible sample records with both sources, keys, motion/confidence/reactive fields |
| `audio.wav` | RIFF/WAVE PCM, mono 44100 Hz, 16-bit little-endian, CPU-generated nonconstant samples |
| `invariance.json` | computed core_state_hash_equal, canonical_frame_hash_equal, canonical_audio_hash_equal, presentation_repeat_equal, iterations=2, mode_runs=6, frames_per_run=8, three SHA-256 digests |
| `summary.json` | computed result PASS/FAIL, synthetic_rom_only=true, commercial_roms_used=false, meaningful-witness counts and bounded scope |

`iterations` means two independent runs of EACH implemented presentation mode,
compared against an additional presentation-free baseline, with the same ROM
and eight-frame input sequence. At every frame, observation/presentation are
bracketed by canonical byte/hash checks. Tests also change controller input to
prove these hashes and the visual witness are not constant placeholders.
Hashes are lowercase SHA-256 of schema-defined bytes; they are not Git blob hashes.
Identity keys in JSON are decimal strings to preserve all 64 bits in consumers.

No timestamps, UUIDs, absolute paths, compiler names, durations or environment
values occur in compared artifact contents. Demo failure returns a nonzero exit
code. Unexpected exceptions do not synthesize PASS records. Reusing an evidence
directory after failure can leave prior files; the runner must honor exit code
and use separate fresh directories for evidence, as the local procedure does.

The optional local validation record is a narrative receipt of actual commands
and counts, not a source oracle or external compatibility claim. Build/evidence
directories remain generated/local and are ignored by source control.
