# Architecture / machine serialization 3 / bounded M3 hardware candidate

## Ownership and time

The machine owns a CPU, mapper, 2 KiB RAM, two controllers, PPU and APU. Each CPU
bus access advances one CPU clock, one APU clock and three NTSC PPU dots. One
falling-M2 mapper event occurs between the second and third dots: chosen master
clock offsets 4/8/12 for PPU events and 10 for M2. The fixed CPU NMI input
sampling slot is between the first and second PPU events. Event order, rather than
subtraction of PPU timestamps, controls the MMC3 low-A12 qualification history. The
CPU instruction API is atomic to its caller but exposes ordered reads, writes
and dummy accesses through a bounded optional trace. Presentation has no Machine
reference, callback, writable view, or dependency on core.hpp. An owned
`shared_ptr<const Frame>` is published at scanline 241 dot 1; older snapshots
remain valid after subsequent frames. The core stores observational source
metadata in parallel with the fetch pipeline; it never reads that metadata to
decide a canonical pixel, interrupt, DMA, or sound sample.

The published machine snapshot contains canonical indices/effects and both PPU
composition candidates. `observe` creates a second owned immutable value with
identity and temporal fields. Region extraction and each renderer consume that
value independently. A future Nucleus host or GPU backend must consume this same
snapshot, rather than recomposing its own gameplay truth.

## Cartridge and CPU

iNES validation precedes allocation: exact payload size, magic, bounded bank
counts, trainer, console type and reserved fields. NROM accepts 16/32 KiB PRG,
8 KiB CHR ROM or zero-initialized 8 KiB CHR RAM, and modeled 8 KiB PRG RAM.
MMC3B accepts mapper 4 with 32..512 KiB PRG, up to 256 KiB CHR ROM or 8 KiB CHR
RAM, and 8 KiB permission-controlled PRG RAM. Bank indices wrap modulo physical
bank count; R6/R7 ignore bits 6/7. R0/R1 ignore their low bit. Four-screen wiring
overrides mirroring writes. NES 2.0 is detected and a conservative linear-size subset is loadable;
extended RAM/timing/misc declarations and exponent sizes are rejected. Nonzero
submappers are accepted only for UxROM/CNROM conflict policies 1 and 2.
Mirroring supports horizontal, vertical, four-screen and one-screen lower/upper.
Battery persistence is not implemented. A trainer initializes $7000-$71FF.

The M3 board factory implements baseline MMC1 and discrete UxROM/CNROM/AxROM.
MMC1 observes the actual CPU bus-cycle start through `Mapper::cpu_write_cycle`.
Five LSB-first data strobes commit to the register selected by the fifth address.
Consecutive-cycle data writes are ignored; bit-7 reset always clears the shift
register and ORs Control with $0C. PRG-RAM enable follows PRG bank bit 4.
The two-argument mapper write API represents independent, untimed pin strobes;
Machine always uses the timed API. Previous write cycle and serial state encode
canonically. No opcode, PC, asset or title participates in mapper decisions.

The discrete boards resolve configured bus conflicts against the byte driven
by the current PRG bank at the write address before updating their register.
UxROM legacy/submapper 0 chooses no conflict; CNROM legacy/submapper 0 chooses
AND conflict. NES 2.0 submapper 1 chooses no conflict and 2 chooses AND for both.
These are explicit compatibility policies for ambiguous headers. AxROM bit 4
selects physical nametable A/B; its baseline has no bus conflict. MMC1/AxROM
four-screen declarations are rejected; other mappers retain fixed four-screen
semantics. Exact board size envelopes and excluded wiring are in the M3 contract.
CHR source IDs bind physical 1 KiB banks; per-bank mutation epochs follow RAM
through bank switches. The high-plane epoch also identifies the selected bank.

