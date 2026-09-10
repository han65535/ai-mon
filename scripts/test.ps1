param([string]$Toolchain = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration Test -Toolchain $Toolchain
$testRoot = Join-Path $projectRoot ('out\test-run-' + [Guid]::NewGuid().ToString('N'))
& (Join-Path $projectRoot 'out\Test\ai-mon-tests.exe') $testRoot
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
