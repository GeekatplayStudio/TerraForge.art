# Geekatplay TerraForge - assemble the runtime tree Windows ships.
#
# One definition of "what an installed TerraForge consists of", used by both
# the one-click installer (scripts/install.ps1) and the setup builder
# (packaging/windows/make_installer.ps1). Having two lists is how a package
# ends up missing the Python layer on a Friday.
#
#   -Destination  folder to fill (created, and cleaned of an older install)
#   -Root         the repository root (defaults to three levels up)
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Destination,
    [string]$Root = (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent)
)

$ErrorActionPreference = "Stop"

$exe = Join-Path $Root "build\geekatplay_studio.exe"
if (-not (Test-Path $exe)) { Write-Error "not built: $exe is missing"; exit 1 }

New-Item -ItemType Directory -Force $Destination | Out-Null

# A previous install's binaries go; the user's own files (logs, preferences,
# projects they saved in there) stay. Deleting a project someone put in the
# install folder would be unforgivable for a version upgrade.
foreach ($stale in @("geekatplay_studio.exe", "nodeterrain_cli.exe", "resources",
                     "orchestrator", "mcp_server", "scripts", "examples", "docs")) {
    $p = Join-Path $Destination $stale
    if (Test-Path $p) { Remove-Item $p -Recurse -Force }
}

Copy-Item $exe $Destination -Force
$cli = Join-Path $Root "build\nodeterrain_cli.exe"
if (Test-Path $cli) { Copy-Item $cli $Destination -Force }

# Ship without debug info. The build carries -g on purpose - a crash report's
# module+RVA resolves to file:line with addr2line - and that is 148 of the
# executable's 159 MB. Stripping costs the user nothing, because the addresses
# in a crash report are offsets into the module and resolve against the
# unstripped build the developer still has. So the symbols are kept beside the
# package rather than thrown away.
if (Get-Command strip -ErrorAction SilentlyContinue) {
    $symbols = Join-Path $Root "dist\symbols"
    New-Item -ItemType Directory -Force $symbols | Out-Null
    foreach ($name in @("geekatplay_studio.exe", "nodeterrain_cli.exe")) {
        $staged = Join-Path $Destination $name
        if (-not (Test-Path $staged)) { continue }
        Copy-Item $staged (Join-Path $symbols $name) -Force
        $before = (Get-Item $staged).Length
        & strip --strip-debug $staged
        if ($LASTEXITCODE -eq 0) {
            $after = (Get-Item $staged).Length
            Write-Host ("  {0}: {1:N1} MB -> {2:N1} MB stripped (symbols in dist\symbols)" -f `
                        $name, ($before / 1MB), ($after / 1MB)) -ForegroundColor DarkGray
        }
    }
}

# The MinGW runtime, but only if this executable actually needs it. The build
# links libstdc++, libgcc and winpthread statically, so normally nothing is
# copied - a package that carries DLLs its binary never loads is just three
# files for a reader to wonder about. A toolchain configured differently still
# produces real dependencies, and shipping without them is the classic way to
# build something that runs only on the machine that built it, so the answer
# is read out of the binary rather than assumed either way.
$needed = @()
if (Get-Command objdump -ErrorAction SilentlyContinue) {
    $needed = (& objdump -p (Join-Path $Destination "geekatplay_studio.exe") 2>$null |
               Select-String "DLL Name:" |
               ForEach-Object { ($_ -split "DLL Name:\s*")[1].Trim() }) |
              Where-Object { $_ -match '^(libstdc\+\+|libgcc|libwinpthread)' }
}
if ($needed) {
    $gxx = Get-Command g++ -ErrorAction SilentlyContinue
    if ($gxx) {
        $bin = Split-Path $gxx.Source -Parent
        foreach ($dll in $needed) {
            $src = Join-Path $bin $dll
            if (Test-Path $src) {
                Copy-Item $src $Destination -Force
                Write-Host "  + $dll (runtime dependency)" -ForegroundColor DarkGray
            } else {
                Write-Warning "  $dll is needed but was not found beside g++"
            }
        }
    } else {
        Write-Warning "  this build needs $($needed -join ', ') and g++ is not on PATH to copy them"
    }
}

function CopyTree ($relative, [switch]$Optional) {
    $src = Join-Path $Root $relative
    if (-not (Test-Path $src)) {
        if (-not $Optional) { Write-Error "missing: $relative" }
        return
    }
    Copy-Item $src (Join-Path $Destination $relative) -Recurse -Force
    Write-Host "  + $relative" -ForegroundColor DarkGray
}

# The icons, from their source rather than from build\resources: the build's
# post-build copy is a convenience for running out of build\, and staging from
# the source means a package never depends on that copy having happened.
Copy-Item (Join-Path $Root "studio\resources") (Join-Path $Destination "resources") -Recurse -Force
Write-Host "  + resources" -ForegroundColor DarkGray

CopyTree "orchestrator"                 # offline renderers (Mitsuba, Cycles, LuxCore)
CopyTree "mcp_server"                   # scripting + MCP tools
CopyTree "examples"                     # sample projects and macros
CopyTree "scripts" -Optional            # crash resolvers, fuzzer

# Flattened into the install folder rather than a docs/ subfolder: there are
# five of them, and someone who opens the folder looking for the node reference
# should see it, not a directory to go into.
foreach ($doc in @("docs\NODES.md", "docs\DEVELOPER_GUIDE.md", "docs\INSTALL.md",
                   "README.md", "LICENSE", "LICENSE.txt", "requirements.txt")) {
    $src = Join-Path $Root $doc
    if (Test-Path $src) { Copy-Item $src (Join-Path $Destination (Split-Path $doc -Leaf)) -Force }
}

# Remove build litter that would otherwise ride along.
Get-ChildItem $Destination -Recurse -Directory -Filter "__pycache__" -ErrorAction SilentlyContinue |
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue

# A self-contained uninstaller: this install is a folder and two shortcuts, so
# removing it is a folder delete and two file deletes. Written out here rather
# than shipped as a file so it always names the folder it actually landed in.
@"
# Removes this TerraForge installation and its shortcuts.
`$here = Split-Path `$MyInvocation.MyCommand.Path -Parent
foreach (`$d in @([Environment]::GetFolderPath("Programs"), [Environment]::GetFolderPath("Desktop"))) {
    `$lnk = Join-Path `$d "TerraForge.lnk"
    if (Test-Path `$lnk) { Remove-Item `$lnk -Force }
}
Write-Host "Removing `$here"
Set-Location `$env:TEMP
Remove-Item `$here -Recurse -Force
Write-Host "TerraForge removed. Your projects and the per-user settings in"
Write-Host "`$env:LOCALAPPDATA\GeekatplayTerraForge were left alone."
"@ | Set-Content -Encoding UTF8 (Join-Path $Destination "uninstall.ps1")

Write-Host "  staged to $Destination" -ForegroundColor DarkGray
