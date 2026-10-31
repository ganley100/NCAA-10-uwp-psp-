# Ways this NCAA port can help other recomp projects

This guide describes reusable engineering work from the NCAA Football 10 PSP web port. A working NCAA scene is evidence for that scene; another title still needs its own executable identity, HLE coverage and gameplay/reference tests.

## Reusable contributions

| Area | Starting point here | Useful contribution to another project |
| --- | --- | --- |
| Static compiler semantics | `PSPRecomp`, `patches/0006-ncaa10-vfpu-and-aot-audio.patch`, `scripts/test_ncaa_codegen.py` | A small synthetic instruction fixture with correct register, memory and delay-slot results |
| Embedded executable programs | `scripts/generate_ncaa_sound.py`, `profile/host/ncaa_sound.cpp` | A documented AOT extraction/binding method for relocated code stored outside the main ELF |
| Disc and host ABI | `profile/host/io.cpp`, `webfs.cpp`, `scripts/manifest.py`, `serve.py` | Sector/byte boundary, 64-bit argument alignment, Range-read and Windows newline regressions |
| Kernel behavior | `profile/host/kernel.cpp`, scheduler and utility tests | Minimal callback, thread lifetime, startup stack, semaphore or timing reproductions |
| Memory diagnostics | `ncaa_heap_trace.hpp`, `ncaa_pointer_trace.hpp`, `scripts/analyze_ncaa_heap.py` | Request-linked allocation/free history, last-writer provenance and allocator integrity checks |
| Rendering | `profile/host/ge.cpp`, `ge_gl.cpp`, `profile/tests/ge.cpp` | Tiny display lists and expected pixels for lighting, texture coordinates, fog and state changes |
| Browser audio | `profile/web/shell.html`, `scripts/test_web_audio.mjs` | Sample-rate, stereo/chunk, drift, reset and underrun recovery tests |
| Static hosting and local disc reads | `profile/web/local_iso.js`, `static_bootstrap.js`, `scripts/prepare_static_ncaa.py` | Lazy ISO indexing, revision checks, raw-sector ranges and startup gating; adapt title identity and AOT boot requirements for each port |
| Input and dialogs | `scripts/test_web_input.mjs`, `test_web_osk.mjs`, host controller/utility tests | Focus-loss release, disconnect, neutral rearm and dialog input suppression fixtures |
| Save validation | `profile/host/savedata.cpp`, NCAA save manifests | Export/hash/reload procedures that compare observable state across fresh processes |
| Pacing diagnosis | GE worker and presentation tests, `?profile` timing | Matched scenes separating game CPU, graphics queue, callback gaps and presented frames |

Paths are relative to the repository root containing `scripts` and `profile`. Existing synthetic fixtures can be shared with their source/license context. Actual game-derived corpora, disc assets and captured guest RAM are local diagnostic material, not portable test fixtures.

## A practical bring-up sequence

1. Identify the new title, region and decrypted executable hash. Give it a separate generated directory, build output and save origin. Never link its assets against NCAA's corpus.
2. Generate AOT code and first run a native validation build. Record the earliest missing import, unsupported instruction or incorrect state instead of treating the final crash as the cause.
3. Implement required PSP behavior with a synthetic regression. Preserve alignment, aliases, calling convention, delay slots and yields. Use primary hardware/runtime reference material for disputed semantics.
4. Validate boot, Start, menus, asset loading, actual gameplay and save/reload separately. Include a representative cold load; a warm cache can hide ordering problems.
5. Compare a repeatable scene with a PSP/reference implementation. Match title hash, inputs, settings and memory layout. Record unverified differences rather than calling a screenshot accurate by appearance alone.
6. Build for the browser with isolated-worker headers and disc streaming, then verify browser-specific input, storage, graphics and audio behavior.
7. Profile correctness-preserving work before optimizing. Record a sustained sample and transitions, not just the best settled FPS counter.

## Lessons to transfer carefully

**Trace pointer provenance across threads.** An asynchronous reader can be where a bad pointer is consumed. Tie allocator return, descriptor construction, submission, stack load and final access together with a request ID. Record last writers and pre-fault state. Keep instrumentation observational and compare diagnostic/production behavior.

**Measure usable contiguous space.** NCAA had enough aggregate free bytes but no suitably aligned block for one request. Compare pool boundaries, headers, alignment, live allocations and coalescing before growing memory. This port's 64 MiB default is a title-specific mitigation; it is not evidence that all PSP titles need 64 MiB or that the original 32 MiB discrepancy is fixed.