The explicit table decodes all 256 byte values: 151 official opcodes, common
unofficial families, and twelve KIL encodings. SLO/RLA/SRE/RRA/DCP/ISC combine
NMOS read/old-write/new-write with their ALU operations. LAX/SAX, ANC/ALR/ARR/AXS,
LAS, alternate SBC and operand-reading NOPs share the normal address/bus paths.
High-byte-masked AHX/SHX/SHY/TAS stores apply the pre-index high-byte-plus-one mask
and carry-dependent address corruption. XAA uses a deterministic $EE precharge
convention; immediate LAX/LXA loads the immediate into A and X. Neither choice
qualifies analog-dependent unstable gates or DMA-sensitive unstable stores.

Read, indexed store and RMW paths issue their respective dummy accesses,
including zero-page wrapping, indexed page crosses and NMOS indirect JMP wrapping.
Branches take 2/3/4 clocks. Decimal flag storage works; all ADC/SBC operations,
including composites, use binary arithmetic. KIL records its opcode/address,
holds PC, and issues held dummy reads while peripherals continue; reset releases
it. It is a modeled machine state rather than an unsupported-opcode exception.

IRQ and NMI input history is distinct from instruction-boundary pending state.
An ordinary instruction uses the penultimate cycle sample; CLI/SEI/PLP use the
old I flag and RTI uses the already restored flag. A same-page taken branch keeps
its early poll; page correction adds a poll. IRQ can remain pending after its
pin falls. A sampled rising NMI remains latched after PPUSTATUS lowers the pin;
an unsampled pulse can be suppressed. BRK and IRQ may select the NMI vector after
the return-PC pushes while retaining the original stacked B bit. The instruction
API remains atomic. These mechanisms pass all five supplied interrupt singles;
full interrupt edge qualification is not claimed. The APU clocks before the
$4015 read strobe: a coincident frame IRQ is visible and then acknowledged.
Acknowledgement lowers the live pin without retracting the earlier CPU sample.

Power creates zero RAM/VRAM/register storage, deterministic OAM/palette, noise
LFSR 1, PPU at pre-render line 261 dot 0, and a seven-clock CPU reset sequence
ending at the ROM reset vector with SP=$FD, P=$24. This models deterministic
initial values, not physical power-on randomness. `Machine::reset` is explicitly
a CPU warm reset: seven bus clocks, SP minus three, I set, RAM and peripherals
preserved. Whole-system deterministic power reset means constructing a fresh machine.

DMA is queued by $4014 and halts a CPU read. The existing step API can return
just the queued OAM transfer at an instruction boundary. CPU writes, including
both RMW writes, cannot be halted by DMC. `tick()` now advances exactly one clock;
bus-read arbitration owns all additional clocks. OAM uses a halt, optional
alignment and 256 get/put pairs for its 513/514-clock baseline. In the machine's
cycle convention, gets start at even cycle numbers and puts at odd ones.
Thus a halt starting on get takes 514 clocks, and a halt on put takes 513.

DMC has an enable-request delay and explicit halt, mandatory dummy, optional
alignment and mapper-backed get phases. Reload requests follow the output unit's
empty-buffer transition on that same clock, allowing RDY on the next CPU read.
OAM and DMC can share a halt. During OAM transfer, DMC
preparation overlaps OAM work; DMC gets own colliding read slots. OAM puts require
a valid latch, so a stolen get causes an alignment slot before the next get.
Every bus slot clocks the APU, three PPU dots, interrupt inputs and falling M2.
Trace entries include DMC sample reads. `dmc.stall_cycles` counts occupied DMC
phases, including overlap, not exclusively additional elapsed CPU time.

This replaces the M1 fixed four-clock approximation. Both supplied OAM/DMC
singles and IRQ/DMA timing pass in R0.4.2 and remain passing in M3. Controller
/OE stays asserted across DMC halt/dummy/alignment reads of $4016/$4017, holding
one serial bit. The sample get releases that strobe, so the resumed CPU read
clocks a second bit, deleting one. Both ports preserve high open-bus bits from
the bus drive; no analog decay is introduced. Hold address/bit/valid state is
canonical. Other repeated-read bus side effects retain their M2 behavior.
DMA abort/reload boundaries, arbitrary OAM/controller-port source combinations,
and unstable-store/DMA interactions remain unqualified.

## PPU composition and observation

