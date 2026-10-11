# Research basis and provenance

No web/network research was performed in M0 or M1. The hardware basis is public
specification knowledge: iNES/NES 2.0 header concepts; official NMOS 6502
instructions/address cycles with RP2A03 binary-only arithmetic; CPU RAM/PPU/APU
address decoding; NTSC 3:1 PPU scheduling; PPU v/t/x/w scroll registers,
nametable/attribute/pattern fetches, sprite priority; NES controller serial latch;
and pulse/triangle/noise timer/envelope/frame-sequencer concepts. These are design
bases, not newly acquired external test-oracle evidence. External conformance
suites and physical measurements remain future validation.

The following first-party local materials were inspected read-only during M0
(historical provenance, not newly repeated research in M1):

| Material | Pattern transferred | Boundary |
| --- | --- | --- |
| `products/vkrt_snes_core_c0/include/vkrt/snes/state.hpp`, `cpu_trace.hpp`, CMake/tests | explicit-endian canonical serialization, bus traces, small deterministic checks | no SNES CPU or adapter implementation reused |
| `products/vkrt_retro_remaster_s0/docs/ARCHITECTURE.md` | immutable machine snapshot then presentation | historical external core dependency excluded |
| `products/vkrt_retro_remaster_s1/docs/S1_ARCHITECTURE.md` | contextual motifs, equal-color regions, HUD protection | no renderer/runtime dependency |
| `products/vkrt_retro_remaster_s1_1/include/vkrt_retro_s1_1/cmsr.hpp` | identity/region/material observations | no copied remaster implementation |
| `products/vkrt_retro_remaster_v1/include/vkrt_retro_remaster_v1/temporal_ir.hpp` | reactive states, motion evidence, ambiguity/gap rejection | semantic motion only; no accumulation claim |
| `products/vkrt_retro_native_hd_r0/include/vkrt_retro_native_hd_r0/coverage.hpp` | explicit ownership accounting | no third-party core path used |
| `products/vkrt_retro_native_remaster_adapter_a0/CLAIM_BOUNDARY.md` | snapshot equivalence vs renderer/compatibility claims | no adapter imported |
| `products/vkrt_retro_native_remaster_adapter_a1/include/vkrt_retro_native_remaster_adapter_a1/adapter.hpp` | owned sprite records, valid lifetimes | no emulator pointers exposed |
| `products/vkrt_neural_reconstruction_s0/include/vkrt_nr_s0/frame_contract.hpp` | current-to-previous motion and reactive/history boundaries | no neural runtime/training |
| `products/vkrt_kotor_remaster_mr0/include/vkrt_kotor_mr0/draw_packet.hpp` | fingerprints are hypotheses, duplicates reject history | no gameplay identity authority |
| `products/vkrt_nucleus_s0/ARCHITECTURE_FREEZE_R1.md`, README frame/ACK references | host-owned semantic lineage | future host consumes existing Frame; no alternate UI truth |
| `docs/332_VKRT_GPU_VULKAN_SYNC_AND_MEMORY_CONTRACTS.md` | declared producer/consumer hazards need execution evidence | CPU only; no Vulkan/parity claim |

The implementation intentionally freezes M0 at three deterministic CPU references.
Neural residuals, if added in a later authorized pass, may affect high-frequency
appearance only and cannot own geometry, silhouettes, HUD/text or low-frequency
structure. Temporal reconstruction must consume explicit semantic correspondence
and disocclusion, with confidence rejection where source identity is ambiguous.

No commercial ROM, third-party emulator implementation or proprietary game asset
was read, copied, hashed, inspected, linked, or used as a test oracle for M0.

M1 uses the task's frozen engineering basis: MMC3 PRG/CHR register layout,
Sharp/new counter-zero/reload behavior, and three falling M2 edges of low-A12
history. No new external documentation, source dump, emulator or ROM database
was consulted. Digital M2/address event ordering is explicit and independently
stimulated by synthetic tests; it is not a measured analog model. The authorized
operator ROM is used only after those synthetic gates pass, as a bounded external
behavioral witness. See `MMC3_ARCHITECTURE_SEAM.md` and `M1_LOCAL_VALIDATION.md`.

## M2 validation provenance

M2 used no network, dependency installation, other emulator, external source code,
or subagent. Digital hardware knowledge informed the implementation; the runner's
excluded ROM corpus was executed only as black-box validation. Result messages,
cycle-visible bus observations and a read-only nametable probe are observations,
not production inputs. No corpus binary was copied into the product. Synthetic
programs in `tests/test_m2.cpp` are original first-party stimuli. R0.4.2 additionally
consulted the supplied pinned diagnostic assembly/readmes and read-only bus traces;
no external emulator implementation was used. The corrected-suite 37/37 Tier A
outcome and remaining qualification limits are recorded in `M2_EVIDENCE_CONTRACT.md`.
