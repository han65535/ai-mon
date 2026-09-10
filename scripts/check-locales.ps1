$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$english = Get-Content -LiteralPath (Join-Path $projectRoot 'locales\en.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$korean = Get-Content -LiteralPath (Join-Path $projectRoot 'locales\ko.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$keys = @($english.strings.PSObject.Properties.Name | Sort-Object)
$otherKeys = @($korean.strings.PSObject.Properties.Name | Sort-Object)
if (Compare-Object $keys $otherKeys) { throw 'Embedded translation keys differ' }
foreach ($key in $keys) {
    $original = [string]$english.strings.$key
    $translated = [string]$korean.strings.$key
    if (-not $original -or -not $translated) { throw "Empty translation: $key" }
    $a = @([regex]::Matches($original, '\{[^{}]+\}') | ForEach-Object { $_.Value } | Sort-Object -Unique)
    $b = @([regex]::Matches($translated, '\{[^{}]+\}') | ForEach-Object { $_.Value } | Sort-Object -Unique)
    if (($a -join '|') -cne ($b -join '|')) { throw "Placeholder mismatch: $key" }
}
$installerEnglish = Get-Content -LiteralPath (Join-Path $projectRoot 'installer\ui.en.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$installerKorean = Get-Content -LiteralPath (Join-Path $projectRoot 'installer\ui.ko.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if (Compare-Object @($installerEnglish.PSObject.Properties.Name) @($installerKorean.PSObject.Properties.Name)) { throw 'Installer translation keys differ' }
Write-Output "Translations PASS: $($keys.Count) application keys; English and Korean installer keys match."
