# Xbox One / UWP package

This fork adds an experimental UWP build of the native SDL player so the
recompiled game can be sideloaded on an Xbox One / One S / One X (or Series)
in **Developer Mode**, or installed on a Windows 10/11 PC.

Status: experimental. The UWP app runs the native player with the CPU
(software) GE renderer and SDL2's WinRT backend for the window, Xbox
controller and audio. The ANGLE (GLES over Direct3D 11) libraries are linked
for the GL renderer, which is not enabled in the SDL player yet. Expect low
frame rates on the Xbox One S CPU.

## What gets built

`scripts/build_uwp.ps1` (also run by `.github/workflows/uwp.yml`) produces
`build/uwp-<game>/package/`:

| File | Purpose |
| --- | --- |
| `*.msix` | The app package, signed with a self-signed certificate |
| `PSPRecompUWP.cer` | The public certificate it was signed with |
| `Microsoft.VCLibs.x64.14.00.appx` | C++ runtime framework dependency |
| `INSTALL.md` | This file |

Two variants:

- **smoke**: no game data. A tiny synthetic PSP program. It installs, opens,
  runs and exits, which proves the toolchain and the install path. Built on
  every push.
- **ncaa10**: NCAA Football 10 (US, ULUS10440) from **your** ISO and **your**
  decrypted `EBOOT.BIN`. The package contains code translated from your game
  and your extracted disc, so it must stay private.

## Building NCAA Football 10 with GitHub Actions

1. Make this repository **private** (Settings > General > Danger Zone > Change
   visibility). The workflow refuses the game build in a public repository,
   because artifacts of public repositories can be downloaded by anyone.
2. Create `ncaa10-input.zip` containing your NCAA Football 10 `.iso` and your
   decrypted `EBOOT.BIN` (SHA-256 `2b743dc6...94f6`, see
   [docs/NCAA10.md](NCAA10.md#verified-game-identity); PPSSPP's developer tools
   can dump it). Each release asset must be under 2 GB. Optionally add a
   square `cover.png` (or `cover.jpg`) to use your box art for the tiles.
3. Create a release with the tag `game-input` and attach `ncaa10-input.zip`.
4. Actions > **Xbox UWP package** > Run workflow > game `ncaa10`.
5. Download the `xbox-uwp-ncaa10` artifact when the run finishes.

## Building on your own PC

Install Visual Studio 2022 with **Desktop development with C++** and
**Universal Windows Platform development** (tick *C++ (v143) Universal Windows
Platform tools*), plus Git for Windows and Python 3. Then in PowerShell:

```powershell
git clone https://github.com/ganley100/NCAA-10-uwp-psp-.git
cd NCAA-10-uwp-psp-
./scripts/build_uwp.ps1 -Game ncaa10 -Iso C:\path\NCAA10.iso -Eboot C:\path\EBOOT.BIN -Cover C:\path\cover.png
```

`-Cover` is optional; without it the package uses the generic icons in
`uwp/Assets`.

## Installing on an Xbox One S

1. **Developer Mode**: register a Microsoft Partner Center developer account
   (one-time fee), install **Xbox Dev Mode Activation** from the Store on the
   console, follow its steps and restart into Dev Mode.
2. In **Dev Home**, enable **Remote Access** (Xbox Device Portal) and set a
   username and password. Note the console's IP address.
3. On your PC, open `https://<xbox-ip>:11443` and accept the certificate
   warning.
4. **Home > Add**: choose the `.msix`, click Next, add
   `Microsoft.VCLibs.x64.14.00.appx` as a dependency (and the `.cer` if the
   page asks for a certificate), then click Start and wait for the install.
5. In Dev Home or Device Portal, find the app, open **View details** and set
   the app type to **Game**. Games get far more memory and CPU than apps.
6. Launch it from Dev Home (or Collections).

To install on a Windows PC instead, double-click `PSPRecompUWP.cer`, install it
to *Local Machine > Trusted People*, then double-click the `.msix`.

## Controls

| Xbox controller | PSP |
| --- | --- |
| A / B / X / Y | Cross / Circle / Square / Triangle |
| LB / RB | L / R |
| D-pad, left stick | D-pad, analog |
| Menu / View | Start / Select |

## Files on the console

Through Device Portal's **File explorer**, under
`LocalAppData\<package>\LocalState`:

- `pspweb.log` is the log of the last run. Read it when something goes wrong.
- `ms0\` holds your saves.
- `game\` is optional. A `game\EBOOT.BIN` plus `game\disc\` placed here
  overrides the game packaged in the `.msix`.
