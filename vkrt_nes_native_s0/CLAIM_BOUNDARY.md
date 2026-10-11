# M0/M1/M2/M3 promoted floors / bounded M4-R presentation candidate

The M4-R task binds promoted M3 parent `9106f25d870fc5481b698aaadc25bf517967cad5`.
Historical milestone claims below retain their original qualification. M4-R adds
no hardware compatibility or promotion claim; its local presentation evidence is
recorded in [the M4-R contract](docs/M4R_REMASTER_EVIDENCE_CONTRACT.md).

Maximum candidate claim, conditional on every independent runner gate passing:

`PASS_VKRT_NES_NATIVE_S0_M0_SOURCE_BUILD_SEMANTIC_REMASTER_VERTICAL`

M0 is the closed/promoted regression floor supplied by the task. Its tests and
self-test contract are retained. The additional maximum M1 claim, conditional
on the complete source/build/synthetic/authorized-ROM/evidence gates, is:

`PASS_VKRT_NES_NATIVE_S0_M1_MMC3_SMB3_REMASTER_VERTICAL`

The local M1 candidate executed the authorized witness for 600 released-controller
frames in two processes. This is a bounded boot/title-animation witness, not a
playthrough. Exact identity and measurements are in `docs/M1_LOCAL_VALIDATION.md`.
The local evidence gate does not promote source or replace independent adjudication.

Local evidence can establish the synthetic NROM CPU reference, deterministic
canonical state/frame/audio, observational semantic packets, and three independent
CPU presentation rungs. A local demo `PASS` means its own assertions passed;
it does not adjudicate source provenance, runner promotion, or wider compatibility.

Explicitly unearned: full NES compatibility, general commercial-game compatibility,
all-mapper support, cycle-perfect behavior, electrically exact NTSC, production
readiness, GPU execution/parity, native window/audio-device integration, temporal
reconstruction quality, neural inference, and performance/generalization claims.
No commercial ROM is needed for the M0 witness. Synthetic NROM PASS alone does
not support an SMB3/MMC3 boot claim. M1 separately uses the authorized external
ROM as behavioral input; no game name/hash/address or asset-specific machine branch exists.

## M2 evidence boundary

The supplied R0.4.2 runner reports **37/37 Tier A, 1/1 Tier B**. The focused
repair is candidate-complete for this support, without promotion authority.
All ten PPU VBL/NMI singles, CPU instruction singles, instruction/branch timing,
instruction-misc singles, five interrupt singles and both DMA singles pass.
The old screen/beep branch ROM timeouts were not demonstrated CPU failures;
the supplied suite now uses the machine-readable branch test. All baseline
passes are preserved. See
[the exact executed receipt](docs/M2_EVIDENCE_CONTRACT.md).

No full NES/general commercial compatibility, PAL/Dendy qualification, blanket
cycle-perfect claim, full sprite-evaluation/OAM/open-bus electrical qualification,
or mapper expansion is earned in M2. Mapper-3 controller/DMC tests remain deferred.

## M3 evidence boundary

M3 is **candidate_complete after independent oracle correction**, with mapper
1/2/3/7 support, deterministic NTSC controller/DMC collision handling and
dot-driven sprite evaluation/diagonal overflow. The 20 CTests and M2's 37/37
Tier A plus 1/1 Tier B pass. R0.2 independently corrects the legacy-ROM oracle:
the three original M3 ROMs and four corpus-qualified sprite-overflow ROMs are
adjudicated by deterministic read-only nametable text rather than the unrelated
modern `$6000` signature. This is bounded screen-oracle evidence, not a blanket
compatibility claim; see [M3 evidence and exact bounds](docs/M3_EVIDENCE_CONTRACT.md).

## Bounded machine behavior

* NROM and normal/new/Sharp-style MMC3B, mapper 4 submapper 0, are implemented.
  M3 adds baseline MMC1/UxROM/CNROM/AxROM. SUROM/SOROM/SXROM outer-bank wiring,
  MMC1A RAM behavior, mapper 105/185, other MMC3 revisions and arbitrary board
  variants remain deferred. Legacy conflict policies are explicit compatibility
  choices, not physical-board identification. Mapper 1 has ordinary bit-4 RAM
  disable; the discrete baseline keeps deterministic compatibility PRG-RAM.
  MMC3 PRG-RAM is 8 KiB; disabled reads return deterministic zero. MMC6 RAM
  semantics, battery persistence and analog bus behavior are not claimed.
  The IRQ filter counts three falling M2 edges while A12 stays low. Fixed NTSC
  phase/event ordering and synthetic phase tests are deterministic digital
  evidence, not a decap reproduction or measured electrical timing qualification.
