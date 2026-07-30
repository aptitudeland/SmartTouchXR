$ErrorActionPreference = "Stop"

$InstallDir = Join-Path $env:LOCALAPPDATA "SmartTouchXR"
$InstallManifest = Join-Path $InstallDir "XR_APILAYER_JBG_SmartTouchXR.json"
$RegistryPath = "HKCU:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit"

if (Test-Path $RegistryPath) {
    Remove-ItemProperty `
        -Path $RegistryPath `
        -Name $InstallManifest `
        -ErrorAction SilentlyContinue
}

Remove-Item $InstallDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "SmartTouchXR uninstalled."
