$ErrorActionPreference = "Stop"

$InstallDir = Join-Path $env:LOCALAPPDATA "SmartTouchXR"
$Manifest = Join-Path $InstallDir "XR_APILAYER_JBG_SmartTouchXR.json"
$Dll = Join-Path $InstallDir "SmartTouchXR.dll"
$RegistrySubKey = "SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit"

Write-Host "===== Process ====="
Write-Host "PowerShell bitness: $([IntPtr]::Size * 8)-bit"

Write-Host "`n===== Files ====="
Get-Item $Dll, $Manifest | Select-Object FullName, Length, LastWriteTime

Write-Host "`n===== Manifest ====="
Get-Content $Manifest

Write-Host "`n===== 64-bit registry value ====="
$base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
    [Microsoft.Win32.RegistryHive]::CurrentUser,
    [Microsoft.Win32.RegistryView]::Registry64
)
$key = $base.OpenSubKey($RegistrySubKey, $false)
try {
    if ($null -eq $key) {
        throw "The 64-bit OpenXR implicit-layer registry key does not exist."
    }
    $value = $key.GetValue($Manifest, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
    if ($null -eq $value) {
        throw "SmartTouchXR is not registered in the 64-bit OpenXR registry view."
    }
    if ([int]$value -ne 0) {
        throw "SmartTouchXR registry value is $value; expected DWORD 0."
    }
    Write-Host "$Manifest : $value"
}
finally {
    if ($null -ne $key) { $key.Dispose() }
    $base.Dispose()
}

Write-Host "`nInstallation verification succeeded for a 64-bit OpenXR application."