PPUCTRL/PPUMASK/PPUSTATUS/OAMADDR/OAMDATA/PPUSCROLL/PPUADDR/PPUDATA implement
write-toggle, v/t/fine-X, read-buffer, increment selection and palette mirrors.
Background fetches occupy visible/pre-render fetch slots: nametable, attribute,
low pattern, high pattern, coarse-X increment. A 16-pixel parallel pipeline
preserves fetched two-bit samples and their palette and source coordinates.
Dot 256 increments Y; 257 transfers X; pre-render 280-304 transfer Y. Prefetch
and dummy fetches are reported to the mapper interface. Rendered odd frames
skip one dot, with rendering sampled at pre-render dot 338 before the dot 339
skip decision. Either background or sprite rendering enables the skip. Vblank
sets at scanline 241 dot 1; status flags clear at pre-render dot 1. `dot` names
the next dot to execute: a PPUSTATUS access immediately before the dot-1 set
suppresses that frame's flag. Status clears the NMI line; only an already CPU-
sampled edge survives. Enabling NMI during vblank raises the line but must pass
the CPU input and instruction-poll stages. All ten external VBL/NMI singles pass
at this fixed NTSC phase. All external timed fetches include absolute PPU clock and kind;
mapper IRQ state belongs in Mapper, never in the presentation observer. Internal
palette accesses do not drive this fetch interface; palette reads still perform
the external nametable buffer refill. Empty 8x8 sprite slots fetch the PPUCTRL
sprite table; empty 8x16 slots fetch table 1. Address levels hold between events.

Secondary OAM clears one byte on each even dot 2-64, with FF latched on odd dots.
On visible rows, dots 65-256 alternate primary-OAM read and secondary operation.
The first eight in-range sprites copy in OAM order. When secondary OAM fills,
an out-of-range comparison advances primary sprite n and byte m independently,
causing the diagonal overflow bug, including false positives and false negatives.
The first matching overflow comparison sets status, drains three suppressed
copy bytes, and stops further range comparisons. Search never wraps past sprite
63. Internal post-match idle OAM-bus activity and OAMADDR corruption are outside
this bounded model. Each row initializes evaluation counters at dot 1; rendering
disable freezes clear/evaluation work and does not clear overflow. Either BG or
sprite enable is sufficient. Pre-render dot 1 clears status regardless of mask;
pre-render does not select sprites for row zero. Visible row 239 still evaluates.

Dots 257-320 load eight output units from secondary OAM and fetch patterns.
No primary-OAM scan occurs in the fetch interval. 8x8/8x16,
flip bits, OAM order, left clipping, background priority and sprite-zero overlap
are modeled. Sprite-zero hit excludes x=255. Source records retain the first opaque sprite
even when background wins. Missing opaque candidates carry default source values
and an explicit false opacity field; they are not asserted visible.

Each visible sample records frame (packet), scanline, dot, canonical palette
index, emphasis, grayscale, winner, candidate opacity, nametable/attribute
addresses, pattern base, tile number, palette group, CHR epoch, fetch epoch,
local coordinates, v-derived scroll/fine-X, OAM slot, flags, zero and priority.
`fetch_epoch` and `high_fetch_epoch` are the low/high pattern-read PPU clocks;
they are provenance, not physical bus pin measurements. Pattern base includes
the NROM pattern-table bank. `chr_source` is FNV-1a of the complete validated
cartridge image for NROM, distinguishing ROM/source domains. For MMC3 it hashes
that cartridge identity plus the actually mapped physical 1 KiB bank number.
MMC3 epochs pack physical bank number in bits 48..55 and CHR-RAM write generation
in bits 0..47, so a bank change between low/high plane reads is also distinct.
The bounded runtime cannot exhaust the 48-bit write generation. NROM CHR epoch increments on
changed CHR-RAM bytes only; low/high epochs are latched separately at fetch,
so writes between reads cannot relabel an already-fetched plane retroactively.

## Identity and temporal semantics

Keys use FNV-1a-64 with explicit little-endian fields, distinct schema domains,
and no pointers, frame numbers, timestamps or presentation settings.

