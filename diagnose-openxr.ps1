$ErrorActionPreference = "Continue"

$InstallDir = Join-Path $env:LOCALAPPDATA "SmartTouchXR"
$Manifest = Join-Path $InstallDir "XR_APILAYER_JBG_SmartTouchXR.json"
$Dll = Join-Path $InstallDir "SmartTouchXR.dll"
$RegistrySubKey = "SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit"
$RuntimeSubKey = "SOFTWARE\Khronos\OpenXR\1"

function Show-RegistryValues($Hive, $View, $SubKey, $Title) {
    Write-Host "`n===== $Title ====="
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey($Hive, $View)
    $key = $base.OpenSubKey($SubKey, $false)
    try {
        if ($null -eq $key) {
            Write-Host "Key missing"
            return
        }
        foreach ($name in $key.GetValueNames()) {
            Write-Host "$name : $($key.GetValue($name))"
        }
        if ($key.GetValueNames().Count -eq 0) {
            Write-Host "Key exists but has no values."
        }
    }
    finally {
        if ($null -ne $key) { $key.Dispose() }
        $base.Dispose()
    }
}

Write-Host "===== Process ====="
Write-Host "PowerShell bitness: $([IntPtr]::Size * 8)-bit"
Write-Host "OS bitness: $(if ([Environment]::Is64BitOperatingSystem) {'64-bit'} else {'32-bit'})"
Write-Host "User: $env:USERNAME"
Write-Host "LOCALAPPDATA: $env:LOCALAPPDATA"

Write-Host "`n===== Installed files ====="
Get-Item $Manifest, $Dll -ErrorAction SilentlyContinue |
    Select-Object FullName, Length, LastWriteTime

Write-Host "`n===== Manifest JSON ====="
if (Test-Path $Manifest) {
    try {
        Get-Content $Manifest -Raw | ConvertFrom-Json | ConvertTo-Json -Depth 6
        Write-Host "Manifest JSON: VALID"
    } catch {
        Write-Host "Manifest JSON: INVALID"
        Write-Host $_
    }
} else {
    Write-Host "Manifest missing: $Manifest"
}

Write-Host "`n===== OpenXR environment ====="
foreach ($name in @("DISABLE_SMARTTOUCHXR", "XR_ENABLE_API_LAYERS", "XR_API_LAYER_PATH", "XR_RUNTIME_JSON")) {
    Write-Host $name
    Write-Host "  Process: '$([Environment]::GetEnvironmentVariable($name, 'Process'))'"
    Write-Host "  User:    '$([Environment]::GetEnvironmentVariable($name, 'User'))'"
    Write-Host "  Machine: '$([Environment]::GetEnvironmentVariable($name, 'Machine'))'"
}

Show-RegistryValues ([Microsoft.Win32.RegistryHive]::CurrentUser) ([Microsoft.Win32.RegistryView]::Registry64) $RegistrySubKey "64-bit implicit layers (HKCU)"
Show-RegistryValues ([Microsoft.Win32.RegistryHive]::CurrentUser) ([Microsoft.Win32.RegistryView]::Registry32) $RegistrySubKey "32-bit implicit layers (HKCU)"
Show-RegistryValues ([Microsoft.Win32.RegistryHive]::LocalMachine) ([Microsoft.Win32.RegistryView]::Registry64) $RegistrySubKey "64-bit implicit layers (HKLM)"

Write-Host "`n===== Active OpenXR runtime ====="
foreach ($view in @([Microsoft.Win32.RegistryView]::Registry64, [Microsoft.Win32.RegistryView]::Registry32)) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, $view)
    $key = $base.OpenSubKey($RuntimeSubKey, $false)
    try {
        $label = if ($view -eq [Microsoft.Win32.RegistryView]::Registry64) { "64-bit" } else { "32-bit" }
        if ($null -eq $key) {
            Write-Host "${label}: key missing"
        } else {
            Write-Host "${label}: $($key.GetValue('ActiveRuntime', '<missing>'))"
        }
    }
    finally {
        if ($null -ne $key) { $key.Dispose() }
        $base.Dispose()
    }
}

Write-Host "`n===== Expected registration ====="
Write-Host "Manifest: $Manifest"
Write-Host "Expected in HKCU 64-bit view with DWORD value 0"
