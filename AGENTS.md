# Agent instructions — NCAA Football 10 PSP Web Recomp

## Scope and orientation

The user's instructions take precedence. This project targets **NCAA Football 10 PSP US, ULUS10440, for the web**. Work from the repository root containing `scripts` and `profile`. In the original local workspace this is the inner directory, beneath an outer wrapper. Read `HANDOFF.md`, `ENGINEERING_LOG.md`, and relevant sections of `docs/NCAA10.md` before substantive changes; consult `docs/NCAA10-CRASH-PROVENANCE.md` for the allocator/reader issue.

The host runtime is inherited from the God of War web ports. Preserve attribution while keeping NCAA's identity and evidence prominent. `NCAA2007` is the verified Football 10 ELF's internal module name. Do not substitute another game's executable, generated corpus, assets or screenshot.

## Identity and execution requirements

- Run `python scripts/verify_ncaa10.py games/ncaa10/root` when validating identity or preparing a build.
- NCAA builds use `PSPRECOMP_PROFILE=web`, `PSPWEB_GAME=ncaa10`, and `disc0:/PSP_GAME/SYSDIR/EBOOT.BIN`.
- Keep gameplay execution ahead-of-time compiled. Guest CPU interpreter/JIT fallbacks are outside the intended architecture. Static analysis/diagnostic instruction decoding is compatible with that requirement.
- Main and EA audio generated corpora are separate. Preserve relocated audio binding and regenerate affected output when changing the compiler/generator.
- Fix the generator or host source, rather than relying on edits to ignored generated C++ that disappear on regeneration. Record compiler fixes in `patches` as needed.

## Memory and correctness

- Keep **64 MiB guest RAM / 56 MiB user partition as the NCAA default**, with explicit 32 MiB comparison support. Other titles and the core runtime retain their own defaults unless separately authorized.
- Use the actual guest-memory size for AOT bounds, SysMem, stacks, display validation, audio scans and diagnostic captures. Emscripten linear-memory capacity is different from guest RAM.
- Preserve the original allocator and instruction semantics. Do not map zero, invent a fallback allocation, skip the faulting store, free a bank early or edit EA heap headers to hide the diagnosed failure.
- Trace the first incorrect state, request provenance and last writers. Respect delay-slot order and distinguish a fault from a completed memory write.
- Guard genuinely title-specific adaptations by the verified title and precise state/caller conditions. Generalize only with evidence and appropriate regressions.

## Build and verification

Use existing build directories and the commands in `HANDOFF.md`. Preserve the working native/web baseline. The framework/profile junction means edits through either path affect the same profile.

For code changes, run relevant host tests and affected web regression scripts. For compiler/runtime changes, run appropriate synthetic compiler tests and regenerate affected units. Rebuild the web/native targets affected by the change. Documentation-only edits need identity/link/command review, not a needless full game rebuild.

Production `PSPWEB_INSTRUCTION_TRACE_UNITS` and `PSPWEB_INSTRUCTION_TRACE_OPT_LEVEL` should be empty. Keep diagnostics opt-in; record flags, RAM mode, title hash and tested scenario in evidence.

Gameplay verification requires pressing Start and reaching actual play beyond title/menu/loading screens. Capture real output. Target NCAA's observed approximately 30 presented FPS at 100% simulation speed; do not equate 60 Hz Vblank or God of War's figures with NCAA gameplay FPS.

Do not claim full PSP/render/audio/controller/mode accuracy from a short smoke test. A passing simulated Gamepad API test does not verify a physical Bluetooth controller. Failed/black reference captures are not valid visual comparisons.

Native graphics captures must explicitly request and verify at least eight bits per RGBA channel; EGL defaults can select RGBA4444. Prefer direct guest-format framebuffer readback and verify that presentation matches it before computing image metrics. Earlier local GLES replay images using the default pbuffer are excluded from color-fidelity conclusions; see the 2026-10-14 correction in `docs/PPSSPP_REFERENCE.md`. Their independent synthetic pixel regressions remain valid.

Blend optimizations must preserve overlapping primitive order, destination stencil-alpha updates and self-texture feedback. Test GPU-only snapshots without CPU downloads, shared-edge/disjoint batching and source-only equations. Keep native replay timings separate from actual browser/gameplay FPS, and report local image regions that worsen even when full-frame error improves.

GPU texture windows must wrap/clamp individual filter taps within their own bounds and expand packed source alpha before filtering. A masked alpha test can still require all source alpha bits after interpolation. Source feedback needs ordering with blending disabled and across separate surfaces that alias guest VRAM. Test overlapping format views and retain distinct source samples at higher internal resolutions; scaled rendering is not native PSP hardware evidence.

Preserve line lists/strips and endpoint exclusion: NCAA uses them for field markings. Line-cell batching may merge only identical effective shader inputs and must respect feedback boundaries. Measure shader changes on ordinary scenes too; an extra varying caused a large native slowdown even with line draws omitted. Keep fractional/antialiased-line hardware claims separate from the selected software-reference behavior.

Triangle-grid/center changes must preserve rectangle UV sampling. A global center shift can improve field geometry while breaking UI edges. Rectangle recognition requires compatible attributes, separable UVs and a shared diagonal; four unique corners alone do not prove rectangle topology. Keep complete frames, isolated prefixes, malformed quads, gradients and alpha-blended adjacent primitives in acceptance evidence.

