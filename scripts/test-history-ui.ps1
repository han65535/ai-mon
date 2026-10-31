param([string]$Exe = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $projectRoot 'out\Test\ai-mon-tests.exe' }
$testRoot = Join-Path $projectRoot ('out\history-ui-test-' + [Guid]::NewGuid().ToString('N'))
& $Exe '--history-ui-smoke' $testRoot
if ($LASTEXITCODE -ne 0) { throw 'History UI test failed' }
Write-Output 'History UI PASS: weekly/monthly display and language switch.'
