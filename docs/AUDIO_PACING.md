# Audio and pacing audit — 2026-10-20

This checkpoint uses NCAA Football 10 PSP US (ULUS10440), the existing AOT runtime and 64 MiB guest / 56 MiB user RAM. User saves and browser profiles were preserved.

## Fixed audio discontinuities

The browser worklet previously jumped directly to silence on starvation/reset and directly to a different sample when trimming its queue or resuming. It now applies two-millisecond attack, exhausted-tail, splice and reset transitions. Ordinary queued PCM/resampling remains unchanged outside these short windows. Starvation also clears accumulated clock correction before re-priming. The target remains 4,410 source frames (100 ms); the cap remains 11,025 (250 ms). Underrun and exact discarded-frame counters remain visible and unchanged for the tested inputs.

A deterministic stereo constant-signal interruption fixture measures the largest adjacent left-channel sample step across startup, starvation, opposite-polarity recovery, queue trimming and reset:

| Device rate | Before | After |
| --- | ---: | ---: |
| 22,050 Hz | 1.500000 | 0.034091 |
| 44,100 Hz | 1.500000 | 0.017045 |
| 48,000 Hz | 1.500000 | 0.015625 |
| 96,000 Hz | 1.500000 | 0.008503 |

Both versions report one starvation and one overrun, with identical dropped-frame totals at each rate. These are synthetic sample discontinuity measurements, not a perceptual score or proof that loading gaps have disappeared. Regression coverage also includes an immediate full-buffer refill after reset, split stereo ramps, steady production, queue bounds and exact drops.

AudioWorklet load/processor failures now close the failed graph and allow the next user interaction to retry. Worklet Blob URLs are released after loading, and rejected resume promises are handled. Tests exercise failed loading, failed processing and successful gesture retry.

## Actual browser observations

Separate fresh headless Chrome 155.0.8059.85 sessions used the real ISO at the default 3x desktop resolution. Logs identify the device graph as 48 kHz. The original player held approximately 30 FPS in settled menus. Stadium loading produced long main-thread tasks and audio delivery gaps: the first stadium run recorded a maximum 555 ms gap and six accumulated underruns. Its settled stadium/coin-toss observation retained six underruns and zero overruns, while presented frame rates varied roughly 18–25 FPS and the graphics queue backed up. A separate run had four loading underruns and subsequently held near 30 FPS. These are separate sessions, not an A/B performance improvement.

In one later session, the settled scene was tested sequentially at 3x, 2x, native and 3x again. Sampled median FPS was 30.0 / 29.9 / 30.0 / 30.0, with no added underruns or overruns. Median GE time stayed approximately 9.4–9.8 ms per guest step. This does not justify a resolution-default change or a universal performance recommendation. Driver warm-up, workload/scene variation and long loading tasks still require targeted tracing. A short successful scene is not sustained full-match or audible hardware-equivalence acceptance.

Private evidence is under `build/audio-audit-20261020/`: original `baseline.json` is a menu/team-selection run (its historical field label is not gameplay); `baseline-field.json` covers the stadium/coin-toss path; `baseline-scales.json` contains the sequential resolution experiment; `click-metrics.json` records the deterministic worklet comparison. No game assets, captures or saves are source contributions.

## Rebuilt player verification

The rebuilt package reached kickoff and subsequent first-down play selection in a fresh profile, with no page errors. Sampled median FPS was 30.0 in menus, 29.0 in the stadium sequence, 28.2 during the next field/coin-toss phase and 29.5 during the final 45-second play-selection observation. Audio counts stayed 2/2 through the menu sample, 8/8 through stadium loading's settled aftermath and the next field sample, and 9/9 through the final sample; overruns/drops stayed zero. The loading path recorded a 2,349 ms maximum delivery gap, including a 2,279 ms main-thread long task. This is evidence that loading starvation remains, not an improvement claim from different team selections or runs. The final historical live label in the JSON includes play selection; screenshots, rather than labels alone, establish the actual game state. `after.json`, its console log and screenshots preserve this check.

## Reproduce checks

Run `node scripts/test_web_audio.mjs`, input/save/persistence/OSK browser logic regressions, `scripts/test_iso_launcher_browser.mjs` in Chrome, exact title verification and static-package tests. The prepared output is `build/static-ncaa10-audio-polish-20261020`. Guest instructions, host mixer, scheduling semantics, renderer and RAM configuration are unchanged. The new HTML must be deployed with its static manifest; changing source alone does not update a hash-pinned Pages runtime.