Use the current hardware-tested software oracle pinned in `docs/PPSSPP_REFERENCE.md`, not the older release's 7/16/nearest-even behavior, for new rendering acceptance. Preserve historical captures and label reference version/commit on every metric. Report worsening prefixes/ROIs alongside full-frame wins. The current half-pixel/byte-stage fixes do not establish complete fixed-point GE accuracy. Inspect reference source locally without copying GPL implementation into the port. `scripts/capture_ge_reference.py` uses a hash/version/title-checked headless executable, a fresh build output/memstick and repeated captures; keep it separate from production AOT execution.

## State and documentation

- Preserve local game files, prior diagnostic captures and save exports. Export/hash saves before replacing fixtures; keep baseline and progressed checkpoints identifiable.
- Reuse a suitable running local server/session. Inspect current UI and savedata before acting on handoff snapshots. Restore temporary test settings.
- The original game/debug workspace has no Git checkout; its published source checkout is recorded in `HANDOFF.md`. GitHub clones are ordinary Git repositories. Inspect the actual root and status before Git operations, and preserve local dependencies, generated output and game state.
- Append dated engineering entries for meaningful changes, with reason, checks, artifact paths and unresolved limitations. Update the handoff to the latest reproducible state.
- Keep `README.md` focused on NCAA; label upstream history and performance evidence by title.
- Share reusable source fixes, synthetic fixtures and measured reports. Keep local disc assets, generated game translations, game binaries and RAM/save dumps out of source contributions. Preserve third-party licenses.
- The browser translator (`profile/translator`, `play/translate/`) must keep the mandatory verified-executable SHA-256 check, link the unchanged patched PSPRecomp generator and stay byte-identical to native generation (`scripts/test_web_translator.mjs`). Keep its decrypter an independent implementation. Per the user's explicit 2026-10-18 deployment request, Pages also publishes the verified prepared NCAA player via a hash-pinned GitHub release; game binaries remain outside the Git source tree. Never publish the ISO, extracted disc tree, RAM dumps or saves.
- Static hosting uses `scripts/prepare_static_ncaa.py`; see `docs/STATIC_HOSTING.md`. Preserve local ISO revision checks, startup gating and range reads without uploading the selected file. Static packages contain compiled game code and verified boot preload; keep them under ignored `build/`. Verify both the header-enabled path and plain-server service-worker fallback when changing isolation or bootstrap behavior.
- Prepared static packages are the ISO-only player launcher. Finish isolation before selecting a File, validate the disc before downloading the engine, and reuse that verified index in preRun without a second picker/read. Keep the source-only build importer and experimental translator separate from this flow. The pulled decrypter rejects the tested real disc tag `0xC0CB167C`; do not advertise playable in-browser ISO compilation from its synthetic tests.

Finite GE transforms use `profile/host/ge_math.hpp`. Preserve per-term alignment before cancellation and column-major P * (V * W) grouping. Lit/skinned clip coordinates use the combined model-to-clip matrix; do not substitute rounded world positions. UV scale product truncation precedes offset addition; through UVs bypass it. Keep the cancellation/grouping and lit/unlit UV fixtures. The finite-row oracle comparison does not certify other GE stages. Report ROI regressions and measured native replay cost; do not label it browser FPS.

Viewport XYZ uses the measured reciprocal and separate normalized products, then carry-preserving center addition, screen-grid flooring and integer drawing-offset subtraction. Keep raw screen centers in `viewport_`; algebraically removing offsets early changes GE precision. The old transformed triangle/line 0.375-subpixel bias is removed. `ge_reciprocal_data.hpp` records independently fitted reference-output observations with provenance. Vertex viewport acceptance does not establish fragment UV division, clipping, lighting or nonfinite accuracy. Preserve the coverage/depth regressions and report native cost separately from browser FPS.

UV/fog raster setup uses independently derived `ge_plane_math.hpp` and production GLSL in `ge_plane_shader.hpp`. Preserve shared 15-bit quantization, signed gradient flooring, anchor selection, fragment product precision and rectangle shared-plane markers. LOD uses a difference of separately quantized logs and the covered four-pixel span's Q, not a log of their ratio. Keep the integer coverage safety proof before choosing the 32-bit shader; do not introduce a precision epsilon or remove the full edge path. Metadata atlases append/rotate without framebuffer readback. Original primitive ordering remains necessary for blend/feedback grouping. Run the `ge_plane` suite, including atlas reuse, as well as ordinary graphics tests. The software diagnostic UV backend is still approximate; do not use it as a new acceptance oracle. Native replay timings must control/report process power throttling and exclude concurrent replay/build work; never change global power settings or label those timings browser FPS. Latest fixed-plane evidence/package is in HANDOFF.md.

Replay performance uses the published native `pspweb_ge_replay` diagnostic and `scripts/benchmark_ge_replay.py`; see `docs/GE_REPLAY_PERFORMANCE.md`. Keep option/environment parsing outside record loops. The old harness's absolute times included substantial diagnostic overhead. Use identical harness/compiler/core and explicit warmup, EGL frame completion, process-local power policy and output hashes for matched comparisons. Never treat replay timings as browser FPS. Immutable metadata atlases upload only used single-row cells; retain 64-cell record addressing and full multi-row uploads. Unused cells may contain old data and must not be sampled. Keep texture-unit cache invalidation and mixed partial/full-row atlas-reuse tests. Report worsening medians/tails alongside wins.
