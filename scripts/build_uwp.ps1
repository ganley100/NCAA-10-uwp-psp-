# Builds a sideloadable UWP .msix for Xbox One (Dev Mode) / Windows 10+ x64.
#
#   scripts/build_uwp.ps1 -Game smoke
#       Game-free smoke package (synthetic PSP program): proves the toolchain,
#       packaging, signing and Xbox install path without any game data.
#
#   scripts/build_uwp.ps1 -Game launcher
#       "NCAA 10 PSP" launcher without any game: finds your game file
#       (Development Files, D:, LocalState, USB) and on Play starts the
#       separately installed "NCAA 10 PSP Game" package (-Game ncaa10).
#
#   scripts/build_uwp.ps1 -Game ncaa10 -Iso C:\path\NCAA10.iso -Eboot C:\path\EBOOT.BIN
#       NCAA Football 10 (ULUS10440) from YOUR disc image and YOUR decrypted
#       EBOOT.BIN (see docs/NCAA10.md).  The result contains code translated
#       from your game and your extracted disc: keep it private.
#
# Requires Visual Studio 2022 with the "Desktop development with C++" and
# "Universal Windows Platform development" workloads (C++ UWP tools), Git for
# Windows and Python 3.  Output: build/uwp-<game>/package/.
param(
    [ValidateSet("smoke", "launcher", "ncaa10")][string]$Game = "smoke",
    [string]$Iso = "",
    [string]$Eboot = "",
    [string]$Cover = "",
    [switch]$PackDisc,
    [ValidateRange(0, 3)][int]$Opt = 1,
    [string]$Version = "1.0.0.0",
    [int]$MsvcJobs = 2,
    [string]$SdlVersion = "2.30.9",
    [string]$AngleVersion = "2.1.13"
)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
$Root = Split-Path (Split-Path $MyInvocation.MyCommand.Path -Parent) -Parent
$Framework = Join-Path $Root "PSPRecomp"
$Out = Join-Path $Root "build/uwp-$Game"
$Deps = Join-Path $Root "build/uwp-deps"
New-Item -ItemType Directory -Force -Path $Out, $Deps | Out-Null

function Invoke-Checked([string]$File, [string[]]$Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$File failed with exit code $LASTEXITCODE" }
}

# --- Visual Studio developer environment --------------------------------------
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio/Installer/vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw "Visual Studio 2022 with C++ tools was not found" }
    Import-Module (Join-Path $vs "Common7/Tools/Microsoft.VisualStudio.DevShell.dll")
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
}
$Sdk = (Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\Include" -Directory |
        Where-Object { $_.Name -match '^10\.0\.\d+\.\d+$' -and (Test-Path (Join-Path $_.FullName "winrt")) } |
        Sort-Object { [version]$_.Name } | Select-Object -Last 1).Name
if (-not $Sdk) { throw "No Windows 10/11 SDK found" }
$SdkBin = "${env:ProgramFiles(x86)}\Windows Kits\10\bin\$Sdk\x64"
Write-Host "== Windows SDK $Sdk"

$Bash = Join-Path $env:ProgramFiles "Git/bin/bash.exe"
if (-not (Test-Path $Bash)) { $Bash = "bash" }
$Python = if (Get-Command python -ErrorAction SilentlyContinue) { "python" } else { "py" }

# --- PSPRecomp + native translator tools --------------------------------------
if (-not (Test-Path (Join-Path $Framework "CMakeLists.txt")) -or -not (Test-Path (Join-Path $Root "build/tools/psp_recomp.exe"))) {
    Write-Host "== setup (PSPRecomp, patches, translator tools)"
    Push-Location $Root
    try { Invoke-Checked $Bash @("scripts/setup.sh", "--no-emsdk") } finally { Pop-Location }
}
$Recomp = Join-Path $Root "build/tools/psp_recomp.exe"
if (-not (Test-Path $Recomp)) { throw "psp_recomp.exe was not built" }

# --- SDL2 for UWP (WinRT backend) ---------------------------------------------
$SdlDir = Join-Path $Deps "SDL2-$SdlVersion"
if (-not (Test-Path $SdlDir)) {
    Write-Host "== SDL2 $SdlVersion source"
    $zip = Join-Path $Deps "SDL2-$SdlVersion.zip"
    Invoke-WebRequest "https://github.com/libsdl-org/SDL/releases/download/release-$SdlVersion/SDL2-$SdlVersion.zip" -OutFile $zip
    Expand-Archive $zip -DestinationPath $Deps -Force
}
$SdlLib = Get-ChildItem (Join-Path $SdlDir "VisualC-WinRT") -Recurse -Filter SDL2.lib -ErrorAction SilentlyContinue |
          Where-Object { $_.FullName -match 'x64[\\/]Release' } | Select-Object -First 1
