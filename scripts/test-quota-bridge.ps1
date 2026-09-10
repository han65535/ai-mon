param([string]$Exe = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $projectRoot 'out\Release\ai-mon.exe' }
$testRoot = Join-Path $projectRoot ('out\quota-bridge-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)
$checks = 0
function Invoke-Bridge([string]$Payload, [int]$ExpectedExit = 0) {
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $Exe
    $start.Arguments = '--claude-statusline --data-dir "' + $testRoot + '"'
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = $utf8
    $start.StandardErrorEncoding = $utf8
    $process = [Diagnostics.Process]::Start($start)
    $output = $process.StandardOutput.ReadToEndAsync()
    $errorOutput = $process.StandardError.ReadToEndAsync()
    $bytes = $utf8.GetBytes($Payload)
    $process.StandardInput.BaseStream.Write($bytes,0,$bytes.Length)
    $process.StandardInput.Close()
    if (-not $process.WaitForExit(10000)) { $process.Kill(); throw 'Bridge timed out' }
    if ($process.ExitCode -ne $ExpectedExit) { throw "Bridge exit $($process.ExitCode): $($errorOutput.Result)" }
    $script:checks++
    return $output.Result
}
$now = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
$payload = [ordered]@{rate_limits=@{five_hour=@{used_percentage=25.5; resets_at=$now+3600}; seven_day=@{used_percentage=80; resets_at=$now+86400}}; unrelated='synthetic text must not be saved'} | ConvertTo-Json -Depth 6 -Compress
$output = Invoke-Bridge $payload
if ($output -notmatch '74% left' -or $output -notmatch '20% left') { throw 'Status line remaining values incorrect' }
$quotaFile = Join-Path $testRoot 'claude-quota.json'
$savedText = Get-Content -LiteralPath $quotaFile -Raw -Encoding UTF8
$saved = $savedText | ConvertFrom-Json
if ($saved.short.remaining -ne 7450 -or $saved.week.remaining -ne 2000 -or $savedText.Contains('synthetic')) { throw 'Quota data was not correctly sanitized' }
$checks++
Invoke-Bridge '{broken' 2 | Out-Null
if ((Get-Content -LiteralPath $quotaFile -Raw -Encoding UTF8) -ne $savedText) { throw 'Invalid input overwrote the last valid quota' }
$checks++
Invoke-Bridge '{"context_window":{"remaining_percentage":99}}' | Out-Null
$saved = Get-Content -LiteralPath $quotaFile -Raw -Encoding UTF8 | ConvertFrom-Json
if ($null -ne $saved.short -or $null -ne $saved.week) { throw 'Context capacity was confused with subscription allowance' }
$checks++
$gitRoot = Split-Path (Split-Path (Get-Command git).Source -Parent) -Parent
$bash = Join-Path $gitRoot 'bin\bash.exe'
if (-not (Test-Path -LiteralPath $bash)) { throw 'Git Bash required for status-line forwarding test' }
$backup = @{original=@{type='command';command='cat'}; shell=$bash; bash=$true} | ConvertTo-Json -Depth 4
[IO.File]::WriteAllText((Join-Path $testRoot 'claude-statusline-backup.json'),$backup,$utf8)
$output = Invoke-Bridge $payload
if ($output -cne $payload) { throw 'Existing status-line input changed during forwarding' }
$checks++
# More than a pipe buffer proves producer/consumer forwarding cannot deadlock.
$large = @{rate_limits=@{five_hour=@{used_percentage=10;resets_at=$now+3600}};padding=('z' * 200000)} | ConvertTo-Json -Depth 4 -Compress
$output = Invoke-Bridge $large
if ($output -cne $large) { throw 'Large status-line input was truncated' }
$checks++
$backup = @{original=@{type='command';command='sleep 30'}; shell=$bash; bash=$true} | ConvertTo-Json -Depth 4
[IO.File]::WriteAllText((Join-Path $testRoot 'claude-statusline-backup.json'),$backup,$utf8)
Invoke-Bridge $payload 3 | Out-Null
[ordered]@{checks=$checks; normalizedQuota='passed'; inputForwarding='passed'; boundedChildLifetime='passed'; fixtureDirectory=$testRoot} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $projectRoot 'out\Release\quota-bridge-test.json') -Encoding UTF8
Write-Output "Quota bridge PASS: $checks checks; percentages, sanitized storage, forwarding and timeout."
