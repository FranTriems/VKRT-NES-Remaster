# M2 NTSC evidence contract and local receipt

Status: **candidate_complete** for the focused R0.4.2 repair, 2026-09-29.
Bounded to the supplied corrected suite and regression witnesses; no Git promotion.

Parent HEAD: `12121debdc83e4199f3f5d8bbee23c0e138b2a0b`.
Parent tree: `2405dfc9a1756f83d2c6cba7211194b4671aa1e6`.
M0/M1 are the task-supplied closed/promoted floors. Source changes are confined
to `products/vkrt_nes_native_s0/`. No commit, network request, dependency install,
other emulator/core, subagent or copied external ROM is part of this pass.

## Contract

The canonical C++20 machine remains the only machine authority:

`CANONICAL MACHINE -> IMMUTABLE SEMANTIC OBSERVATION -> PRESENTATION`

The runner owns its excluded suite, harness, corpus, baseline and result protocol.
Its binaries are validation inputs only, never source authority or a product
runtime dependency. Only the explicitly supplied runner command generates files
in the excluded runner directory; no harness, suite or corpus was edited:

```text
python .vkrt_m2_validation/run_suite.py --output .vkrt_m2_validation/model_latest.json
```

A complete focused candidate requires all 37 Tier A rows to report `pass`, preservation
of every baseline pass, synthetic/build/static gates and deterministic M0/M1
evidence. A timeout or exception is not a pass. Tier B is prospective only.
The no-mapper-expansion boundary defers the mapper-3 `read_joy3` tests entirely.

## Executed result

| Gate | Observed result |
| --- | --- |
| Offline GCC 15.2.0 / C++20 / CMake / Ninja Release build | Pass |
| CTest, retained M0/M1 plus five M2 suites and static gate | 19/19 pass |
| M2 synthetic executable, all suites | 1,587,279 checks; zero failures |
| External Tier A, supplied R0.4.1 baseline | 34/37 pass |
| External Tier A, R0.4.2 candidate | 37/37 pass; no baseline pass regressed |
| External Tier B, R0.4.2 candidate | 1/1 pass |
| M0 self-test, two processes, eight artifacts compared | Pass |
| Authorized released-controller ROM, two 600-frame processes | Pass; eight artifacts identical |
| Controlled-input authorized ROM, two independent repeats | Pass; matching state/frame/audio hashes and nonconstant PCM |

Passing external groups: all 15 instruction singles, both instruction/branch timing
singles, all four instruction-misc singles, all ten VBL/NMI singles, all five
interrupt singles, and both DMA singles. No broad instruction or electrical
qualification follows beyond this selected digital evidence.

## Focused repairs and oracle correction

The three old screen/beep branch ROMs do not expose the runner's result protocol.
Their R0.3 timeouts were not demonstrated CPU failures. The user-supplied suite
replaces them with `instr_timing/rom_singles/2-branch_timing.nes`, which passes
its eight cases with the hardware timing `2 2 2 2 3 3 4 4`. No suite was edited.

Only `src/core.cpp` and `src/apu.cpp` changed in production during R0.4.2:

* The frame sequencer clocks before the $4015 status-read strobe. A coincident
  IRQ is returned and acknowledged; the live pin clears while the earlier CPU
  sample remains valid. The prior ordering prevented APU phase synchronization
  and caused the branch/IRQ test to fail before reaching its branch case.
* OAM and DMC get phases use even bus-cycle starts, puts use odd starts. OAM
  retains a halt plus optional alignment and 256 get/put pairs (514 clocks when
  halted on get, 513 on put). IRQ/OAM now matches the entire expected table,
  including `8 +524`, `8 +525`, `8 +526`, `9 +527`.
* DMC reload requests become pending on the same clock that the output unit
  empties the buffer, making RDY eligible on the next read. Its initial-load
  delay remains separate. DMC still shares setup clocks with OAM and steals only
  get slots. The collision single and its Tier B variant both pass.

The existing generic CPU branch poll is retained: same-page taken branches keep
the operand-fetch poll; page correction adds another poll. All four sections of
the branch/IRQ single pass. There are no filename, hash, message, PC, result-code
or asset-dependent production paths, and no fixed fetch-count stall multiplier.

## Synthetic hardware oracles

`tests/test_m2.cpp` uses original programs and independently computed expected
values, without external program bytes or identities:

* All 256 opcode clock counts, twelve KIL held-read/reset paths, exhaustive
  SLO/RLA/SRE/RRA/DCP/ISC arithmetic, each composite addressing form's read/old-
  write/new-write, NOP read side effects, indirect wrapping and page crosses.
* Immediate digital known answers, SAX/LAX/LAS, high-byte masks, carry-dependent
  unstable-store addresses and TAS stack effects. XAA uses a fixed $EE precharge
  convention; immediate LAX/LXA loads A/X. These are bounded digital choices,
  not claims about analog-sensitive silicon behavior or unstable-store/DMA edges.
* A first-party mapper pin stimulus for IRQ polling; CLI/SEI/PLP old-I behavior,
  RTI restored-I behavior, branch page correction, BRK/IRQ NMI hijack and stacked
  B, enable-during-vblank latency, frame-IRQ reassertions and delayed $4017 reset.