if (-not $SdlLib) {
    Write-Host "== building SDL2 for UWP"
    Invoke-Checked msbuild @((Join-Path $SdlDir "VisualC-WinRT/SDL-UWP.vcxproj"), "/m", "/nologo", "/v:minimal",
        "/p:Configuration=Release", "/p:Platform=x64", "/p:PlatformToolset=v143",
        "/p:WindowsTargetPlatformVersion=$Sdk")
    $SdlLib = Get-ChildItem (Join-Path $SdlDir "VisualC-WinRT") -Recurse -Filter SDL2.lib |
              Where-Object { $_.FullName -match 'x64[\\/]Release' } | Select-Object -First 1
}
if (-not $SdlLib) { throw "SDL2.lib for UWP was not produced" }
$SdlDll = Join-Path $SdlLib.DirectoryName "SDL2.dll"

# --- ANGLE for UWP (GLES 3 over Direct3D 11) ----------------------------------
$AngleDir = Join-Path $Deps "angle-$AngleVersion"
if (-not (Test-Path $AngleDir)) {
    Write-Host "== ANGLE.WindowsStore $AngleVersion"
    $zip = Join-Path $Deps "angle-$AngleVersion.zip"
    Invoke-WebRequest "https://www.nuget.org/api/v2/package/ANGLE.WindowsStore/$AngleVersion" -OutFile $zip
    Expand-Archive $zip -DestinationPath $AngleDir -Force
}
$EglLib = Get-ChildItem $AngleDir -Recurse -Filter libEGL.lib | Where-Object { $_.FullName -match 'x64' } | Select-Object -First 1
$GlesLib = Get-ChildItem $AngleDir -Recurse -Filter libGLESv2.lib | Where-Object { $_.FullName -match 'x64' } | Select-Object -First 1
$AngleInclude = (Get-ChildItem $AngleDir -Recurse -Directory -Filter GLES3 | Select-Object -First 1).Parent.FullName
if (-not $EglLib -or -not $GlesLib -or -not $AngleInclude) {
    Get-ChildItem $AngleDir -Recurse | ForEach-Object { Write-Host $_.FullName }
    throw "ANGLE x64 libraries or GLES3 headers not found in the NuGet package"
}
$AngleDlls = Get-ChildItem $AngleDir -Recurse -Filter *.dll | Where-Object { $_.FullName -match 'x64' }

# --- libchdr (CHD images; its miniz also inflates CSO) -------------------------
$LibchdrCommit = "47d794151b2b64f6aba60304e6480f4f5a2bc24b"
$LibchdrDir = Join-Path $Deps "libchdr-$LibchdrCommit"
if (-not (Test-Path (Join-Path $LibchdrDir "CMakeLists.txt"))) {
    Write-Host "== libchdr $LibchdrCommit"
    Invoke-Checked git @("clone", "--quiet", "https://github.com/rtissera/libchdr.git", $LibchdrDir)
    Invoke-Checked git @("-C", $LibchdrDir, "checkout", "--quiet", $LibchdrCommit)
}

# --- Game input and AOT translation -------------------------------------------
# The launcher-only package links the game-free smoke corpus.
$Corpus = if ($Game -eq "ncaa10") { "ncaa10" } else { "smoke" }
$GameRoot = Join-Path $Root "games/$Corpus/root"
$Generated = Join-Path $Root "profile/generated/$Corpus"
New-Item -ItemType Directory -Force -Path $GameRoot | Out-Null
if ($Corpus -eq "smoke") {
    $Guest = "ms0:/PSP/GAME/PSPWEB/EBOOT.PBP"
    New-Item -ItemType Directory -Force -Path (Join-Path $GameRoot "disc") | Out-Null
    Invoke-Checked $Python @("-I", (Join-Path $Root "uwp/make_smoke_elf.py"), (Join-Path $GameRoot "EBOOT.BIN"))
    New-Item -ItemType Directory -Force -Path $Generated | Out-Null
    Invoke-Checked $Recomp @((Join-Path $GameRoot "EBOOT.BIN"), "--auto", $Generated, "0x08804000", "0x4000")
} else {
    $Guest = "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN"
    $Disc = Join-Path $GameRoot "disc"
    if (-not $Iso -or -not (Test-Path $Iso)) { throw "-Iso <your NCAA Football 10 ISO> is required" }
    if (-not (Test-Path (Join-Path $Disc "PSP_GAME"))) {
        Write-Host "== extracting the disc"
        Invoke-Checked $Python @("-I", (Join-Path $Root "scripts/extract_iso.py"), $Iso, $Disc)
    }
    $Target = Join-Path $GameRoot "EBOOT.BIN"
    if ($Eboot) { Copy-Item -Force $Eboot $Target }
    if (-not (Test-Path $Target)) {
        $packaged = Join-Path $Disc "PSP_GAME/SYSDIR/EBOOT.BIN"
        $magic = [System.IO.File]::ReadAllBytes($packaged)[0..3]
        if (($magic -join ",") -ne "127,69,76,70") {
            throw "The disc EBOOT.BIN is encrypted: pass -Eboot with the decrypted executable (PPSSPP developer tools)"
        }
        Copy-Item $packaged $Target
    }
    Invoke-Checked $Python @("-I", (Join-Path $Root "scripts/verify_ncaa10.py"), $GameRoot)
    Write-Host "== MIPS to C++"
    New-Item -ItemType Directory -Force -Path $Generated | Out-Null
    Invoke-Checked $Recomp @($Target, "--auto", $Generated, "0x08804000", "0x4000")
    Invoke-Checked $Python @("-I", (Join-Path $Root "scripts/generate_ncaa_sound.py"), $Disc, $Recomp,
        (Join-Path $Root "profile/generated/ncaa10_sound"))
}

