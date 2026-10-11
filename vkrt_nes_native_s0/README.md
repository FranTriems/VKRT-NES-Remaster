# VKRT NES Native S0 — Independent public source tree

This directory preserves the native NES C++ implementation and its deterministic validation surface. It is intentionally named `vkrt_nes_native_s0`: the original read-only static validator binds this directory name.

## Architecture

`CANONICAL_MACHINE -> IMMUTABLE_SEMANTIC_OBSERVATION -> PRESENTATION`

See [ARCHITECTURE.md](ARCHITECTURE.md) and [CLAIM_BOUNDARY.md](CLAIM_BOUNDARY.md) for the detailed source-level machine boundary, current implementation and bounded claims. The CPU, PPU, APU, mappers and controller state own gameplay truth. Rendering consumes immutable observations; Original/F1 and Remaster/F4 are presentation paths.

## Build

From the **repository root** (the parent of this directory):

```sh
cmake -S vkrt_nes_native_s0 -B build-nes -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=20
cmake --build build-nes
ctest --test-dir build-nes --output-on-failure
python vkrt_nes_native_s0/tools/validate_static.py --root vkrt_nes_native_s0
```

On Windows, the `vkrt_nes_runtime` target supplies the Home/UI/frontend. Emulation compatibility is bounded; passing synthetic automated suites must not be treated as a universal game-compatibility or physical-device acceptance claim.

## Included documents

The source-only export keeps the canonical architecture, claim, third-party boundary, public research basis and M0–M3 evidence contracts required by the unchanged static gate. Optional milestone histories and local/private receipts were excluded deliberately, not reconstructed or relabeled as public results. This repository carries **no original private monorepo Git history**.

See the [root README](../README.md) and [signed AAL license](../LICENSE.asc) for attribution and redistribution obligations. No commercial ROMs or assets are included.