* VBL set/clear dots, status suppression before set, sampled versus unsampled
  NMI, rearming versus held levels, rendering enable and odd-frame skip sampling.
* DMC read-only halt, mandatory dummy/alignment/get, OAM 513/514 baseline retained
  in M0, shared halt, parity and mapper clocks, a 1,034-case OAM collision-offset
  sweep with byte/order/parity assertions, RMW write immunity and sample wrap.
* Mutation coverage of every added canonical field, repeated DMA-state equality
  and presentation brackets around the expanded serialization.
* R0.4.2 adds coincident frame-IRQ status reads, every branch condition/direction/
  page case and input-edge position, normal JMP polls, IRQ/NMI during OAM halt,
  exact collision lengths across all 1,034 offsets, OAM consecutive RMW writes,
  immediate reload versus delayed initial load, and DMC reload-state encoding.

All M0/M1 suites remain registered. Two obsolete M0 expectations were replaced
and strengthened because they directly contradict the M2 contract: rejecting
every unofficial opcode, and fetching/stalling synchronously on the $4015 write.
The 151-official coverage and exhaustive ADC/SBC tests remain intact. The new
checks require digital execution/KIL and read-cycle-only parity-bounded DMC,
including mapper sample identity. No test was removed, disabled or marked xfail.
R0.4.2 corrects the old inverted get/put parity expectations in M0/M2 and adds
exact collision-length assertions to the existing bounds and byte checks.

## Canonical state contract

Machine serialization is version **2**. There is still no state-restore API or
version-1 byte/hash compatibility promise. New encoded fields are IRQ pending,
poll validity and sample history; NMI latch and sample history; VBL suppression
and render-skip latch; DMC request delay and DMA phase; DMA-active, OAM-latch
validity, transfer index, held address and byte latch. Existing state, mapper
encodings, semantic/presentation interfaces and artifact layouts are retained.
R0.4.2 introduces no new canonical fields: the existing sample history, request
delay, phase, index and latch fields already encode the repaired scheduling.
Core hashes are allowed to evolve; no historical hash is emulated.

`tick()` advances exactly one CPU/APU clock and three PPU dots. Read arbitration
owns DMA stalls. DMC phase occupancy includes clocks overlapped with OAM, so its
`stall_cycles` counter must not be interpreted as exclusively added CPU clocks.
Trace observations and presentation never drive scheduler decisions.

## Reproducible local artifacts

Generated and excluded paths, relative to the product:

* `build-r042/`: compiler outputs and CTest log.
* `evidence-r042/`: initial source digests/copies, baseline executable/library,
  read-only IRQ probe, controlled witness source/executables and JSON receipts,
  supplied runner result copy, artifact comparisons and validation receipts.
* `evidence-r042/baseline-m0/`, `candidate-m0-a/`, `candidate-m0-b/`: M0 artifacts.
* `evidence-r042/baseline-smb3/`, `candidate-smb3-a/`, `candidate-smb3-b/`: M1 artifacts.

Use the README build, CTest and evidence-validator commands with these directory
names. The runner JSON is copied as evidence only; no verdict is recomputed from
ROM identity, result strings, known PCs, failure codes or display assets.

The released-controller witness is silent (zero nonzero PCM samples). The separate
controlled witness runs 600 frames with START held at zero-based frames 180-181
and 360-361, released otherwise. Both machines receive identical inputs; one
brackets all three renderers on the final two immutable observations, the other
executes without presentation. Two process repeats produced identical receipts:

* DMC fetches: 1,543; DMC occupied phase clocks: 6,146 (R0.3: 6,147).
* PCM samples: 440,218; minimum 0, maximum 22,663.
* Mapper IRQ assertions/acknowledgements: 575/575.
* Canonical state SHA-256: `8e6240fddc9a2ec2fce6129dd20e5ee9995f0cbde4a58a7d2a071d5bdc5cc6ce`.
* Canonical frame SHA-256: `bf63816a64bedb9a92942093c1d7a7bf0c31980031a37640075931b63baa4d20`.
* Canonical audio SHA-256: `5bbdfbdfe25b51a18db4509fa4b7096bfbe30acb6438c75b77cd0a39bd644aa4`.

For M0 and the released-controller SMB3 witness, canonical frame/audio,
all presentation images and semantic JSONL are byte-identical to the preloaded
candidate. The controlled witness's audio changes with corrected DMC timing;
its frame is unchanged and each repeat remains deterministic and presentation-
immutable. The supplied local 600-frame schedule above yields 1,543 fetches;
it is distinct from the task's 129-fetch aggregate example, which is not used
as a timing formula or a production condition.

The input schedule belongs to the external test adapter, not production machine
code. This is a bounded behavioral/audio/invariance witness, not a playthrough.

## Unearned claims

M2 closure, full NES or general commercial-game compatibility, PAL/Dendy,
blanket cycle-perfect timing, full sprite evaluation/overflow/OAM corruption,
open-bus electrical decay, unstable-opcode analog fidelity, complete APU or
DMA/controller-edge conformance and mapper expansion remain unearned. CNROM
remains unsupported. M0/M1 promotion authority remains with the task's supplied
floor; this local pass creates no new promotion authority.
