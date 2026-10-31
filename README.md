# NCAA Football 10 PSP Web Recomp

> **Xbox One / UWP fork.** This repository adds an experimental UWP `.msix` build for Xbox One (Developer Mode) on top of [awest813/NCAA-10-PSP-Recomp](https://github.com/awest813/NCAA-10-PSP-Recomp). See [docs/XBOX.md](docs/XBOX.md) and the *Xbox UWP package* workflow.

Play **NCAA Football 10 for PSP (US, ULUS10440)** in a web browser using your own disc image. The game's Allegrex executable is translated ahead of time to C++, compiled to WebAssembly, and linked with a PSP host runtime and WebGL2 renderer. Gameplay executes the original game code without a guest CPU interpreter or JIT.

**[Open the player](https://awest813.github.io/NCAA-10-PSP-Recomp/)** · [NCAA guide](docs/NCAA10.md) · [Known gaps](docs/REMAINING_GAPS.md) · [Build from source](#build-ncaa-from-a-fresh-checkout)

## Play

1. Open the [hosted player](https://awest813.github.io/NCAA-10-PSP-Recomp/) in a browser with WebGL2 and shared-memory support. Chrome on Windows is the current verified gameplay baseline.
2. Choose an **uncompressed US NCAA Football 10 PSP ISO** matching `ULUS10440` and the supported executable revision. The picker does not accept ZIP, CSO or ZSO files.
3. Wait for validation and startup, then press **Enter for Start**. Follow the title and first-run prompts, choose **Play Now**, confirm the teams and controller, and allow the stadium to load.

The ISO stays on your device. The player reads requested file and sector ranges locally; it does not upload or copy the whole disc into WebAssembly memory. Select the ISO again after reloading the page.

The site supplies a prepared, game-specific WebAssembly engine and verified boot preload from a hash-pinned release. It hosts no ISO, extracted disc tree or saves. Players do not need a compiler or build-folder import. Release provenance is recorded in [PAGES_PLAYER.json](docs/PAGES_PLAYER.json); source updates and deployed engine updates are tracked separately.

### Controls

| Keyboard | PSP input |
| --- | --- |
| Enter | Start |
| Z / X / A / S | Cross / Circle / Square / Triangle |
| Arrow keys | D-pad |
| I / J / K / L | Analog up / left / down / right |
| Q / W | L / R |
| Shift | Select |

At kickoff, Z selects **Kickoff Middle**. Hold K briefly to pull the analog stick back, then I to push forward and kick. Analog keys move the selected player during regular play. See the [match walkthrough](docs/NCAA10.md#controls-and-path-into-a-match) for the first-run menu sequence.

The web shell also provides gamepad and touch controls. Physical controller, Bluetooth and mobile-device coverage remain incomplete.

### Saves and rosters

Progress persists in browser storage for the site's origin. Use **Export saves** to download a backup ZIP and **Import saves** to restore a compatible archive. Export before clearing site data or moving between domains, ports, browser profiles or devices.

The runtime does not decrypt PSP saves. Encrypted saves or rosters copied from a PSP or PPSSPP require a decrypted copy; recognizable encrypted NCAA save pairs are rejected before import changes existing savedata. Imports validate ZIP integrity and paths, then report success after storage confirms persistence.

## Current status

Actual browser play has been verified beyond menus: stadium loading, coin toss, kickoff and returns, offensive and defensive scrimmage, and subsequent play selection. A separate run reached postgame using Super Sim for part of the match. These checks do not establish complete mode or season coverage.

| Area | Verified state and remaining limits |
| --- | --- |
| Memory | NCAA defaults to **64 MiB guest RAM / 56 MiB user partition** in native and web builds. `?ram=32` enables comparison with the original layout; the diagnosed 32 MiB fragmentation failure remains open. |
| Performance | Observed gameplay cadence is about **30 presented FPS at 100% simulation speed** on the tested Windows machine. Loading and presentation stalls remain. A 60 Hz Vblank is distinct from the game's presented frame rate. |
| Graphics | WebGL2 renders gameplay with ongoing GE accuracy work. Selected reference comparisons and synthetic regressions do not establish full PSP fidelity. |
| Audio | Music, speech and effects run through the host audio path. Recovery transitions were improved in the October 20 audit; startup starvation and uninterrupted audio remain unresolved. |
| Compatibility | The supported target is the verified US executable. Other revisions, all game modes, long sessions and broad browser/device coverage are not established. |
| Video | The optional NCAA intro movie is skipped; AVC playback is not implemented. |

The [NCAA guide](docs/NCAA10.md#validation-and-current-limits), [audio and pacing audit](docs/AUDIO_PACING.md), and [remaining gaps](docs/REMAINING_GAPS.md) explain the evidence and its limits. The October 20 Chrome audit reached kickoff and subsequent play selection with a sampled median of 29.5 FPS in its final 45-second window; startup/loading had already incurred audio underruns. This is a local observation, not a performance guarantee.

## Build NCAA from a fresh checkout

A fresh clone contains source only. Supply your own matching US disc and verified decrypted executable. Game files, generated translations, builds and savedata are excluded from version control.

Run all commands from the repository root containing `scripts` and `profile`. In the original local workspace, that is the inner `psp-web-recomp-main` directory.

### 1. Install dependencies and set up the compiler

Install Git, Python 3.10+, CMake, Ninja and a C++20 compiler. Windows setup uses Git Bash and requires Clang on `PATH`.

```bash
git clone https://github.com/awest813/NCAA-10-PSP-Recomp.git
cd NCAA-10-PSP-Recomp
bash scripts/setup.sh
```

Setup fetches the pinned [PSPRecomp](https://github.com/jessicanataliagta/PSPRecomp) revision, applies seven patches, links this project's profile, installs Emscripten 6.0.10 under `tools/emsdk`, and builds the native compiler tools.

### 2. Supply the verified executable

Create `games/ncaa10/root` and place the matching **decrypted** executable at:

```text
games/ncaa10/root/EBOOT.BIN
```

The supported file is 8,415,696 bytes with SHA-256:

```text
2b743dc6d746d822969a9b967b13422a369ff03c6a3a3e2604e051c1642b94f6
```

A retail encrypted `EBOOT.BIN` cannot be used directly. PPSSPP can dump a decrypted executable through its developer tools; see [verified game identity](docs/NCAA10.md#verified-game-identity). The ELF's internal module name, `NCAA2007`, belongs to this Football 10 executable.

**The experimental browser translator is not a supported way to prepare this disc.** The actual-disc audit rejected encryption tag `0xC0CB167C`. Its synthetic tests do not establish retail compatibility, and compiling a playable game engine in the browser is not implemented. See [translator status](docs/STATIC_HOSTING.md#translator).

### 3. Build and run

On **Windows**, run from PowerShell:

```powershell
./scripts/build_ncaa_web.ps1 -Iso 'C:/path/to/NCAA-Football-10.iso' -Emsdk ./tools/emsdk -Generate -Serve
```

This extracts the disc as needed, verifies the exact title and executable, prepares raw UMD reads, generates both the main executable and EA audio corpora, and builds the web target. `-Serve` then creates a fresh static package and serves its ISO-only launcher. Open [localhost:8621](http://127.0.0.1:8621/), select the ISO and press Enter.

For subsequent incremental builds, omit `-Generate`. Use `-StaticOutput build/my-new-launcher` to choose a new package directory. Existing package directories are preserved.

On **Linux**, with the verified decrypted executable already in place:

```bash
scripts/port.sh ncaa10 '/path/to/NCAA-Football-10.iso'
python3 scripts/prepare_static_ncaa.py --output build/static-ncaa10
python3 scripts/serve.py build/static-ncaa10 8621
```

Open [localhost:8621](http://127.0.0.1:8621/) and select the ISO. Choose a new output directory when packaging again. Windows gameplay is the verified baseline; first-time Linux setup and a full Linux game build still need end-to-end validation. macOS is untested here.

Both build paths verify disc metadata and executable identity before linking NCAA code. They support an ISO or a ZIP containing one ISO as build input; the player picker still requires an uncompressed ISO. Existing raw images are checked by SHA-256 and preserved on mismatch.

### Serve an existing development build

The development server can stream from the local extracted disc instead of asking for an ISO:

```powershell
python scripts/serve.py build/web-ncaa10/profiles/web 8621 --mount /disc=games/ncaa10/root/disc
```

Keep the extracted disc and `disc/__umd.iso` in place. Check for an existing server before reusing its port. Full build and native diagnostic details are in the [NCAA guide](docs/NCAA10.md) and [handoff](HANDOFF.md).

## Host your own player

After building, prepare a new static package and preview it:

```powershell
python scripts/prepare_static_ncaa.py --output build/static-ncaa10
python scripts/serve.py build/static-ncaa10 8622
```

Publish the package contents together on an HTTPS static host. The package contains compiled NCAA code and a verified boot preload; it contains no ISO or savedata. Keep game-derived outputs outside the Git source tree.

Threaded WebAssembly requires cross-origin isolation. `scripts/serve.py` supplies the headers locally; the static package includes a service-worker fallback for hosts without configurable headers. Use HTTPS or localhost, allow service workers where needed, and serve `.wasm` as `application/wasm`. Opening `index.html` directly from disk does not work.

See [Static hosting](docs/STATIC_HOSTING.md) for headers, deployment, package verification and the optional developer build importer.

## How it works

- **Ahead-of-time code:** PSPRecomp translates the verified executable into C++. EA audio-bank programs have a separate generated corpus, including relocated guest jumps. Emscripten compiles these with the host runtime into WebAssembly.
- **PSP services:** `profile/host` implements threads, synchronization, memory partitions, file and sector I/O, input, save dialogs and other kernel services used by the game.
- **Graphics:** the CPU decodes GE display lists and vertex state; the WebGL2 backend handles render targets, textures, blending and presentation. A graphics worker with OffscreenCanvas overlaps GE work with game execution on supported browsers.
- **Audio:** voice synthesis and ATRAC3/ATRAC3+ decoding feed an AudioWorklet. Buffering and recovery diagnostics track starvation and overruns.
- **Disc access:** prepared players bridge local ISO ranges into the disc cache. Development builds can stream extracted files and raw sectors through the local HTTP server.

See [runtime internals](docs/internals.md) for implementation details and [Helping other recomps](OTHER_RECOMPS.md) for reusable fixes and investigation methods.

## Documentation and contributions

| Document | Use it for |
| --- | --- |
| [NCAA guide](docs/NCAA10.md) | Identity, controls, builds and gameplay evidence |
| [Static hosting](docs/STATIC_HOSTING.md) | Local-ISO packages, Pages deployment and developer tools |
| [Remaining gaps](docs/REMAINING_GAPS.md) | Open compatibility and accuracy work |
| [Audio and pacing](docs/AUDIO_PACING.md) | Measured stalls, audio recovery and validation limits |
| [Handoff](HANDOFF.md) | Current reproducible development state |
| [Engineering log](ENGINEERING_LOG.md) | Dated changes, checks and evidence |
| [Agent instructions](AGENTS.md) | Requirements for work in this repository |
| [GitHub publication](GITHUB_READY.md) | Source publication and contribution workflow |
| [Helping other recomps](OTHER_RECOMPS.md) | Reusable source fixes and synthetic tests |

Contribute host/compiler fixes, synthetic fixtures and reproducible reports. Preserve title checks, ahead-of-time execution and the NCAA memory defaults. Keep disc assets, generated game translations, game binaries, RAM dumps and saves out of source contributions. Read the agent instructions and handoff before changing the runtime.

## Credits and license

This port extends [Samir Nuri's PSP Web Recomp](https://github.com/snuri00/psp-web-recomp), originally developed for God of War: Chains of Olympus and Ghost of Sparta. Their bring-up and performance history belongs to those titles; it is separate from NCAA's gameplay evidence.

[PSPRecomp](https://github.com/jessicanataliagta/PSPRecomp) supplies the static compiler. PPSSPP and JPCSP research informs hardware behavior; the standalone FFmpeg ATRAC decoder is used under its own license. Emscripten builds the WebAssembly. Attribution and dependency details are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Repository code is available under the [MIT License](LICENSE). The ATRAC decoder retains its [LGPL 2.1-or-later license](profile/third_party/at3_standalone/COPYING.LGPLv2.1) and per-file notices.

Use disc images of games you own. This project is not affiliated with or endorsed by EA, the NCAA, Sony or any game publisher.
