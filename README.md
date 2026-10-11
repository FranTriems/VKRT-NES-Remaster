# VKRT NES Remaster

Native NES emulation and observation-driven visual remastering in C++20. An independent source distribution of the VKRT NES product, authored by **Francisco Triemstra (VKRT)**. This repository is **not** the private VKRT software factory and contains none of its Git history or engineering control-plane components.

## Engineering boundary

```text
CANONICAL_MACHINE -> IMMUTABLE_SEMANTIC_OBSERVATION -> PRESENTATION
```

The CPU, PPU, APU, cartridge/mapper, controller and timing state are authoritative. Original and remastered display paths consume observations of that state; presentation is not permitted to rewrite the emulation state. See [architecture](vkrt_nes_native_s0/ARCHITECTURE.md) and [claim boundaries](vkrt_nes_native_s0/CLAIM_BOUNDARY.md).

## Windows application

The Windows frontend provides a Home screen, ROM selection, audio and input facilities. The current user-facing presentation choices are **Original / F1** and **Remaster / F4**. Prior F2/F3 rendering experiments are not additional supported user modes. Remaster uses semantic observation and bounded presentation methods, not external commercial artwork. One Metroid-specific visual profile can be selected from a ROM fingerprint; this distribution includes no Metroid ROM, artwork or soundtrack.

Keyboard settings, optional XInput navigation and persistence have bounded automated coverage. A complete real-device/audio/playability acceptance test and compatibility survey are not implied by passing CTest. This is not a claim of universal, cycle-perfect or all-game compatibility.

## Reproducible build

Requires Python 3, CMake 3.20+, Ninja (for these commands), and a C++20 compiler. From this repository root:

```sh
cmake -S vkrt_nes_native_s0 -B build-nes -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=20
cmake --build build-nes
ctest --test-dir build-nes --output-on-failure
python vkrt_nes_native_s0/tools/validate_static.py --root vkrt_nes_native_s0
```

On Windows, run `build-nes/vkrt_nes_runtime.exe` (Home screen). The test/demo tools also exercise synthetic, generated NES inputs without distributing commercial games. The original independent Windows packaging passed **41/41 CTests**; validation of the exact published commit should be performed separately and recorded as its own receipt.

## Licensing, authorship and visible credit

**Copyright (c) 2026 Francisco Triemstra (VKRT)**. Licensed under the OSI-approved **Attribution Assurance License (AAL)**. Read [LICENSE](LICENSE) and its verifiable [GPG-signed copy](LICENSE.asc). [Public verification key](AUTHOR_GPG_PUBLIC_KEY.asc).

The AAL requires a prominent author attribution at every launch of a distributed binary incorporating this covered code or depending on it, according to its actual terms. The original author credit is **Francisco Triemstra — VKRT — https://github.com/FranTriems**. Preserving attribution is not an endorsement of downstream products by VKRT. Contributors may retain attribution for their own changes.

Source redistribution must include the verifiable signed license notice. **This initial source release does not distribute executable binaries or commercial ROMs.** If you distribute a built executable, you are responsible for implementing and validating the license's launch-time attribution in your build.

## Boundaries and provenance

No commercial `.nes`/`.fds` ROMs, third-party game assets or external emulator cores are included. The implementation follows documented public hardware concepts and contains first-party code; an exhaustive text-similarity or legal audit of every possible prior implementation is outside the automated qualification. Read [third-party boundaries](vkrt_nes_native_s0/THIRD_PARTY_BOUNDARY.md) and [research basis](vkrt_nes_native_s0/docs/RESEARCH_BASIS.md).

The original product contains historical internal milestone/evidence documentation; this public source distribution includes the narrower documentation required for compilation, deterministic gates and responsible provenance. Historical private engineering receipts are deliberately not reproduced. VKRT NES Remaster is not affiliated with Nintendo or commercial game publishers.
