$ErrorActionPreference = "Stop"

$BuildDll = Join-Path $PSScriptRoot "build\Release\SmartTouchXR.dll"
$ManifestSource = Join-Path $PSScriptRoot "layer\XR_APILAYER_JBG_SmartTouchXR.json"
$InstallDir = Join-Path $env:LOCALAPPDATA "SmartTouchXR"
$InstallDll = Join-Path $InstallDir "SmartTouchXR.dll"
$InstallManifest = Join-Path $InstallDir "XR_APILAYER_JBG_SmartTouchXR.json"
$RegistrySubKey = "SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit"
$LegacyManifest = Join-Path $env:LOCALAPPDATA "DCSHandAssist\XR_APILAYER_JBG_DCS_handassist_hello.json"

function Open-Hkcu64([bool]$Writable) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::CurrentUser,
        [Microsoft.Win32.RegistryView]::Registry64
    )
    if ($Writable) {
        return $base.CreateSubKey($RegistrySubKey, $true)
    }
    return $base.OpenSubKey($RegistrySubKey, $false)
}

if (!(Test-Path $BuildDll)) {
    throw "DLL not found. Run .\build.ps1 first."
}
if (!(Test-Path $ManifestSource)) {
    throw "Manifest not found: $ManifestSource"
}

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Copy-Item $BuildDll $InstallDll -Force
Copy-Item $ManifestSource $InstallManifest -Force

$key = Open-Hkcu64 $true
try {
    $key.DeleteValue($InstallManifest, $false)
    $key.DeleteValue($LegacyManifest, $false)
    $key.SetValue(
        $InstallManifest,
        0,
        [Microsoft.Win32.RegistryValueKind]::DWord
    )
}
finally {
    if ($null -ne $key) { $key.Dispose() }
}

$Log = Join-Path $InstallDir "SmartTouchXR-layer.txt"
Remove-Item $Log -Force -ErrorAction SilentlyContinue

Write-Host "SmartTouchXR installed in the 64-bit OpenXR registry view."
Write-Host "DLL:      $InstallDll"
Write-Host "Manifest: $InstallManifest"
Write-Host "Log:      $Log"