**Separate guest RAM from WebAssembly capacity.** Increasing Emscripten linear memory alone does not expand a game's SysMem partition. A coherent larger layout requires backed guest memory, dynamic allocation limits, valid aliases, stack placement, framebuffer checks and upper-memory regression tests.

**Look for code outside the main executable.** NCAA's EA sound banks contain executable programs. Statically compiling their known instruction sequences and binding relocated addresses preserved the AOT architecture. Other games need their own identification and relocation checks; do not scan and execute arbitrary data as a shortcut.

**Distinguish timing quantities.** Vblank cadence, simulation speed, display submission rate, presented FPS and audio sample production are separate. Do not force a game's timing to match a different title's FPS. Measure hot instrumentation itself so diagnostics do not become the performance problem.

**Replay the same graphics input.** A verified PPSSPP GE dump can isolate renderer behavior from CPU progression. Keep original captures local, record title/hash/scene/settings, validate the capture structure, then compare matching outputs. A native window screenshot can supply a reference when debugger readback fails, but retain scaling/crop limitations. Protocol raw pixel base64 needs format/stride/flip metadata; an encoded PNG URI is a different representation.

**Check blend algebra at saturation.** Mapping PSP `2*alpha` to ordinary host source alpha darkens destination modulation. For a zero source contribution, `2*a*D = a*D + a*D` and `(1-2*a)*D = (1-a)*D - a*D` fit normal host blend operands without prematurely clamping the doubled factor. Test alpha above 0.5, inverse factors and state reset. This correction covers that specific algebraic case; it does not establish all PSP blend modes accurate. Depth storage precision also matters: fractional drawing depths that share one integer 16-bit value must compare accordingly.

## Preparing a useful patch or report

**Quantize interpolated colors before pixel operations.** The chosen x64 software reference rounds interpolated primary/secondary colors to nearest-even bytes, then truncates later texture/blend divisions. A fractional background gradient can propagate a one-unit error into otherwise correct text blending. Test gradients, exact ties, alpha-test boundaries and textured/untextured paths. Skip zero secondary colors only with vertex-driven state tracking and transition coverage. Reference architectures differ here; verify hardware rather than promoting one backend's rounding into an unqualified PSP rule.

**Test texture filtering beyond half-texel averages.** A GL-rounded result can differ from a four-bit weighted byte result even when the texture has no mip chain. Include alpha-test outcomes, negative coordinates, fixed-point boundaries, wrap/clamp and exact expected bytes; a one-unit tolerance can hide the bug. Keep GPU surface windows/scaled textures in the coverage plan. A pixel-center change that improves a prefix but worsens complete frames needs further diagnosis before adoption.

**Filter GPU views with guest format and memory semantics.** Expand packed source alpha before weighting; masking the final alpha test does not make the same source-bit mask safe before interpolation. Wrap/clamp each tap inside its window, then translate the surface origin. Verify min/mag selection without a mip chain and preserve distinct samples at increased resolution. Overlapping source feedback requires fresh snapshots even when blending is off, including distinct GPU objects that alias the same guest bytes. Test a format-alias write/read chain with exact packed expected values and no rendering CPU download.

**Inventory primitive modes before tuning scene colors.** Missing line primitives accounted for NCAA field markings. Test endpoint-exclusive strips under blending, indexed pairs, projected UV and line-specific LOD derivatives. Pixel-cell geometry can share the main shader pipeline; merge only identical effective samples, retaining feedback boundaries. An extra fragment varying can affect ordinary draws too: measure a shader-only control with the new primitive omitted before attributing cost to geometry. Keep reference approximations and PSP hardware claims distinct.

**Round blend contributions at the right stage.** A normalized host blend can round the combined result where the guest truncates each contribution separately. Test exact channel values, doubled/inverse factors, subtract/reverse-subtract and dither before saturation. A shader destination snapshot must preserve overlapping primitive order and stencil-alpha updates. Share copies across disjoint interiors/shared edges; source-only equations can skip destination copies, while self-texturing still needs ordered source snapshots. Verify zero CPU downloads and profile copied area as well as draw counts. Native replay timing is not browser FPS, and a better full-frame metric can hide worse text regions.

**Separate texture arithmetic from blending.** GE texture functions consume byte colors with mode-specific rounding; RGB doubling precedes division and does not double alpha. Test all functions with exact alpha-test outcomes, since framebuffer alpha may represent stencil. Use constant non-power-of-two colors and varying clip w to reveal interpolation roundoff. A reference prefix with blending disabled can isolate source-color differences, but only on a preserved diagnostic copy. Report full-scene metrics even when they worsen after a proven stage correction.

