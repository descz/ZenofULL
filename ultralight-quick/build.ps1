$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$sdk = Join-Path $root 'SDK'
if (-not (Test-Path (Join-Path $sdk 'README.md'))) {
  throw "Ultralight SDK ausente. Baixe em https://ultralig.ht/download e extraia em: $sdk"
}
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path $cmake)) { $cmake = 'cmake' }
Push-Location $root
try {
  & $cmake --preset Release
  & $cmake --build --preset Release
  Write-Host "Build concluido: $root\build\out\ZenoBrowser\ZenoBrowser.exe"
} finally { Pop-Location }
