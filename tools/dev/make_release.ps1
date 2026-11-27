# Builds the release zip: release\DualSenseRDR2-v<version>.zip (or -ExperimentalDS4 for DS4 experimental zip)
#   DualSenseRDR2.asi, DualSenseRDR2\ (DualSenseCore.dll, weapons, haptics, sound labels, speech voices),
#   lml\ (the Settings page), README.txt, LICENSE.txt
# Your personal calibration.json is NOT included (players get estimated zones + auto-align).
param(
    [switch]$ExperimentalDS4
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$commonRaw = Get-Content "$root\mod\src\common.h" -Raw
if ($ExperimentalDS4) {
    $m = [regex]::Match($commonRaw, '#ifdef\s+DSR2_DS4_EXPERIMENTAL[\s\S]*?kVersion\s*=\s*"([^"]+)"')
    if (-not $m.Success) { throw "could not parse DS4 version from common.h" }
    $baseVer = ($m.Groups[1].Value -split '-')[0]
    $zipName = "DualSenseRDR2-v$baseVer-ds4-experimental.zip"
    $buildDir = "$root\mod\build-ds4"
    $readmeSrc = "$root\release\README-DS4.txt"
} else {
    $m = [regex]::Matches($commonRaw, 'kVersion\s*=\s*"([^"]+)"')
    if ($m.Count -lt 1) { throw "could not parse version from common.h" }
    $version = $m[$m.Count - 1].Groups[1].Value
    $zipName = "DualSenseRDR2-v$version.zip"
    $buildDir = "$root\mod\build"
    $readmeSrc = "$root\release\README.txt"
}
$asi = "$buildDir\DualSenseRDR2.asi"
$coreDll = "$buildDir\DualSenseCore.dll"
if (-not (Test-Path $asi) -or -not (Test-Path $coreDll)) {
    throw "build the mod first ($asi + $coreDll)"
}
$testExe = "$buildDir\dsr2_test.exe"
if (Test-Path $testExe) {
    & $testExe --selftest
    if ($LASTEXITCODE -ne 0) { throw "dsr2_test --selftest failed (exit $LASTEXITCODE)" }
}
$reloadExe = "$buildDir\test_reload\dsr2_reload_test.exe"
if (Test-Path $reloadExe) {
    & $reloadExe
    if ($LASTEXITCODE -ne 0) { throw "dsr2_reload_test failed (exit $LASTEXITCODE)" }
}

$stage = "$root\release\stage"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
try {
    New-Item -ItemType Directory -Force "$stage\DualSenseRDR2" | Out-Null
    Copy-Item $asi $stage
    Copy-Item $coreDll "$stage\DualSenseRDR2\"
    foreach ($f in "weapons.json", "haptics.json", "sound_labels.json", "speech_voices.txt", "translations.json") { Copy-Item "$root\tools\trigger_lab\data\$f" "$stage\DualSenseRDR2\" }
    # the Settings page for Lenny's Mod Loader, laid out like <game>\lml
    $lml = "$root\mod\lml\DualSenseRDR2"
    $archives = @("replace\update_4\x64\data\ui\startup_data.rpf")
    $loose = "install.xml", "data\ui\settings\controls.ymt", "data\gamepad_icons_pc.ytd", "data\pm_settings_controls.ytd"
    foreach ($f in $loose + $archives) {
        if (-not (Test-Path "$lml\$f")) { throw "LML file missing: $f (run the tools\rpf8 builders)" }
    }
    New-Item -ItemType Directory -Force "$stage\lml\DualSenseRDR2\data\ui\settings" | Out-Null
    foreach ($f in $loose) { Copy-Item "$lml\$f" "$stage\lml\DualSenseRDR2\$f" }
    foreach ($a in $archives) {
        New-Item -ItemType Directory -Force (Split-Path "$stage\lml\$a") | Out-Null
        Copy-Item "$lml\$a" "$stage\lml\$a"
    }
    # (the texts are not a file: the .asi adds them to the game's string table - any game language)
    Copy-Item $readmeSrc "$stage\README.txt"
    Copy-Item "$root\LICENSE" "$stage\LICENSE.txt"

    $zip = "$root\release\$zipName"
    if (Test-Path $zip) { Remove-Item $zip }
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::Open($zip, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        $stageFull = (Resolve-Path $stage).Path
        Get-ChildItem $stageFull -Recurse -File | ForEach-Object {
            $rel = $_.FullName.Substring($stageFull.Length + 1).Replace('\', '/')
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $_.FullName, $rel, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    } finally {
        $archive.Dispose()
    }
    Get-Item $zip | Select-Object Name, Length
} finally {
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
}
