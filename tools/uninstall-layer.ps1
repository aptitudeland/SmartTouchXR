$ErrorActionPreference = "Stop"

$InstallDir = Join-Path $env:LOCALAPPDATA "SmartTouchXR"
$InstallManifest = Join-Path $InstallDir "XR_APILAYER_JBG_SmartTouchXR.json"
$LegacyDir = Join-Path $env:LOCALAPPDATA "DCSHandAssist"
$LegacyManifest = Join-Path $LegacyDir "XR_APILAYER_JBG_DCS_handassist_hello.json"
$RegistrySubKey = "SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit"

foreach ($view in @(
    [Microsoft.Win32.RegistryView]::Registry64,
    [Microsoft.Win32.RegistryView]::Registry32
)) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::CurrentUser,
        $view
    )
    $key = $base.OpenSubKey($RegistrySubKey, $true)
    try {
        if ($null -ne $key) {
            $key.DeleteValue($InstallManifest, $false)
            $key.DeleteValue($LegacyManifest, $false)
        }
    }
    finally {
        if ($null -ne $key) { $key.Dispose() }
        $base.Dispose()
    }
}

Remove-Item $InstallDir -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $LegacyDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "SmartTouchXR and legacy HandAssist registrations removed from both registry views."
