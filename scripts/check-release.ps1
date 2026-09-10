param([string]$Toolchain = '', [string]$Exe = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $projectRoot 'out\Release\ai-mon.exe' }
$toolchainRoot = & (Join-Path $PSScriptRoot 'toolchain.ps1') -Toolchain $Toolchain
$file = Get-Item -LiteralPath $Exe
if ($file.Length -gt 1000000) { throw "Release exceeds 1,000,000 bytes: $($file.Length)" }
$inspection = & (Join-Path $toolchainRoot 'bin\llvm-readobj.exe') '--coff-imports' '--file-headers' $Exe
if ($LASTEXITCODE -ne 0) { throw 'PE inspection failed' }
$dlls = @($inspection | Select-String '^\s*Name: (.+\.dll)$' | ForEach-Object { $_.Matches[0].Groups[1].Value.ToLowerInvariant() } | Sort-Object -Unique)
if (-not $dlls.Count) { throw 'No import descriptors found' }
$systemDlls = @('kernel32.dll','user32.dll','gdi32.dll','shell32.dll','comctl32.dll','advapi32.dll','ole32.dll','oleaut32.dll','ucrtbase.dll','ntdll.dll','shlwapi.dll','bcrypt.dll')
foreach ($dll in $dlls) { if ($dll -notin $systemDlls -and $dll -notmatch '^api-ms-win-crt-[a-z0-9-]+\.dll$') { throw "Non-allowlisted DLL dependency: $dll" } }
if (($inspection -join "`n") -notmatch 'IMAGE_FILE_MACHINE_AMD64') { throw 'Release must target x64' }
foreach ($flag in @('IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE','IMAGE_DLL_CHARACTERISTICS_NX_COMPAT','IMAGE_DLL_CHARACTERISTICS_GUARD_CF')) { if (($inspection -join "`n") -notmatch $flag) { throw "Missing PE protection: $flag" } }
$report = [ordered]@{ bytes=$file.Length; limit=1000000; sha256=(Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash.ToLowerInvariant(); imports=$dlls }
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $file.DirectoryName 'release-check.json') -Encoding UTF8
Write-Output "Release OK: $($file.Length) bytes; Windows system imports only."
