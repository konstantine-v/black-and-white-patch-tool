# Builds everything, runs the unit tests and verifies every hook against the original executable.
#
#   .\build.ps1                 # Release build + tests + hook verification
#   .\build.ps1 -Config Debug
#   .\build.ps1 -UpdateSymbols  # also mark verified hooks as decompiled in symbols\functions.csv
#
# Hook verification needs orig\runblack.exe (the supported build, never committed) and a Python with capstone and
# pefile; set BW_PYTHON to override the interpreter (default: E:\Tools\re-venv\Scripts\python.exe).
param(
    [ValidateSet("Release", "Debug", "RelWithDebInfo")] [string] $Config = "Release",
    [switch] $UpdateSymbols
)
$ErrorActionPreference = "Stop"

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = "C:\Program Files\CMake\bin\cmake.exe" }
$python = if ($env:BW_PYTHON) { $env:BW_PYTHON } else { "E:\Tools\re-venv\Scripts\python.exe" }
$build = Join-Path $PSScriptRoot "build"

& $cmake -S $PSScriptRoot -B $build -G "Visual Studio 17 2022" -A Win32
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
& $cmake --build $build --config $Config
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

$tests = Join-Path $build "$Config\bw_tests.exe"
& $tests
if ($LASTEXITCODE -ne 0) { throw "Unit tests failed" }

$manifest = Join-Path $build "hooks.csv"
& $tests --write-hook-manifest $manifest
if ($LASTEXITCODE -ne 0) { throw "Writing the hook manifest failed" }

$original = Join-Path $PSScriptRoot "orig\runblack.exe"
if (-not (Test-Path $original)) {
    Write-Warning "orig\runblack.exe missing: hook verification skipped"
    exit 0
}
$checkArgs = @("-I", (Join-Path $PSScriptRoot "tools\check_hooks.py"), $original,
               (Join-Path $PSScriptRoot "symbols\functions.csv"), $manifest)
if ($UpdateSymbols) { $checkArgs += "--update" }
& $python @checkArgs
if ($LASTEXITCODE -ne 0) { throw "Hook verification failed" }
