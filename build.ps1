param(
  [string]$ApiUrl = 'https://euilscrr.vercel.app/api/watcher',
  [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
  throw 'Visual Studio Installer (vswhere.exe) not found. Install Visual Studio Build Tools with C++ Desktop workload.'
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
  throw 'MSVC x64 C++ build tools not found.'
}

$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) {
  throw "vcvars64.bat not found at $vcvars"
}

New-Item -ItemType Directory -Force -Path build, dist | Out-Null

$escapedUrl = $ApiUrl.Replace('\', '\\').Replace('"', '\"')
Set-Content -Path 'build\api_config.hpp' -Encoding UTF8 -Value "#pragma once`n#define EUILSCRR_DEFAULT_API_URL L`"$escapedUrl`""

$testCmd = if (-not $SkipTests) {
  'cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /Fo:build\ test\tracker_test.cpp /Fe:build\tracker_test.exe && build\tracker_test.exe && '
} else {
  ''
}

$buildCmd = "${testCmd}rc /nologo /fo build\resources.res src\resources.rc && cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /Ibuild /DUNICODE /D_UNICODE /Fo:build\ src\main.cpp build\resources.res /Fe:dist\euilscrr-watcher.exe /link /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF user32.lib gdi32.lib shell32.lib advapi32.lib ole32.lib dwmapi.lib comctl32.lib d2d1.lib dwrite.lib winhttp.lib windowsapp.lib"

cmd /c "`"$vcvars`" >nul && $buildCmd"
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

$exe = Get-Item dist\euilscrr-watcher.exe
Write-Host ("Built {0} ({1:N1} KB)" -f $exe.FullName, ($exe.Length / 1KB))
