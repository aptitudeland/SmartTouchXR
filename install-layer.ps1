$ErrorActionPreference = "Stop"

$BuildDll = Join-Path $PSScriptRoot "build\Release\SmartTouchXR.dll"
$ManifestSource = Join-Path $PSScriptRoot "layer\XR_APILAYER_JBG_SmartTouchXR.json"
$InstallDir = Join-Path $env:LOCALAPPDATA "SmartTouchXR"
$InstallDll = Join-Path $InstallDir "SmartTouchXR.dll"
$InstallManifest = Join-Path $InstallDir "XR_APILAYER_JBG_SmartTouchXR.json"
$RegistryPath = "HKCU:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit"

if (!(Test-Path $BuildDll)) {
    throw "DLL not found. Run .\build.ps1 first."
}

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Copy-Item $BuildDll $InstallDll -Force
Copy-Item $ManifestSource $InstallManifest -Force

New-Item -Path $RegistryPath -Force | Out-Null
New-ItemProperty `
    -Path $RegistryPath `
    -Name $InstallManifest `
    -PropertyType DWord `
    -Value 0 `
    -Force | Out-Null

$Log = Join-Path $InstallDir "SmartTouchXR-layer.txt"
Remove-Item $Log -Force -ErrorAction SilentlyContinue

Write-Host "SmartTouchXR installed."
Write-Host "Manifest: $InstallManifest"
Write-Host "Log after launching DCS VR: $Log"