* All official and common unofficial CPU paths are implemented with bus reads,
  RMW writes, page crossings and deterministic KIL. Synthetic coverage includes
  all 256 opcode cycle counts and exhaustive binary official/composite ALU cases.
  IRQ polling, CLI/SEI/PLP latency, RTI and NMI vector hijacks are modeled, with
  broader interrupt timing beyond the selected support unqualified. XAA's $EE convention,
  immediate LAX/LXA and high-byte-masked stores are deterministic digital choices;
  no analog stability, silicon-lot equivalence or unstable-store/DMA qualification.
* PPU background pipeline and candidate composition are modeled. M3 sprite
  evaluation is a bounded dot-driven digital secondary-OAM model with diagonal
  overflow, not transistor-level qualification. OAM DRAM decay, corruption,
  every $2004 edge, PPUDATA access while rendering, open-bus decay
  and hardware power-on write suppression remain incomplete. VBL/NMI suppression
  and enable/disable timing pass the ten selected NTSC singles, at a fixed phase.
  PAL/Dendy/VS/extended NES 2.0 modes are rejected or unsupported.
* APU DMC models $4010-$4013 configuration, $4015 enable/status, direct DAC,
  timer/shift output, sample address/length, loop/IRQ, mapper-backed sample fetch,
  address wrap and canonical serialization. M2 uses explicit DMA halt/dummy/
  alignment/get phases and OAM arbitration. Synthetic parity, mapper reads and
  collision byte preservation and both supplied external DMA singles pass.
  M3 models one-bit controller deletion from held-read/sample-get bus strobes.
  Full arbitration timing, all controller/DMA edge cases, complete frame-IRQ
  edge behavior, analog filtering/mixing and triangle DAC hold remain unqualified. The released
  600-frame SMB3 animation witness remains silent and is not an audible-output
  witness. A separate controlled two-START commercial witness exercises real
  game-driven DMC/APU writes, mapper-backed DMC fetches and nonconstant PCM while
  preserving core/frame/audio presentation invariance. This does not establish
  full APU or general commercial-game compatibility.
* Power-on bytes are chosen deterministic values, not measured power-on hardware.
  CPU warm reset preserves peripherals. Battery persistence and state restore
  are absent. Audio/trace/frame/output allocation budgets are explicit.

## Bounded presentation behavior

Canonical truth is the 256x240 palette-index/effect raster. Its fixed RGB palette
is an intentionally uncalibrated display reference. AUTHENTIC_SIGNAL is
signal-inspired/authentic, not electrically exact NTSC.

SEMANTIC_HD v1 uses connected regions, winning AssetKey/SurfaceKey provenance,
diagonal ambiguity locks and supersampled inset coverage. Source-cell ownership
is fixed; fine structures, occlusion and different owners constrain smoothing.
Safe large interiors permit at most two RGB codes of palette-aware modulation.
The claim is tested topology preservation on the stated fixtures, not universal
vector truth, font recognition, recovered artist intent, invented silhouettes or
a general perceptual quality win. Protected scenes can remain exactly digital.
A beautiful image is not machine evidence. SMB3 is a local generic visual witness
only; neither its ROM nor derived commercial imagery belongs in source.

AssetKey/SurfaceKey and correspondence identify observational source pieces, not
game entities. Duplicate pieces, changed epochs, occlusion and animation can
lose identity. Confidence values are conservative policy levels, not calibrated
probabilities. TEMPORAL_RECONSTRUCTION and NEURAL_RESIDUAL_EXPERIMENTAL are disabled
seams. No neural runtime, external emulator, vendor reconstruction library,
training or model invocation is part of this product.

Static validation is a local source/dependency assertion, not a security proof.
Generated build/evidence directories are not source authority. Git promotion,
host provisioning, external side effects and independent runner adjudication are
outside this candidate.
# M4-R2 modern semantic material candidate

M4-R2 may claim deterministic first-party semantic/material presentation in the
tested CPU scope, exact preservation of controlled machine/legacy witnesses,
generic bounded relief and a future authored-asset seam. Human visual acceptance
remains separate from automated activity and invariance checks.

It does not claim physically correct materials, recovered true 3D geometry,
universal semantic understanding, neural generation, temporal reconstruction,
photorealism or full commercial-game compatibility. No commercial artwork or
third-party runtime dependency is added. See the [M4-R2 contract](docs/M4R2_MODERN_MATERIAL_CONTRACT.md).
