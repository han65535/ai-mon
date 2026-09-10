param([string]$Exe = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $projectRoot 'out\Release\ai-mon.exe' }
$testRoot = Join-Path $projectRoot ('out\language-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$checks = 0
function Invoke-LanguageSmoke([string]$Data, [string]$Expected, [switch]$Override) {
    $arguments = '--smoke-test --data-dir "' + $Data + '"'
    if ($Override) { $arguments += ' --language ' + $Expected }
    $process = Start-Process -FilePath $Exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(15000)) { throw 'Language UI test timed out' }
    if ($process.ExitCode -ne 0) { throw "Language UI test failed: $Expected" }
    $saved = Get-Content -LiteralPath (Join-Path $Data 'settings.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($saved.language -ne $Expected) { throw "Language selection not preserved: $Expected" }
    $script:checks++
}
foreach ($language in @('en','ko','auto')) {
    $data = Join-Path $testRoot $language
    Invoke-LanguageSmoke $data $language -Override
    Invoke-LanguageSmoke $data $language
}
$community = Join-Path $testRoot 'community'
$packs = Join-Path $community 'languages'
New-Item -ItemType Directory -Path $packs -Force | Out-Null
$packFile = Join-Path $packs 'fr.json'
$pack = '{"schema":1,"id":"fr","name":"French test","strings":{"action.open":"Ouvrir"}}'
[IO.File]::WriteAllText($packFile, $pack, (New-Object Text.UTF8Encoding($false)))
Invoke-LanguageSmoke $community 'fr' -Override
# Remove only this test's exact pack file to exercise fallback on restart.
Remove-Item -LiteralPath $packFile
Invoke-LanguageSmoke $community 'fr'
[ordered]@{checks=$checks; builtInSwitching='passed'; persistedSelection='passed'; externalSelection='passed'; missingPackFallback='passed'; fixtureDirectory=$testRoot} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $projectRoot 'out\Release\language-test.json') -Encoding UTF8
Write-Output "Language UI PASS: $checks runs; switching, restart, external pack selection and missing-pack fallback."