**Validate the capture path before chasing pixels.** EGL defaults selected RGBA4444 in the original replay harness and distorted comparisons from correctly rendered RGBA8 targets. Request/query actual channel precision, export the guest-format framebuffer and require presented pixels to match it. Keep references and CPU/direct framebuffer output separate from affected GPU screenshots when correcting earlier reports. A better measurement is not itself a rendering improvement.

**Separate texture opacity from destination blend alpha.** RGB565 samples as an opaque texture but has zero destination alpha for blending. Changing a shared decode helper to make every use transparent breaks the texture path. Cover both behaviors in display-list regressions.

**Presentation owns its sampler.** A bound sampler overrides texture filtering parameters. Clear or replace the game's sampler before presenting; test both inherited linear filtering and inherited mip filtering against an RGBA8 output target. Correct game draw state alone does not establish correct presentation.

**Test dithering across pipeline modes.** Ordered dither changes RGB before saturation and framebuffer conversion; clear mode and 8888 can also expose it. Check all signed nibble values, matrix-only changes, surface origins and internal resolution scaling. Blended output needs dithering after blending; a source-color offset does not implement that order. Preserve saturation-boundary fixtures so an early clamp cannot hide the difference.

**Treat a mip chain as one cache resource.** Include each level's layout and content in cache identity/revalidation, count all decoded texels toward capacity, and keep pending draws from observing mutated data. Test a higher-level-only RAM update and a same-frame VRAM write while the base hash remains unchanged. Use distinct level colors and per-level indexed palettes to reveal incorrect selection and CLUT sharing. Explicit texel fetch still needs an appropriate sampler and backed mip chain on the tested GLES path.

**Keep LOD in guest pixel units.** Host resolution scaling must not silently shift the selected mip. Check automatic derivatives, signed constant bias, slope with varying clip w, nearest-level rounding and interpolation fractions separately. Compare the same captured frame after the change; unchanged draw counts alone do not establish unchanged GPU cost or full browser FPS.

**Keep minification and magnification separate.** GE `TEXFILTER` carries independent settings. Test a striped texture at both shrinking and enlarging scales, change only one filter and revisit the original state. Include wrap/clamp combinations to exercise sampler-cache selection. This validates filtering within one mip level; mip selection and interpolation between levels require their own fixtures.

**Separate reference methods.** A coherent, repeatable software-rendered VRAM capture avoids window scaling, but is still a software reference rather than hardware proof. Report its format, stride, hashes and capture consistency, and retain hardware-window results separately. The same change can improve one metric and worsen the other.

**Validate GPU sampling without readback.** Packing a 16-bit target only when the CPU downloads it leaves GPU texture sampling at the wrong precision. Draw into each target format, sample it into a separate 8888 target and assert that no download occurred. Unblended output can be packed in the shader; blended output needs conversion after the blend equation. A targeted fixture can prove a semantics fix even when a window-derived frame metric gets worse; report both results and retain the unresolved frame differences.

**Verify the reference backend.** Command-line switches differ by platform and version. Windows PPSSPP v1.20.4 selects software with `--graphics=software`; `-software` is parsed as its pause switch. Requested INI settings and filenames do not prove the actual renderer. Reject all-zero or failed readbacks before calculating visual accuracy.

Provide the concrete trigger and observed before/after behavior, the smallest source change, a synthetic regression when feasible, tested platforms/settings and relevant license attribution. State whether the change is general PSP behavior or a title-specific adaptation. Include a repro sequence and compact diagnostic values; share only data suitable for the destination.

A useful bug report might say: “An aligned allocation fails despite sufficient aggregate space; here are the pool bounds, physical/free-list integrity, requested size, alignment and relevant allocation/free order.” It should not conclude “the STORE emitter is broken” solely from a null destination.

Run the target project's tests as well as the fixture that exposed the issue. If a fix depends on a particular compiler/emitter version, record that dependency and regenerate output. Make no compatibility claim for titles not tested.

## References and boundaries

The local [internals guide](docs/internals.md), [engineering log](ENGINEERING_LOG.md) and [crash provenance](docs/NCAA10-CRASH-PROVENANCE.md) are the starting evidence. `patches` preserves framework changes; `profile/host` contains runtime changes; `profile/tests` and `scripts/test_web_*.mjs` contain reusable synthetic checks.

