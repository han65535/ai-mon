param([string]$Toolchain = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $Toolchain) { $Toolchain = $env:AIMON_TOOLCHAIN }
if (-not $Toolchain) { $Toolchain = Join-Path $projectRoot '.tools\llvm-mingw-20260908-ucrt-x86_64' }
$Toolchain = [IO.Path]::GetFullPath($Toolchain)
$compiler = Join-Path $Toolchain 'bin\clang++.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw "LLVM-MinGW not found. Set AIMON_TOOLCHAIN or pass -Toolchain. Expected: $compiler" }
return $Toolchain
