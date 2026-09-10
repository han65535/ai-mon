param([string]$Package = '', [ValidateSet('en','ko')][string]$Language = 'en')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'msi-common.ps1')
$projectRoot = Split-Path $PSScriptRoot -Parent
$report = Get-Content -LiteralPath (Join-Path $projectRoot "out\Installer\installer-check.$Language.json") -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $Package) { $Package = Join-Path $projectRoot ('out\Installer\' + $report.package) }
if ((Get-FileHash -LiteralPath $Package -Algorithm SHA256).Hash.ToLowerInvariant() -ne $report.sha256) { throw 'Package hash does not match its report' }
$wi = New-Object -ComObject WindowsInstaller.Installer
$testRoot = Join-Path $projectRoot ('out\installer-test-' + [Guid]::NewGuid().ToString('N'))
$installDir = Join-Path $testRoot 'App'
$menuDir = Join-Path $testRoot 'Menu'
$dataDir = Join-Path $testRoot 'Data'
$checks = 0
$installed = $false
function Assert-Installer([bool]$Condition,[string]$Message) {
    $script:checks++
    if (-not $Condition) { throw $Message }
}
function Invoke-Installer([string]$Arguments,[string]$Step) {
    $process = Start-Process -FilePath "$env:SystemRoot\System32\msiexec.exe" -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(120000)) { throw "$Step timed out; inspect its MSI log before retrying" }
    if ($process.ExitCode -ne 0) { throw "$Step failed with MSI exit code $($process.ExitCode); logs: $testRoot" }
}
try {
    Assert-Installer ((Get-MsiProperty $wi 'ProductState' @($report.productCode)) -eq -1) 'AI Mon is already registered; refusing to change an existing installation'
    Assert-Installer (-not (Test-Path -LiteralPath 'HKCU:\Software\AI Mon\Installer')) 'An existing AI Mon installer registration must not be changed by the test'
    $workspace = [IO.Path]::GetFullPath($projectRoot).TrimEnd('\') + '\'
    foreach ($path in @($testRoot,$installDir,$menuDir,$dataDir)) {
        Assert-Installer ([IO.Path]::GetFullPath($path).StartsWith($workspace,[StringComparison]::OrdinalIgnoreCase)) 'Test path escapes workspace'
    }
    New-Item -ItemType Directory -Force -Path $testRoot,$dataDir | Out-Null
    $installArgs = '/i "' + $Package + '" /qn /norestart INSTALLDIR="' + $installDir + '" AIMONMENUFOLDER="' + $menuDir + '" /L*v "' + (Join-Path $testRoot 'install.log') + '"'
    Invoke-Installer $installArgs 'Install'
    $installed = $true
    $installedExe = Join-Path $installDir 'ai-mon.exe'
    Assert-Installer (Test-Path -LiteralPath $installedExe) 'Executable not installed'
    Assert-Installer ((Get-FileHash -LiteralPath $installedExe -Algorithm SHA256).Hash.ToLowerInvariant() -eq $report.payloadSha256) 'Installed payload differs from Release'
    Assert-Installer ((Get-MsiProperty $wi 'ProductState' @($report.productCode)) -eq 5) 'Product not registered as installed'
    Assert-Installer ((Get-MsiProperty $wi 'ProductInfo' @($report.productCode,'AssignmentType')) -eq '0') 'Installation is not per-user'
    $linkPath = Join-Path $menuDir 'AI Mon.lnk'
    Assert-Installer (Test-Path -LiteralPath $linkPath) 'Start menu shortcut is missing'
    $shell = New-Object -ComObject WScript.Shell
    try { $link = $shell.CreateShortcut($linkPath); Assert-Installer ($link.TargetPath -eq $installedExe) 'Shortcut points to the wrong executable'; Close-MsiObject $link }
    finally { Close-MsiObject $shell }
    $registration = Get-ItemProperty -LiteralPath 'HKCU:\Software\AI Mon\Installer'
    Assert-Installer ($registration.InstallLocation.TrimEnd('\') -eq $installDir) 'Registered install location is wrong'
    $smoke = Start-Process -FilePath $installedExe -ArgumentList @('--smoke-test','--data-dir',('"' + $dataDir + '"')) -WindowStyle Hidden -PassThru
    Assert-Installer ($smoke.WaitForExit(15000)) 'Installed app did not finish smoke test'
    Assert-Installer ($smoke.ExitCode -eq 0) 'Installed app smoke test failed'
    $settingsPath = Join-Path $dataDir 'settings.json'
    $settingsHash = (Get-FileHash -LiteralPath $settingsPath -Algorithm SHA256).Hash
    # Delete only this test installation's executable to exercise MSI repair.
    Assert-Installer ([IO.Path]::GetFullPath($installedExe).StartsWith($installDir+'\',[StringComparison]::OrdinalIgnoreCase)) 'Unexpected executable path'
    Remove-Item -LiteralPath $installedExe
    Invoke-Installer ('/fa ' + $report.productCode + ' /qn /norestart /L*v "' + (Join-Path $testRoot 'repair.log') + '"') 'Repair'
    Assert-Installer (Test-Path -LiteralPath $installedExe) 'Repair failed to recover the executable at its registered location'
    Assert-Installer ((Get-FileHash -LiteralPath $installedExe -Algorithm SHA256).Hash.ToLowerInvariant() -eq $report.payloadSha256) 'Repaired executable differs from Release'
    $sentinel = Join-Path $installDir 'user-file.txt'
    [IO.File]::WriteAllText($sentinel,'Must survive uninstall')
    Invoke-Installer ('/x ' + $report.productCode + ' /qn /norestart /L*v "' + (Join-Path $testRoot 'uninstall.log') + '"') 'Uninstall'
    $installed = $false
    Assert-Installer (-not (Test-Path -LiteralPath $installedExe)) 'Uninstall left the executable behind'
    Assert-Installer (-not (Test-Path -LiteralPath $linkPath)) 'Uninstall left the shortcut behind'
    Assert-Installer (Test-Path -LiteralPath $sentinel) 'Uninstall removed an unrelated file'
    Assert-Installer ((Get-FileHash -LiteralPath $settingsPath -Algorithm SHA256).Hash -eq $settingsHash) 'Uninstall changed application data'
    Assert-Installer ((Get-MsiProperty $wi 'ProductState' @($report.productCode)) -eq -1) 'Product registration was not removed'
    Assert-Installer (-not (Test-Path -LiteralPath 'HKCU:\Software\AI Mon\Installer')) 'Installer registry values remain'
    [ordered]@{checks=$checks; package=$report.package; sha256=$report.sha256; install='passed'; repair='passed'; uninstall='passed'; appSmoke='passed'; userData='preserved'; unrelatedFiles='preserved'; logDirectory=$testRoot} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $projectRoot "out\Installer\installer-test.$Language.json") -Encoding UTF8
    Write-Output "Installer PASS: $checks checks; install, repair, app smoke and uninstall."
} finally {
    # Roll back only the registration made by this run, never a pre-existing app.
    if ($installed) { Invoke-Installer ('/x ' + $report.productCode + ' /qn /norestart /L*v "' + (Join-Path $testRoot 'cleanup.log') + '"') 'Test cleanup' }
    Close-MsiObject $wi
}