`AssetKey = H(AST1, mapper_domain, layer, chr_source, pattern_base, tile,
palette_group, sprite_flags, low_chr_epoch, high_chr_epoch,
background_attribute_address_if_background)`.

Backdrop keys omit source fields. For background the attribute address supplies
conservative neighborhood context. This intentionally limits cross-context
merging; it is not a learned asset database.

`SurfaceKey = H(SRF1, AssetKey, layer, nametable_cell_if_background)`.

Sprites deliberately exclude OAM slot and screen position. This is stronger than
slot continuity for a uniquely visible piece but is not an entity ID. Duplicated
pieces, animation, CHR changes and gaps can invalidate correspondence. Hashes are
indexing hypotheses; matching also compares the underlying structural fields.

Per-sample correspondence is indexed by surface and local source coordinates;
backdrop uses screen coordinates. Both frames must have a unique candidate and
be adjacent with the same mapper domain. Motion is previous-minus-current
position, bounded to 32 source pixels per axis. Stable background confidence is
255, unique sprite-piece confidence 192, appearance change confidence 96; absent,
ambiguous or discontinuous support has confidence 0. These are policy grades,
not statistical probabilities. Reactive classes are stable, disoccluded,
appearance, ambiguous and history_gap. No optical flow or gameplay identity is
invented. No temporal accumulation or neural inference runs in M0 or M1.

## Presentation references

EXACT uses the frozen M3 first-party integer display mapping and uniform integer
scaler. The canonical indexed frame remains the machine-truth oracle; this
palette is not a colorimetric hardware measurement. M4-R freezes all uint8
palette/effect calls and the complete output of a palette/effect fixture at
each scale from 1x through 8x against the separately built authoritative parent.

AUTHENTIC_SIGNAL v1 converts the palette/effects to integer YIQ-like values.
Horizontal luma uses [1,6,1]/8; chroma uses [1,2,3,4,3,2,1]/16. A twelve-entry
carrier table supplies three phases selected from source x/y and observed frame number, mixing bounded detail
between luma/chroma (each interaction at most eight codes). RGB decoding clamps
to bytes. Horizontal subpixel interpolation and a quadratic 216..256/256 beam
response give a continuous bounded scan profile. Borders extend the same row;
there is no vertical bleed, random noise, wall clock or recurrent display state.
This is a deterministic signal reference, not electrical NTSC exactness.

SEMANTIC_HD v1 retains four-connected row-major regions, area, perimeter and
bounds. Compatibility now requires AssetKey, SurfaceKey, layer, effects and
winning source provenance. Background nametable cells remain separate even
when assets repeat. Sprite origin/slot separate within-frame instances sharing
keys; this does not change the slot-independent observation/history identity.
Hidden candidates never donate color across an occlusion boundary.

Before coverage, reconstruction locks tiny/narrow/high-perimeter fine regions,
backdrop, local one-pixel necks/tips and all four cells of two-color diagonal
saddles. Four-connectivity wins ambiguous diagonal contacts. Broad sprite and
background interiors may reconstruct; no font, HUD location, game, tile number
or scene recognition is used. Protection is a conservative geometric heuristic.
The diagnostic protected surface includes local locks beyond Region::protected_fine.

Eligible axis contours and unambiguous three-to-one corners receive Q8 inset
coverage from 4x4 area samples per output footprint. Samples are clipped to the
categorical source cell; corner donors must belong to an observed compatible
region. Total donor weight is at most 96/256, leaving at least 62.5% original
owner contribution before integer rounding. Locked cells are exact. No donor
crosses an owner/effect boundary or comes from a locked cell. There is no new
categorical silhouette or geometry. A palette-relative spatial gain of at most
two RGB codes is allowed only within a two-cell erosion of sufficiently large
regions; it never runs on contour cells. This is tested topology preservation,
not universal vector truth or recovery of artist intent.

`render_diagnostic` provides debug-only layer, asset, protected-structure,
signed motion/confidence and reactive views. Asset colors are a lossy hash
visualization, not an identity oracle. Motion channels encode 128+4*displacement
with byte saturation; blue is confidence. These views grant no history authority.