# --- UWP build ---------------------------------------------------------------
Write-Host "== configuring the UWP app"
$Cmake = Join-Path $Out "cmake"
Invoke-Checked cmake @("-S", $Framework, "-B", $Cmake, "-G", "Visual Studio 17 2022", "-A", "x64",
    "-DCMAKE_SYSTEM_NAME=WindowsStore", "-DCMAKE_SYSTEM_VERSION=10.0",
    "-DCMAKE_PROJECT_INCLUDE=$(Join-Path $Root 'uwp/uwp.cmake')",
    "-DPSPRECOMP_PROFILE=web", "-DPSPRECOMP_BUILD_TESTS=OFF", "-DPSPRECOMP_BUILD_PROFILE_TESTS=OFF",
    "-DPSPWEB_GAME=$Corpus", "-DPSPWEB_UWP_LAUNCHER_ONLY=$(if ($Game -eq 'launcher') { 'ON' } else { 'OFF' })", "-DPSPWEB_GUEST_PATH=$Guest", "-DPSPWEB_SDL=ON",
    "-DPSPRECOMP_GENERATED_OPT_LEVEL=$Opt", "-DPSPRECOMP_MSVC_MP_JOBS=$MsvcJobs",
    "-DPSPWEB_UWP_SDL2_INCLUDE=$(Join-Path $SdlDir 'include')", "-DPSPWEB_UWP_SDL2_LIB=$($SdlLib.FullName)",
    "-DPSPWEB_UWP_ANGLE_INCLUDE=$AngleInclude", "-DPSPWEB_UWP_EGL_LIB=$($EglLib.FullName)",
    "-DPSPWEB_UWP_GLES_LIB=$($GlesLib.FullName)",
    "-DPSPWEB_UWP_LIBCHDR_DIR=$LibchdrDir",
    "-DPSPWEB_UWP_CPPWINRT_INCLUDE=${env:ProgramFiles(x86)}\Windows Kits\10\Include\$Sdk\cppwinrt")
Write-Host "== building (generated code can take a long time)"
Invoke-Checked cmake @("--build", $Cmake, "--config", "Release", "--target", "pspweb", "--", "/nologo", "/v:minimal")
$Exe = Get-ChildItem $Cmake -Recurse -Filter "pspweb_$Corpus.exe" | Where-Object { $_.FullName -match 'Release' -and $_.FullName -notmatch 'AppX' } | Select-Object -First 1
if (-not $Exe) { throw "pspweb_$Corpus.exe was not produced" }

# --- Package layout ----------------------------------------------------------
Write-Host "== package layout"
$Layout = Join-Path $Out "layout"
if (Test-Path $Layout) { Remove-Item -Recurse -Force $Layout }
New-Item -ItemType Directory -Force -Path (Join-Path $Layout "game") | Out-Null
Copy-Item $Exe.FullName, $SdlDll $Layout
$AngleDlls | ForEach-Object { Copy-Item $_.FullName $Layout }
Copy-Item -Recurse (Join-Path $Root "uwp/Assets") (Join-Path $Layout "Assets")
Invoke-Checked $Python @("-m", "pip", "install", "--quiet", "pillow")
$LauncherArgs = @((Join-Path $Root "uwp/make_launcher.py"), (Join-Path $Layout "Assets"))
if ($Cover) {
    Write-Host "== tiles from $Cover"
    Invoke-Checked $Python @((Join-Path $Root "uwp/make_cover_assets.py"), $Cover, (Join-Path $Layout "Assets"))
    $LauncherArgs += @("--cover", $Cover)
}
Write-Host "== start screen images"
Invoke-Checked $Python $LauncherArgs
Copy-Item (Join-Path $GameRoot "EBOOT.BIN") (Join-Path $Layout "game/EBOOT.BIN")
if ($PackDisc -and $Iso) {
    Write-Host "== packing the disc image into the package"
    Copy-Item $Iso (Join-Path $Layout "game/$([System.IO.Path]::GetFileName($Iso))")
}

