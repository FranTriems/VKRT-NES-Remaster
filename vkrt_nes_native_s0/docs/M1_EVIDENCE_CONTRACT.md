# M1 arbitrary-ROM runner contract

`vkrt_nes_m0_demo --rom <ROM_PATH> --frames <1..600> --evidence-dir <ABSOLUTE_DIR>`

The ROM is read once into bounded owned memory (maximum 16+512+32 MiB before
iNES validation). Header and exact payload length are validated, followed by
mapper-specific limits. No filename, title, checksum, asset or program address
selects machine behavior. Both controllers start released. Execution uses the
same first-party C++20 Machine as the retained M0 self-test.

Two canonical executions use the owned input: one observes and renders the last
two frames, one executes without presentation. Full state bytes and frame/audio
hashes bracket observation/rendering. Each mode repeats with byte-equal pixels.
Every requested frame contributes to the unique-frame metric. No ROM payload,
absolute input path, state dump or host timestamp is emitted. Serialization is
hashed in memory only.

A completed run emits exactly these files:

| File | Meaning |
| --- | --- |
| canonical.ppm | canonical fixed-palette 256x240 P6 image |
| exact_digital.ppm | exact 3x nearest integer scaling |
| authentic_signal.ppm | independent 768x720 signal-inspired presentation |
| semantic_hd.ppm | independent 768x720 semantic presentation |
| semantic.jsonl | immutable frame, regions and 61440 source-aware samples |
| audio.wav | 44100 Hz, mono signed-16 PCM from canonical APU |
| invariance.json | four required booleans and canonical hashes |
| run_summary.json | identity, execution/activity counts and bounded result |

Summary fields are result, commercial_rom_used, rom_sha256, rom_bytes, mapper,
frames_requested/completed, cpu_halted, mapper_irq_assertions,
mapper_irq_acknowledgements, mapper_prg_bank_writes, mapper_chr_bank_writes,
unique_canonical_frames, non_backdrop_pixels, semantic_samples,
tracked_semantic_samples, pcm_samples and claim_scope. External input is
conservatively marked `commercial_rom_used: true`; the runner does not classify
ownership or detect games. Non-backdrop counts background/sprite-winning samples,
not merely colors unlike the dominant color. IRQ metrics count actual asserted
level transitions and acknowledgements of pending IRQs. Bank metrics count
bank-data writes, including repeated values.

Generic PASS requires requested frames completed, running CPU, at least two
frame hashes, over 1000 non-backdrop samples, multiple palette colors, nonempty
semantics/PCM and every invariance check. Mapper 4 additionally requires positive
PRG/CHR writes and IRQ assertions/acknowledgements. Valid short or intentionally
blank ROMs can execute successfully yet receive FAIL as compatibility witnesses.
Runtime faults produce FAIL; partial artifacts exist only when a frame exists.
Invalid input fails before evidence generation. Use a fresh directory per process.

`validate_m1_evidence.py` compares all eight files from independent processes by
size/SHA-256, checks operator-supplied identity/size/frame count, parses formats,
independently checks exact scaling, binds PCM/semantic bytes to canonical hashes,
and checks semantic counts and multiple mapped CHR sources. Summary negative
controls execute in memory. The parser has no built-in game hash. It reports
nonzero PCM sample count separately; silence is not evidence of audible output.

The complete M1 label also requires source/build, synthetic and unchanged M0
gates. M0's CLI and `summary.json` remain unchanged. No temporal renderer or
neural runtime is added. Generated evidence is local output, never source authority.

## DMC/audio repair gate

A commercial audio witness must exercise real game-driven APU/DMC register writes, mapper-backed DMC fetches when enabled, nonconstant signed-16 PCM, deterministic A/B output, and unchanged presentation invariance. The M1 DMC DMA stall model is deterministic and bounded; cycle-exact get/put parity and controller collision quirks are not claimed.
