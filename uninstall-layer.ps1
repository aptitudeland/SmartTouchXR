$ErrorActionPreference = "Stop"

$InstallDir = Join-Path $env:LOCALAPPDATA "DCSHandAssist"
$InstallManifest = Join-Path $InstallDir "XR_APILAYER_JBG_DCS_handassist_hello.json"
$RegistryPath = "HKCU:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit"

if (Test-Path $RegistryPath) {
    Remove-ItemProperty `
        -Path $RegistryPath `
        -Name $InstallManifest `
        -ErrorAction SilentlyContinue
}

Remove-Item $InstallDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "Hello Layer uninstalled."