# The UWP C++ runtime framework package (vcruntime140_app etc.).
$VcLibsAppx = Get-ChildItem "${env:ProgramFiles(x86)}\Microsoft SDKs\Windows Kits\10\ExtensionSDKs\Microsoft.VCLibs" -Recurse `
    -Filter "Microsoft.VCLibs.x64.14.00.appx" -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match 'Retail' } | Sort-Object FullName | Select-Object -Last 1
$VcLibsVersion = "14.0.30704.0"
if ($VcLibsAppx) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::OpenRead($VcLibsAppx.FullName)
    try {
        $reader = New-Object System.IO.StreamReader(($archive.Entries | Where-Object FullName -eq "AppxManifest.xml").Open())
        $VcLibsVersion = ([xml]$reader.ReadToEnd()).Package.Identity.Version
        $reader.Close()
    } finally { $archive.Dispose() }
}
Write-Host "== VCLibs framework $VcLibsVersion"

$Publisher = "CN=PSPRecompUWP"
$Display = @{ smoke = "NCAA 10 PSP (test)"; launcher = "NCAA 10 PSP"; ncaa10 = "NCAA 10 PSP Game" }[$Game]
# The launcher starts the game package through this protocol.
$Extensions = if ($Game -eq "ncaa10") {
    '<Extensions><uap:Extension Category="windows.protocol"><uap:Protocol Name="ncaa10psp-game">' +
    '<uap:DisplayName>NCAA 10 PSP Game</uap:DisplayName></uap:Protocol></uap:Extension></Extensions>'
} else { "" }
$Identity = @{ smoke = "PSPRecomp.UWPSmoke"; launcher = "PSPRecomp.NCAA10Launcher"; ncaa10 = "PSPRecomp.NCAAFootball10" }[$Game]
(Get-Content -Raw (Join-Path $Root "uwp/AppxManifest.xml.in")) `
    -replace '@NAME@', $Identity -replace '@VERSION@', $Version -replace '@PUBLISHER@', $Publisher `
    -replace '@DISPLAY@', $Display -replace '@EXECUTABLE@', $Exe.Name -replace '@VCLIBS_VERSION@', $VcLibsVersion `
    -replace '@EXTENSIONS@', $Extensions |
    Set-Content -Encoding UTF8 (Join-Path $Layout "AppxManifest.xml")

# --- Pack and sign -----------------------------------------------------------
$Package = Join-Path $Out "package"
if (Test-Path $Package) { Remove-Item -Recurse -Force $Package }
New-Item -ItemType Directory -Force -Path $Package | Out-Null
$Msix = Join-Path $Package "${Identity}_${Version}_x64.msix"
Write-Host "== makeappx"
Invoke-Checked (Join-Path $SdkBin "makeappx.exe") @("pack", "/o", "/h", "SHA256", "/d", $Layout, "/p", $Msix)

$Pfx = Join-Path $Root "build/uwp-signing.pfx"
$PfxPassword = "pspweb"
if (-not (Test-Path $Pfx)) {
    Write-Host "== creating a self-signed signing certificate ($Publisher)"
    $cert = New-SelfSignedCertificate -Type Custom -Subject $Publisher -KeyUsage DigitalSignature `
        -FriendlyName "PSP Recomp UWP sideload" -CertStoreLocation "Cert:\CurrentUser\My" `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")
    Export-PfxCertificate -Cert $cert -FilePath $Pfx -Password (ConvertTo-SecureString $PfxPassword -AsPlainText -Force) | Out-Null
}
$PfxCert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($Pfx, $PfxPassword)
[System.IO.File]::WriteAllBytes((Join-Path $Package "PSPRecompUWP.cer"), $PfxCert.Export("Cert"))
Write-Host "== signtool"
Invoke-Checked (Join-Path $SdkBin "signtool.exe") @("sign", "/fd", "SHA256", "/a", "/f", $Pfx, "/p", $PfxPassword, $Msix)
if ($VcLibsAppx) { Copy-Item $VcLibsAppx.FullName $Package }
Copy-Item (Join-Path $Root "docs/XBOX.md") (Join-Path $Package "INSTALL.md")
Write-Host "== done: $Package"
Get-ChildItem $Package | ForEach-Object { Write-Host ("   {0}  {1:N1} MB" -f $_.Name, ($_.Length / 1MB)) }
