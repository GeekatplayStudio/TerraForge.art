# TerraForge - fetch third-party dependencies into external/
# Run once after cloning:  powershell -ExecutionPolicy Bypass -File scripts\get_deps.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$ext = Join-Path $root "external"
New-Item -ItemType Directory -Force $ext | Out-Null
Set-Location $ext

function Clone($url, $dir, $branch) {
    if (Test-Path $dir) { Write-Host "$dir already present" -ForegroundColor DarkGray; return }
    Write-Host "cloning $dir..." -ForegroundColor Cyan
    if ($branch) { git clone --depth 1 --branch $branch $url $dir }
    else { git clone --depth 1 $url $dir }
}

Clone "https://github.com/ocornut/imgui.git" "imgui" "docking"
Clone "https://github.com/glfw/glfw.git" "glfw" $null
Clone "https://github.com/thedmd/imgui-node-editor.git" "imgui-node-editor" $null
Clone "https://github.com/g-truc/glm.git" "glm" $null

function Fetch($url, $file) {
    if (Test-Path $file) { Write-Host "$file already present" -ForegroundColor DarkGray; return }
    Write-Host "downloading $file..." -ForegroundColor Cyan
    Invoke-WebRequest -Uri $url -OutFile $file
}

Fetch "https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp" "json.hpp"
Fetch "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h" "stb_image.h"
Fetch "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h" "stb_image_write.h"

# miniz (zip reading for the PBR material downloader)
if (-not (Test-Path "miniz")) {
    Write-Host "downloading miniz..." -ForegroundColor Cyan
    Invoke-WebRequest -Uri "https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip" -OutFile "miniz.zip"
    Expand-Archive "miniz.zip" -DestinationPath "miniz" -Force
    Remove-Item "miniz.zip"
}

# glad OpenGL 4.6 core loader (generated with the glad2 python package)
if (-not (Test-Path "glad")) {
    Write-Host "generating glad loader..." -ForegroundColor Cyan
    python -m pip install --quiet glad2
    python -m glad --api "gl:core=4.6" --out-path glad c
}

# imgui-node-editor ships an operator that newer Dear ImGui already defines
$inl = Join-Path $ext "imgui-node-editor\imgui_extra_math.inl"
if (Test-Path $inl) {
    $text = Get-Content $inl -Raw
    if ($text -notmatch "IMGUI_VERSION_NUM < 19105") {
        $text = $text -replace "(?s)(inline ImVec2 operator\*\(const float lhs, const ImVec2& rhs\)\s*\{[^}]*\})",
                               "# if IMGUI_VERSION_NUM < 19105`n`$1`n# endif"
        Set-Content -Encoding utf8 $inl $text
        Write-Host "patched imgui-node-editor for current Dear ImGui" -ForegroundColor Cyan
    }
}

Write-Host "`nDependencies ready. Now run: .\build.ps1" -ForegroundColor Green