The user also supplied [lcs-recomp](https://github.com/elmasas/lcs-recomp) as a possible reference. It is not a dependency in the current NCAA build path. Inspect its actual code, architecture and license before borrowing any technique; do not assume its game addresses or engine behavior apply to NCAA.

This guide prepares local contributions and reports. It does not itself publish changes or send messages to another project.

Triangle and rectangle sampling can differ even when rectangles are submitted as strips/fans. Recheck rejected pixel-center trials after identifying the affected primitive paths; isolated geometry is insufficient. Preserve UV/interpolation contracts when recognizing rectangles, require a true shared diagonal and test malformed quads, gradients, perspective attributes and adjacent alpha blending. NCAA’s combined grid/center/rectangle fix improves both complete reference frames without an extra varying or rendering CPU download.

Reference versions can encode different arithmetic, so a near-perfect emulator comparison can still diverge from hardware. Pin binary/archive/source hashes and run the reference's measured-math tests. Compare identical frames across oracle versions before retuning production, and retain regressions and historical metrics with their original oracle. The shared `scripts/capture_ge_reference.py` records repeated headless software captures with isolated state and rejects wrong provenance, black output and overwrites. NCAA's reference migration also exposed 16-byte texture rows, separate bilinear truncation and floored channels; its affine GPU transport retains perspective UV without a color-rounding epsilon. Exact hardware transforms/planes remain work rather than a claim of full compatibility.

Float24 transforms require more than masking a float32 dot result: align each product before cancellation, and preserve the measured matrix grouping. NCAA uses an independently derived integer helper in `profile/host/ge_math.hpp`, checked against 1.1 million pinned reference rows. Its synthetic matrix/UV pixel fixtures reproduce old-source failures without game assets. Test represented subnormal outputs separately from flushed inputs and separate UV product/add stages. Report small ROI regressions and transform cost even when every full/prefix MAE improves; this checkpoint increases native medians by 1.58% / 3.11%. Nonfinite, reciprocal/viewport, normals/fog/lighting, full skin/morph and fixed planes need their own evidence.

Viewport centers and drawing offsets are separate arithmetic stages. Apply the measured reciprocal and normalized products, add the raw screen center with the carry-preserving GE adder, floor to 12.4 coordinates, then subtract the integer drawing offset. Pre-subtracting the offset changes precision even when the algebra looks equivalent. NCAA’s captured draw showed a one-subpixel visible discrepancy; the correction lowers complete reference-frame MAE by about 74% / 82%. Reciprocal constants were fitted independently from reference-function outputs; exhaustive normal float24 checks and actual edge/depth fixtures accompany the source. Do not claim that correcting the vertex viewport also fixes per-fragment UV division or the near clipper. Native timings rise 1.14% / 0.63%; browser FPS and physical hardware agreement need their own verification.

## Fixed-point raster setup and GPU transport

`profile/host/ge_plane_math.hpp` provides independent setup reciprocal, anchor and shared-value plane construction; `ge_plane_shader.hpp` evaluates integer planes, measured reciprocals, fragment products and span coverage using GLES3/WebGL2 operations. `profile/tests/ge_plane.cpp` supplies asset-free arithmetic/edge comparisons and an atlas reuse stress test. The graphics suite includes UV truncation, rectangle anchoring, subtexel filtering and a reference-corrected slope LOD fixture. Reuse the invariants and synthetic tests, then collect the other title's own full/prefix/ROI captures. Factual setup/reciprocal observations retain their PPSSPP hardware-documentation provenance; reference GPL source and local game dumps are excluded.

Per-primitive metadata can remove unnecessary UV/W varyings while retaining the original geometry for ordered blend and texture feedback. Append-only rotating integer atlases avoid overwriting records needed by earlier GPU commands. A narrow shader may omit wide edge operations only after an exact bound proves equivalent results. The native timing investigation also demonstrates why process power throttling must be recorded: both unchanged and candidate replays slowed abruptly, and opting only the benchmark process out removed that cliff. This does not establish browser FPS or justify changing a user's global power settings.

## Reusable replay measurements

The native `pspweb_ge_replay` target and `scripts/benchmark_ge_replay.py` provide explicit warmup, RGBA8 presentation/direct-RGB565 verification, source hashes, ABBA samples and rendering/bandwidth counters. The runner rejects invalid samples, mismatched images and destructive output paths. Options are parsed before replay; per-record environment lookups previously dominated the apparent cost. EGL frame completion and process-local power policy are recorded separately from production behavior. Synthetic runner and CLI fixtures are in `scripts/test_ge_replay_benchmark.py` and `scripts/test_ge_replay_cli.py`; no disc or game dump is published. See [the measurement guide](docs/GE_REPLAY_PERFORMANCE.md). Adapt title/RAM metadata and display validation explicitly for another title, and retain image equality when measuring a performance-only change.
