# GitHub publication and source preparation

Target: [awest813/NCAA-10-PSP-Recomp](https://github.com/awest813/NCAA-10-PSP-Recomp).

On 2026-10-12 the audited 126-file source snapshot was committed and pushed to `main` as [f0a57cb](https://github.com/awest813/NCAA-10-PSP-Recomp/commit/f0a57cb05c295ad56f6effe22313a64b0d31e4e9). The connected GitHub API confirmed that the remote branch matched the local commit. Publication used a normal push after verifying that the repository was empty.

## What is included

The snapshot includes the NCAA-focused README, engineering/handoff/agent/reuse documents, host/runtime source, synthetic tests, browser shell, scripts, seven framework patches and license notices. The inherited God of War demo media remain clearly labeled upstream history. The original MIT notice and FFmpeg/ATRAC LGPL text, source revision and per-file notices are retained.

`PSPRecomp` is reconstructed from its pinned upstream revision plus `patches/*.patch`; its local checkout is not uploaded. Patch 7 preserves the additional instruction-trace, heartbeat, codegen and synthetic-test work missing from patch 6 alone. The complete series was applied to a clean export of the pinned base, reproducing the corresponding working sources; that clean framework built and passed its tests and the standalone NCAA codegen regression.

## What stays local

`games`, game ISO files, `profile/generated`, `build`, SDK/tool binaries, RAM/trace dumps and memory-stick/save fixtures are excluded. The snapshot is source and documentation, not a playable game download. A fresh user supplies the matching decrypted executable and disc, then generates the AOT corpora. Artifact paths in engineering documents refer to this local workspace and will not exist in a fresh source checkout.

`scripts/check_source_tree.py` uses an explicit file allowlist and checks selected credential patterns without printing matching values. This is a preparation check, not an exhaustive secret-detection guarantee. `--tracked` checks every index entry, including accidentally staged forbidden files.

## Create or refresh the snapshot

From the inner project directory:

```powershell
python scripts/check_source_tree.py
python scripts/prepare_github.py --output build/github-ready-20261012
```

The published checkout uses `build/github-ready-20261012`; its preparation ZIP and per-file SHA-256 manifest are `build/github-ready-20261012.zip` and `build/github-ready-20261012-manifest.json`. Those dated artifacts describe the initial source snapshot before this publication-status update; Git records subsequent changes. The earlier `build/github-ready` folder is a preliminary audit snapshot. The packager refuses to overwrite an earlier snapshot; use a new output name when refreshing it. It never pushes.

## Prepare another source checkout

The prepared folder can be initialized independently, keeping the active game/debug workspace unchanged:

```powershell
cd build/github-ready-20261012
git init -b main
git remote add origin https://github.com/awest813/NCAA-10-PSP-Recomp.git
git add .
$shellFiles = Get-ChildItem scripts -Filter '*.sh' | ForEach-Object { 'scripts/' + $_.Name }
git update-index --chmod=+x -- $shellFiles
python scripts/check_source_tree.py --tracked
git diff --cached --stat
git diff --cached --check -- . ':(exclude)patches/*.patch'
git status --short
```

The published Git folder already has `main` tracking `origin/main`. Skip initialization and remote creation when using that checkout. The commands above describe preparing a new independent source snapshot. Patch files are excluded from the ordinary whitespace check because unified-diff context includes intentional spaces; the separate clean-base patch application verifies them.

For subsequent updates, review the staged files and source changes, then commit using your configured Git identity. Fetch and integrate remote changes before a normal push to `main`; do not force-push. For a new development checkout, clone the existing repository so its published history is retained.

On a clean source checkout, use `bash scripts/setup.sh --no-emsdk` to fetch/apply the framework and build native tools. Windows users need a working Git Bash environment for those shell setup scripts. Install/select Emscripten for the web build, supply the verified decrypted EBOOT and disc, and follow [docs/NCAA10.md](docs/NCAA10.md). Regenerate with `scripts/build_ncaa_web.ps1 -Generate` because generated sources are intentionally absent.

The source audit added `.github/workflows/source-checks.yml` for game-free source, browser and patched-compiler checks. Its hosted result is recorded separately when available. The existing Windows game target rebuilt successfully after the audit, but a fresh full game build from this source-only checkout and first-time Windows/Linux setup remain additional checks. Follow the [README fresh-checkout instructions](README.md#build-ncaa-from-a-fresh-checkout) for the current commands.
