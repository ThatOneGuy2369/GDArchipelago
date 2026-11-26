# Installs the Geometry Dash Archipelago mod (.geode file next to this script) into Geometry Dash.
# Finds GD in any Steam library, checks Geode is installed, and copies the mod into geode\mods.
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

function Find-GD {
    $candidates = @()
    try {
        $steam = (Get-ItemProperty -Path "HKCU:\Software\Valve\Steam" -Name SteamPath -ErrorAction Stop).SteamPath
        if ($steam) {
            $candidates += (Join-Path $steam "steamapps\common\Geometry Dash")
            $vdf = Join-Path $steam "steamapps\libraryfolders.vdf"
            if (Test-Path $vdf) {
                foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
                    $candidates += (Join-Path ($m.Groups[1].Value -replace '\\\\', '\') "steamapps\common\Geometry Dash")
                }
            }
        }
    } catch {}
    $candidates += "C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash"
    foreach ($c in $candidates) {
        if (Test-Path (Join-Path $c "GeometryDash.exe")) { return $c }
    }
    return $null
}

$mod = Get-ChildItem -Path $here -Filter *.geode | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $mod) {
    Write-Host "No .geode file next to this installer." -ForegroundColor Red
    Write-Host "Build the mod first (see BUILD.md), then put the .geode file in this folder and run again."
    exit 1
}

$gd = Find-GD
if (-not $gd) {
    Write-Host "Couldn't find Geometry Dash automatically."
    $gd = Read-Host "Paste your Geometry Dash folder (the one with GeometryDash.exe)"
    $gd = $gd.Trim('"', ' ')
    if (-not (Test-Path (Join-Path $gd "GeometryDash.exe"))) {
        Write-Host "GeometryDash.exe isn't in that folder." -ForegroundColor Red
        exit 1
    }
}
Write-Host "Geometry Dash: $gd"

if (-not ((Test-Path (Join-Path $gd "Geode.dll")) -or (Test-Path (Join-Path $gd "geode")))) {
    Write-Host "Geode isn't installed in this Geometry Dash." -ForegroundColor Red
    Write-Host "Install Geode from https://geode-sdk.org first, start the game once, then run this again."
    exit 1
}

$mods = Join-Path $gd "geode\mods"
New-Item -ItemType Directory -Force -Path $mods | Out-Null
Copy-Item -Force $mod.FullName (Join-Path $mods $mod.Name)
Write-Host "Installed $($mod.Name) into $mods" -ForegroundColor Green
Write-Host "Start Geometry Dash, open Geode > Mods > Geometry Dash Archipelago > settings, set Server and Slot name,"
Write-Host "then press the AP button on the main menu."
