$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Build = Join-Path $Root "build"

cmake -S (Join-Path $Root "layer") -B $Build -A x64
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed with exit code $LASTEXITCODE."
}

cmake --build $Build --config Release
if ($LASTEXITCODE -ne 0) {
    throw "Compilation failed with exit code $LASTEXITCODE."
}

$Dll = Join-Path $Build "Release\DCSHandAssistHelloLayer.dll"

if (!(Test-Path $Dll)) {
    throw "The DLL was not created: $Dll"
}

Write-Host ""
Write-Host "Build succeeded."
Write-Host "Output: $Dll"
