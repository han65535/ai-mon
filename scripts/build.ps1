param([ValidateSet('Debug','Release','Test')][string]$Configuration = 'Release', [string]$Toolchain = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'check-locales.ps1')
$toolchainRoot = & (Join-Path $PSScriptRoot 'toolchain.ps1') -Toolchain $Toolchain
$bin = Join-Path $toolchainRoot 'bin'
$out = Join-Path $projectRoot "out\$Configuration"
New-Item -ItemType Directory -Force -Path $out | Out-Null
$common = @('--target=x86_64-w64-windows-gnu','-DUNICODE','-D_UNICODE','-DWINVER=0x0A00','-D_WIN32_WINNT=0x0A00','-DNOMINMAX','-DCJSON_NESTING_LIMIT=64','-Wall','-Wextra','-Werror','-mguard=cf','-I', (Join-Path $projectRoot 'src'), '-I', (Join-Path $projectRoot 'third_party\cjson'))
$opt = if ($Configuration -eq 'Debug') { @('-O0','-g') } else { @('-Oz','-flto','-ffunction-sections','-fdata-sections') }
& (Join-Path $bin 'clang.exe') @common @opt '-std=c99' '-c' (Join-Path $projectRoot 'third_party\cjson\cJSON.c') '-o' (Join-Path $out 'cjson.o')
if ($LASTEXITCODE -ne 0) { throw 'cJSON compilation failed' }
$sources = @('platform.cpp','usage.cpp','collector.cpp','localization.cpp','quota.cpp','claude_bridge.cpp','mini_window.cpp','startup.cpp') | ForEach-Object { Join-Path $projectRoot "src\$_" }
$objects = @((Join-Path $out 'cjson.o'))
Push-Location (Join-Path $projectRoot 'resources')
try {
    & (Join-Path $bin 'x86_64-w64-mingw32-windres.exe') '-i' 'app.rc' '-o' (Join-Path $out 'resources.o')
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed' }
} finally { Pop-Location }
$objects += Join-Path $out 'resources.o'
if ($Configuration -eq 'Test') {
    $sources += Join-Path $projectRoot 'tests\tests.cpp'
    $name = 'ai-mon-tests.exe'
    $subsystem = @('-municode')
} else {
    $sources += Join-Path $projectRoot 'src\main.cpp'
    $name = 'ai-mon.exe'
    $subsystem = @('-mwindows','-municode')
}
$link = @('-static','-Wl,--gc-sections','-Wl,--dynamicbase','-Wl,--nxcompat','-Wl,--high-entropy-va','-Wl,--no-insert-timestamp')
if ($Configuration -ne 'Debug') { $link += '-s' }
& (Join-Path $bin 'clang++.exe') @common @opt '-std=c++17' @sources @objects @subsystem @link '-lshell32' '-luser32' '-lgdi32' '-lcomctl32' '-ladvapi32' '-o' (Join-Path $out $name)
if ($LASTEXITCODE -ne 0) { throw 'C++ build failed' }
Get-Item -LiteralPath (Join-Path $out $name) | Select-Object FullName,Length
if ($Configuration -eq 'Release') { & (Join-Path $PSScriptRoot 'check-release.ps1') -Toolchain $toolchainRoot }