Output extents are bounded to 256..2048 by 240..1920. EXACT additionally requires
uniform integer scaling; HD supports noninteger scales. Disabled temporal/neural
rungs throw rather than silently falling back. Neural policy grants no geometry,
silhouette, HUD/text, or low-frequency authority, even in its future seam.
1024x960 is explicitly covered by all three modes and is the debug default;
existing render defaults remain 768x720. The historical M4-R evidence
contract is not included in this source-only distribution.

## Audio and deterministic state

APU clocks pulse duty/timers/envelopes/length/sweep, triangle timer/linear/length,
and a 15-bit noise LFSR with mode and period. Four/five-step frame sequencing,
frame IRQ, $4015 status and delayed $4017 reset are explicit. DMC models
$4010-$4013 configuration, direct DAC, timer/shift output, sample address/length,
loop/IRQ, $4015 enable/status, mapper-backed sample fetch and address wrap. Its
DMA uses the explicit scheduler above. Frame IRQ can assert on each of the
three terminal frame-counter edges; status acknowledgement does not inhibit a
later assertion. The $4017 reset delay includes the write cycle and then three
or four clocks according to APU parity. Broader APU timing remains unqualified. A rational 44100/1789773 accumulator emits mono
signed-16 PCM. Pulse mixing uses integer nonlinearity and triangle/noise/DMC use
a bounded deterministic presentation mix. Analog filtering, exact TND transfer,
triangle DAC hold subtleties and electrical audio exactness are not qualified.
PCM is capped at ten seconds; the machine is a bounded capture engine, not an
unbounded real-time audio host.

Canonical serialization version 3 has magic/version and explicit little-endian fields,
including ROM/RAM/mapper epoch, CPU flags/latches, bus/controller state, pending
DMA, scheduler clocks, all PPU composition/fetch buffers and completed canonical
pixels, and every APU counter/register/PCM sample. No padding, native object dumps
or memory addresses enter it. New encoded state includes IRQ poll validity and
sample history, pending IRQ, NMI latch/sample history, VBL suppression, rendering
skip latch, DMC request delay/phase, DMA-active and OAM-latch validity, byte index,
halted address and OAM data latch. M3 adds the controller read-strobe hold,
secondary OAM and primary identities, evaluation n/m/address/read latch/count,
overflow-copy tail and exhausted-search state. Mutation tests cover every added
field. Observational metadata, trace settings, keys,
regions, temporal confidence and rendered images are excluded. SHA-256 hashes
the serialized bytes. Tests additionally compare full state byte vectors.
This is a serialization/digest API, not a save-state restore API. Public machine
fields are test/debug surfaces, not validated arbitrary-state import surfaces.

MMC3 serialization includes cartridge metadata and storage, registers, selected
register/modes, RAM permissions, mirroring, counter/latch/reload, IRQ enable and
pending level, held A12 and saturated low-M2 history, CHR-RAM generation, and all
diagnostic event counters. Immutable per-bank source hashes are derivable from
the serialized cartridge source and bank count. No host pointer or time enters
the encoding. Existing NROM/MMC3 encodings and the eight-file M0 evidence layout
remain unchanged; M3 boards add their own encodings. Machine state bytes/hashes
deliberately change with version 3.

The ROM runner owns one bounded file read and creates two machines from that
memory. One brackets observation and all three renderers on the final two frames;
the other runs the same requested frame count without presentation. Full canonical
state bytes and frame/audio hashes must match. Every requested frame contributes
to a set of canonical frame hashes; only the final immutable observation and PCM
are emitted. The runner has no identity allowlist or title-specific behavior.
# M4-R2 additive presentation path

`ContourIR + immutable semantic provenance -> ModernPlan -> bounded material lighting -> MODERN_REMASTER`

The implementation is isolated in `modern_plan.cpp`, `modern_assets.cpp` and
`modern_render.cpp`. The original Semantic HD renderer remains the structural
raster and zero-control fallback. No presentation data feeds the machine.
The detailed historical M4-R2 contract is not included in this source-only
repository. Its owner, depth, asset replacement, control, diagnostic, and test
boundaries remain historical claims; the current source and tests are included.
