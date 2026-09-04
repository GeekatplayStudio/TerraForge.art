# Geekatplay TerraForge - build the Windows setup program.
#
#   powershell -ExecutionPolicy Bypass -File packaging\windows\make_installer.ps1
#
# Produces, in dist/:
#   TerraForge-<version>-Setup.exe   an Inno Setup installer (if ISCC is found)
#   TerraForge-<version>-win64.zip   the same tree as a portable archive
#
# The ZIP is not a consolation prize: it is the same staged folder the setup
# program installs, so anyone who cannot or will not run an installer gets an
# identical, working copy. It is always produced; the .exe only when Inno
# Setup is present (winget install --id JRSoftware.InnoSetup).
[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [string]$Version
)

$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Set-Location $root

function Say ($m) { Write-Host $m -ForegroundColor Cyan }
function Ok  ($m) { Write-Host $m -ForegroundColor Green }

if (-not $Version) {
    $cml = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
    $Version = if ($cml -match 'project\([^)]*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') { $Matches[1] } else { "0.0.0" }
}
Say "TerraForge $Version - building the Windows package"

if (-not $SkipBuild) {
    & "$root\build.ps1"
    if ($LASTEXITCODE -ne 0) { Write-Error "build failed"; exit 1 }
}

$stage = Join-Path $root "build\package\TerraForge"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
Say "Staging"
& "$root\packaging\windows\stage.ps1" -Destination $stage -Root $root
if ($LASTEXITCODE -ne 0) { Write-Error "staging failed"; exit 1 }
# The setup program writes its own uninstaller; the folder one would only
# confuse someone who installed with the .exe.
Remove-Item (Join-Path $stage "uninstall.ps1") -Force -ErrorAction SilentlyContinue

$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Force $dist | Out-Null

# ------------------------------------------------------------------ portable
$zip = Join-Path $dist "TerraForge-$Version-win64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Say "Writing $([IO.Path]::GetFileName($zip))"
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal
Ok "  $zip"

# --------------------------------------------------------------------- setup
# Resolved to a plain path string on purpose: Get-Command yields a CommandInfo
# with .Source and Get-Item a FileInfo with .FullName, and calling `& $x.Source`
# on the second silently produces $null, which fails as "the expression after
# '&' produced an object that was not valid" - a message that says nothing
# about Inno Setup.
$iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    foreach ($p in @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
                     "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
                     "${env:ProgramFiles(x86)}\Inno Setup 5\ISCC.exe")) {
        if (Test-Path $p) { $iscc = $p; break }
    }
}
if (-not $iscc) {
    Write-Host ""
    Write-Host "Inno Setup not found, so only the portable ZIP was built." -ForegroundColor Yellow
    Write-Host "  winget install --id JRSoftware.InnoSetup" -ForegroundColor DarkGray
    Write-Host "  then run this script again for TerraForge-$Version-Setup.exe" -ForegroundColor DarkGray
    exit 0
}

Say "Compiling the setup program"
& $iscc "/DAppVersion=$Version" "/DStageDir=$stage" "/DOutDir=$dist" `
    (Join-Path $PSScriptRoot "terraforge.iss")
if ($LASTEXITCODE -ne 0) { Write-Error "Inno Setup failed"; exit 1 }
Ok "  $dist\TerraForge-$Version-Setup.exe"
Write-Host ""
Ok "Done."
