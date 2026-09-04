# Geekatplay TerraForge - one-click install for Windows.
#
# Run it by double-clicking install.bat in the repository root, or directly:
#   powershell -ExecutionPolicy Bypass -File scripts\install.ps1
#
# What it does, in order:
#   1. finds or installs the build tools (git, CMake, Ninja, a C++20 compiler,
#      and optionally Python)
#   2. fetches the third-party sources into external/
#   3. builds the studio
#   4. copies the result to %LOCALAPPDATA%\Programs\TerraForge and makes the
#      Start Menu and Desktop shortcuts
#
# It installs per user, never machine-wide, so it needs no administrator
# rights - and, just as importantly, the installed folder stays writable, which
# is where preferences, logs and crash reports go.
#
# Flags:
#   -NoBuildTools   fail instead of installing anything with winget
#   -Dev            stop after building; do not copy anywhere or make shortcuts
#   -Prefix <path>  install somewhere other than the default
[CmdletBinding()]
param(
    [switch]$NoBuildTools,
    [switch]$Dev,
    [string]$Prefix = "$env:LOCALAPPDATA\Programs\TerraForge"
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

function Say  ($m) { Write-Host $m -ForegroundColor Cyan }
function Ok   ($m) { Write-Host $m -ForegroundColor Green }
function Warn ($m) { Write-Host $m -ForegroundColor Yellow }
function Die  ($m) { Write-Host "`n$m" -ForegroundColor Red; exit 1 }

function Have ($exe) { return [bool](Get-Command $exe -ErrorAction SilentlyContinue) }

Write-Host ""
Write-Host "======================================================================" -ForegroundColor DarkCyan
Write-Host "  Geekatplay TerraForge - installer" -ForegroundColor White
Write-Host "======================================================================" -ForegroundColor DarkCyan
Write-Host ""

# ---------------------------------------------------------------- 1. tooling
# winget ships with Windows 10 1809+ and Windows 11. Where it is missing we say
# what to install rather than trying to bootstrap a package manager.
$winget = Have winget

function Ensure ($exe, $wingetId, $label, [switch]$Optional) {
    if (Have $exe) { Write-Host ("  {0,-22} found" -f $label) -ForegroundColor DarkGray; return $true }
    if ($NoBuildTools -or -not $winget) {
        if ($Optional) { Warn ("  {0,-22} missing (optional)" -f $label); return $false }
        Die "$label is required and not installed. Install it and run this again.`n  winget install --id $wingetId"
    }
    Say ("  {0,-22} installing via winget..." -f $label)
    winget install --id $wingetId --source winget --accept-package-agreements --accept-source-agreements --silent | Out-Null
    # winget adds to PATH for *new* processes; make it visible to this one too
    $env:PATH = [Environment]::GetEnvironmentVariable("PATH", "Machine") + ";" +
                [Environment]::GetEnvironmentVariable("PATH", "User")
    if (-not (Have $exe)) {
        if ($Optional) { Warn ("  {0,-22} still missing (optional)" -f $label); return $false }
        Die "$label was installed but is not on PATH yet. Close this window, open a new one, and run install.bat again."
    }
    return $true
}

Say "Checking build tools"
Ensure git    "Git.Git"              "git"           | Out-Null
Ensure cmake  "Kitware.CMake"        "CMake"         | Out-Null
Ensure ninja  "Ninja-build.Ninja"    "Ninja"         | Out-Null
$havePython = Ensure python "Python.Python.3.12" "Python 3" -Optional

# A C++20 compiler. The committed golden hashes were recorded with MinGW-w64
# GCC, so that is what we look for first; MSVC builds fine but re-records them.
$compiler = $null
foreach ($c in @("g++", "clang++", "cl")) { if (Have $c) { $compiler = $c; break } }
if (-not $compiler) {
    $mingw = "C:\msys64\ucrt64\bin"
    if (Test-Path "$mingw\g++.exe") {
        $env:PATH = "$mingw;$env:PATH"
        $compiler = "g++"
    } elseif (-not $NoBuildTools -and $winget) {
        Say "  C++ compiler          installing MSYS2 + GCC (this takes a few minutes)..."
        winget install --id MSYS2.MSYS2 --source winget --accept-package-agreements --accept-source-agreements --silent | Out-Null
        $bash = "C:\msys64\usr\bin\bash.exe"
        if (Test-Path $bash) {
            & $bash -lc "pacman -Sy --noconfirm --needed mingw-w64-ucrt-x86_64-gcc" | Out-Null
        }
        if (Test-Path "$mingw\g++.exe") { $env:PATH = "$mingw;$env:PATH"; $compiler = "g++" }
    }
}
if (-not $compiler) {
    Die @"
No C++20 compiler found. Install one of these and run install.bat again:

  MinGW-w64 (recommended - what the reference builds use)
      winget install --id MSYS2.MSYS2
      C:\msys64\usr\bin\bash.exe -lc "pacman -S mingw-w64-ucrt-x86_64-gcc"
      then add C:\msys64\ucrt64\bin to your PATH

  or Visual Studio Build Tools with the "Desktop development with C++" workload
      https://visualstudio.microsoft.com/downloads/
"@
}
Write-Host ("  {0,-22} {1}" -f "C++ compiler", $compiler) -ForegroundColor DarkGray

# ------------------------------------------------------------ 2. dependencies
Write-Host ""
Say "Fetching third-party sources into external/"
& "$root\scripts\get_deps.ps1"
if ($LASTEXITCODE -ne 0) { Die "Dependency download failed (see above)." }

# ------------------------------------------------------------------ 3. build
Write-Host ""
Say "Building (a few minutes the first time)"
& "$root\build.ps1"
if ($LASTEXITCODE -ne 0) { Die "Build failed (see the errors above)." }

$exe = Join-Path $root "build\geekatplay_studio.exe"
if (-not (Test-Path $exe)) { Die "The build reported success but $exe is missing." }

if ($havePython) {
    Write-Host ""
    Say "Installing the optional Python layer (offline renderers, AI assistant)"
    python -m pip install --quiet --disable-pip-version-check -r "$root\requirements.txt" 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { Warn "  pip failed; TerraForge still runs, the offline renderers will not." }
    else { Write-Host "  requirements installed" -ForegroundColor DarkGray }
}

if ($Dev) {
    Write-Host ""
    Ok "Built. Run it with .\start.ps1"
    exit 0
}

# ---------------------------------------------------------------- 4. install
Write-Host ""
Say "Installing to $Prefix"
& "$root\packaging\windows\stage.ps1" -Destination $Prefix -Root $root
if ($LASTEXITCODE -ne 0) { Die "Copying the files failed." }

# Shortcuts. WorkingDirectory is the install folder on purpose: that is where
# the application looks for the Python layer, and where its logs go.
$target = Join-Path $Prefix "geekatplay_studio.exe"
$icon   = Join-Path $Prefix "resources\terraforge.ico"
$shell  = New-Object -ComObject WScript.Shell
foreach ($dir in @(
    [Environment]::GetFolderPath("Programs"),
    [Environment]::GetFolderPath("Desktop"))) {
    if (-not $dir) { continue }
    $lnk = $shell.CreateShortcut((Join-Path $dir "TerraForge.lnk"))
    $lnk.TargetPath       = $target
    $lnk.WorkingDirectory = $Prefix
    $lnk.IconLocation     = if (Test-Path $icon) { $icon } else { $target }
    $lnk.Description      = "Geekatplay TerraForge - node-based 3D terrain studio"
    $lnk.Save()
    Write-Host "  shortcut: $dir\TerraForge.lnk" -ForegroundColor DarkGray
}

Write-Host ""
Ok "TerraForge is installed."
Write-Host "  Start Menu:  TerraForge"
Write-Host "  Folder:      $Prefix"
Write-Host "  Uninstall:   run $Prefix\uninstall.ps1, or just delete that folder"
Write-Host ""
$answer = Read-Host "Start TerraForge now? [Y/n]"
if ($answer -eq "" -or $answer -match '^[Yy]') {
    Start-Process -FilePath $target -WorkingDirectory $Prefix
}
