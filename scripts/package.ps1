param([string]$Toolchain = '', [switch]$Rebuild, [ValidateSet('en','ko')][string]$Language = 'en')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'msi-common.ps1')
$projectRoot = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $projectRoot 'out\Release\ai-mon.exe'
if ($Rebuild -or -not (Test-Path -LiteralPath $exe)) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release -Toolchain $Toolchain
} else {
    & (Join-Path $PSScriptRoot 'check-release.ps1') -Exe $exe -Toolchain $Toolchain
}
$product = Get-Content -LiteralPath (Join-Path $projectRoot 'installer\product.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$ui = Get-Content -LiteralPath (Join-Path $projectRoot "installer\ui.$Language.json") -Raw -Encoding UTF8 | ConvertFrom-Json
$codepage = if ($Language -eq 'ko') { 949 } else { 1252 }
$languageId = if ($Language -eq 'ko') { 1042 } else { 1033 }
$font = if ($Language -eq 'ko') { 'Malgun Gothic' } else { 'Segoe UI' }
$versionInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo($exe)
$version = '{0}.{1}.{2}' -f $versionInfo.ProductMajorPart, $versionInfo.ProductMinorPart, $versionInfo.ProductBuildPart
$fileVersion = '{0}.{1}.{2}.{3}' -f $versionInfo.FileMajorPart, $versionInfo.FileMinorPart, $versionInfo.FileBuildPart, $versionInfo.FilePrivatePart
$output = Join-Path $projectRoot 'out\Installer'
$work = Join-Path $output 'build'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$msiPath = Join-Path $output "AI-Mon-$version-x64-$Language.msi"
$payloadHash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
# Stable product identity for the same version; new versions share UpgradeCode.
$md5 = [Security.Cryptography.MD5]::Create()
try { $identityBytes = $md5.ComputeHash([Text.Encoding]::UTF8.GetBytes($product.upgradeCode + ':' + $version)) }
finally { $md5.Dispose() }
$productCode = (New-Object Guid (,$identityBytes)).ToString('B').ToUpperInvariant()
$packageCode = [Guid]::NewGuid().ToString('B').ToUpperInvariant()

# Build an embedded cabinet using Windows' own tool. Relative DDF names avoid
# encoding trouble when the repository path contains Korean characters.
Copy-Item -LiteralPath $exe -Destination (Join-Path $work 'AIMON_EXE') -Force
$ddf = @'
.OPTION EXPLICIT
.Set Cabinet=on
.Set Compress=on
.Set CompressionType=LZX
.Set CompressionMemory=21
.Set CabinetNameTemplate=payload.cab
.Set DiskDirectoryTemplate=.
.Set MaxDiskSize=0
.Set RptFileName=payload.rpt
.Set InfFileName=payload.inf
"AIMON_EXE" "AIMON_EXE"
'@
[IO.File]::WriteAllText((Join-Path $work 'payload.ddf'), $ddf, [Text.Encoding]::ASCII)
Push-Location $work
try {
    & "$env:SystemRoot\System32\makecab.exe" /F payload.ddf | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Cabinet creation failed' }
} finally { Pop-Location }

$installer = New-Object -ComObject WindowsInstaller.Installer
$database = $null
function Invoke-Sql([string]$Sql) {
    $view = Invoke-MsiMethod $database 'OpenView' @($Sql)
    try { Invoke-MsiMethod $view 'Execute' | Out-Null }
    finally { Invoke-MsiMethod $view 'Close' | Out-Null; Close-MsiObject $view }
}
function Add-MsiRow([string]$Table, [string[]]$Columns, [object[]]$Values) {
    if ($Columns.Count -ne $Values.Count) { throw "Column count mismatch: $Table" }
    $columnSql = ($Columns | ForEach-Object { '`' + $_ + '`' }) -join ','
    $parameters = (@('?') * $Values.Count) -join ','
    $view = Invoke-MsiMethod $database 'OpenView' @('INSERT INTO `' + $Table + '` (' + $columnSql + ') VALUES (' + $parameters + ')')
    $record = Invoke-MsiMethod $installer 'CreateRecord' @($Values.Count)
    try {
        for ($i=0; $i -lt $Values.Count; $i++) {
            if ($null -eq $Values[$i]) { continue }
            $property = if ($Values[$i] -is [int] -or $Values[$i] -is [long]) { 'IntegerData' } else { 'StringData' }
            Set-MsiProperty $record $property @(($i+1), $Values[$i])
        }
        Invoke-MsiMethod $view 'Execute' @($record) | Out-Null
    } finally { Close-MsiObject $record; Invoke-MsiMethod $view 'Close' | Out-Null; Close-MsiObject $view }
}
function Add-Stream([string]$Table, [string]$Name, [string]$Path) {
    $view = Invoke-MsiMethod $database 'OpenView' @('INSERT INTO `' + $Table + '` (`Name`,`Data`) VALUES (?,?)')
    $record = Invoke-MsiMethod $installer 'CreateRecord' @(2)
    try {
        Set-MsiProperty $record 'StringData' @(1,$Name)
        Invoke-MsiMethod $record 'SetStream' @(2,$Path) | Out-Null
        Invoke-MsiMethod $view 'Execute' @($record) | Out-Null
    } finally { Close-MsiObject $record; Invoke-MsiMethod $view 'Close' | Out-Null; Close-MsiObject $view }
}
function Add-Control([string]$Dialog,[string]$Name,[string]$Type,[int]$X,[int]$Y,[int]$W,[int]$H,[string]$Text,[string]$Next='') {
    Add-MsiRow 'Control' @('Dialog_','Control','Type','X','Y','Width','Height','Attributes','Text','Control_Next') @($Dialog,$Name,$Type,$X,$Y,$W,$H,3,$Text,$(if($Next){$Next}else{$null}))
}
function Add-Event([string]$Dialog,[string]$Control,[string]$Event,[string]$Argument,[int]$Order=1) {
    Add-MsiRow 'ControlEvent' @('Dialog_','Control_','Event','Argument','Condition','Ordering') @($Dialog,$Control,$Event,$Argument,'1',$Order)
}
try {
    Write-Output "Creating MSI database: $msiPath"
    $database = Invoke-MsiMethod $installer 'OpenDatabase' @($msiPath,3)
    [IO.File]::WriteAllText((Join-Path $work 'codepage.idt'), "`r`n`r`n$codepage`t_ForceCodepage`r`n", [Text.Encoding]::ASCII)
    Invoke-MsiMethod $database 'Import' @($work,'codepage.idt') | Out-Null
    $schemas = @(
        'CREATE TABLE `Property` (`Property` CHAR(72) NOT NULL, `Value` CHAR(0) NOT NULL PRIMARY KEY `Property`)',
        'CREATE TABLE `Directory` (`Directory` CHAR(72) NOT NULL, `Directory_Parent` CHAR(72), `DefaultDir` CHAR(255) NOT NULL PRIMARY KEY `Directory`)',
        'CREATE TABLE `Component` (`Component` CHAR(72) NOT NULL, `ComponentId` CHAR(38), `Directory_` CHAR(72) NOT NULL, `Attributes` SHORT NOT NULL, `Condition` CHAR(255), `KeyPath` CHAR(72) PRIMARY KEY `Component`)',
        'CREATE TABLE `Feature` (`Feature` CHAR(38) NOT NULL, `Feature_Parent` CHAR(38), `Title` CHAR(64), `Description` CHAR(255), `Display` SHORT, `Level` SHORT NOT NULL, `Directory_` CHAR(72), `Attributes` SHORT NOT NULL PRIMARY KEY `Feature`)',
        'CREATE TABLE `FeatureComponents` (`Feature_` CHAR(38) NOT NULL, `Component_` CHAR(72) NOT NULL PRIMARY KEY `Feature_`,`Component_`)',
        'CREATE TABLE `File` (`File` CHAR(72) NOT NULL, `Component_` CHAR(72) NOT NULL, `FileName` CHAR(255) NOT NULL, `FileSize` LONG NOT NULL, `Version` CHAR(72), `Language` CHAR(20), `Attributes` SHORT, `Sequence` SHORT NOT NULL PRIMARY KEY `File`)',
        'CREATE TABLE `Media` (`DiskId` SHORT NOT NULL, `LastSequence` SHORT NOT NULL, `DiskPrompt` CHAR(64), `Cabinet` CHAR(255), `VolumeLabel` CHAR(32), `Source` CHAR(72) PRIMARY KEY `DiskId`)',
        'CREATE TABLE `Registry` (`Registry` CHAR(72) NOT NULL, `Root` SHORT NOT NULL, `Key` CHAR(255) NOT NULL, `Name` CHAR(255), `Value` CHAR(0), `Component_` CHAR(72) NOT NULL PRIMARY KEY `Registry`)',
        'CREATE TABLE `Shortcut` (`Shortcut` CHAR(72) NOT NULL, `Directory_` CHAR(72) NOT NULL, `Name` CHAR(128) NOT NULL, `Component_` CHAR(72) NOT NULL, `Target` CHAR(72) NOT NULL, `Arguments` CHAR(255), `Description` CHAR(255), `Hotkey` SHORT, `Icon_` CHAR(72), `IconIndex` SHORT, `ShowCmd` SHORT, `WkDir` CHAR(72) PRIMARY KEY `Shortcut`)',
        'CREATE TABLE `RemoveFile` (`FileKey` CHAR(72) NOT NULL, `Component_` CHAR(72) NOT NULL, `FileName` CHAR(255), `DirProperty` CHAR(72) NOT NULL, `InstallMode` SHORT NOT NULL PRIMARY KEY `FileKey`)',
        'CREATE TABLE `Upgrade` (`UpgradeCode` CHAR(38) NOT NULL, `VersionMin` CHAR(20), `VersionMax` CHAR(20), `Language` CHAR(255), `Attributes` LONG NOT NULL, `Remove` CHAR(255), `ActionProperty` CHAR(72) NOT NULL PRIMARY KEY `UpgradeCode`,`VersionMin`,`VersionMax`,`Language`,`Attributes`)',
        'CREATE TABLE `LaunchCondition` (`Condition` CHAR(255) NOT NULL, `Description` CHAR(255) NOT NULL PRIMARY KEY `Condition`)',
        'CREATE TABLE `AppSearch` (`Property` CHAR(72) NOT NULL, `Signature_` CHAR(72) NOT NULL PRIMARY KEY `Property`,`Signature_`)',
        'CREATE TABLE `RegLocator` (`Signature_` CHAR(72) NOT NULL, `Root` SHORT NOT NULL, `Key` CHAR(255) NOT NULL, `Name` CHAR(255), `Type` SHORT PRIMARY KEY `Signature_`)',
        'CREATE TABLE `Signature` (`Signature` CHAR(72) NOT NULL, `FileName` CHAR(255) NOT NULL, `MinVersion` CHAR(20), `MaxVersion` CHAR(20), `MinSize` LONG, `MaxSize` LONG, `MinDate` LONG, `MaxDate` LONG, `Languages` CHAR(255) PRIMARY KEY `Signature`)',
        'CREATE TABLE `CustomAction` (`Action` CHAR(72) NOT NULL, `Type` SHORT NOT NULL, `Source` CHAR(72), `Target` CHAR(255) PRIMARY KEY `Action`)',
        'CREATE TABLE `InstallExecuteSequence` (`Action` CHAR(72) NOT NULL, `Condition` CHAR(255), `Sequence` SHORT PRIMARY KEY `Action`)',
        'CREATE TABLE `InstallUISequence` (`Action` CHAR(72) NOT NULL, `Condition` CHAR(255), `Sequence` SHORT PRIMARY KEY `Action`)',
        'CREATE TABLE `Dialog` (`Dialog` CHAR(72) NOT NULL, `HCentering` SHORT NOT NULL, `VCentering` SHORT NOT NULL, `Width` SHORT NOT NULL, `Height` SHORT NOT NULL, `Attributes` LONG NOT NULL, `Title` CHAR(128), `Control_First` CHAR(50) NOT NULL, `Control_Default` CHAR(50), `Control_Cancel` CHAR(50) PRIMARY KEY `Dialog`)',
        'CREATE TABLE `Control` (`Dialog_` CHAR(72) NOT NULL, `Control` CHAR(50) NOT NULL, `Type` CHAR(20) NOT NULL, `X` SHORT NOT NULL, `Y` SHORT NOT NULL, `Width` SHORT NOT NULL, `Height` SHORT NOT NULL, `Attributes` LONG, `Property` CHAR(72), `Text` CHAR(0), `Control_Next` CHAR(50), `Help` CHAR(50) PRIMARY KEY `Dialog_`,`Control`)',
        'CREATE TABLE `ControlEvent` (`Dialog_` CHAR(72) NOT NULL, `Control_` CHAR(50) NOT NULL, `Event` CHAR(50) NOT NULL, `Argument` CHAR(255) NOT NULL, `Condition` CHAR(255) NOT NULL, `Ordering` SHORT PRIMARY KEY `Dialog_`,`Control_`,`Event`,`Argument`,`Condition`)',
        'CREATE TABLE `TextStyle` (`TextStyle` CHAR(72) NOT NULL, `FaceName` CHAR(32) NOT NULL, `Size` SHORT NOT NULL, `Color` LONG, `StyleBits` SHORT PRIMARY KEY `TextStyle`)',
        'CREATE TABLE `ActionText` (`Action` CHAR(72) NOT NULL, `Description` CHAR(64), `Template` CHAR(128) PRIMARY KEY `Action`)',
        'CREATE TABLE `Icon` (`Name` CHAR(72) NOT NULL, `Data` OBJECT NOT NULL PRIMARY KEY `Name`)'
    )
    foreach ($schema in $schemas) { Invoke-Sql $schema }
    $properties = [ordered]@{
        ProductName=$product.name; Manufacturer=$product.manufacturer; ProductVersion=$version; ProductLanguage=[string]$languageId;
        ProductCode=$productCode; UpgradeCode=$product.upgradeCode; INSTALLLEVEL='1'; REBOOT='ReallySuppress';
        ARPNOMODIFY='1'; ARPPRODUCTICON='AppIcon'; DefaultUIFont='BodyFont';
        MSIRESTARTMANAGERCONTROL='DisableShutdown'; SecureCustomProperties='OLDPRODUCTS;NEWERPRODUCTS;INSTALLDIR;AIMONMENUFOLDER'
    }
    foreach ($entry in $properties.GetEnumerator()) { Add-MsiRow 'Property' @('Property','Value') @($entry.Key,[string]$entry.Value) }
    foreach ($row in @(
        @('TARGETDIR',$null,'SourceDir'), @('LocalAppDataFolder','TARGETDIR','.'),
        @('UserPrograms','LocalAppDataFolder','Programs'), @('INSTALLDIR','UserPrograms','AIMon|AI Mon'),
        @('ProgramMenuFolder','TARGETDIR','.'), @('AIMONMENUFOLDER','ProgramMenuFolder','AIMon|AI Mon')
    )) { Add-MsiRow 'Directory' @('Directory','Directory_Parent','DefaultDir') $row }
    Add-MsiRow 'Component' @('Component','ComponentId','Directory_','Attributes','KeyPath') @('App',$product.appComponentCode,'INSTALLDIR',260,'AppRegistry')
    Add-MsiRow 'Component' @('Component','ComponentId','Directory_','Attributes','KeyPath') @('Menu',$product.menuComponentCode,'AIMONMENUFOLDER',260,'MenuRegistry')
    Add-MsiRow 'Feature' @('Feature','Title','Display','Level','Directory_','Attributes') @('Main','AI Mon',1,1,'INSTALLDIR',0)
    foreach ($component in @('App','Menu')) { Add-MsiRow 'FeatureComponents' @('Feature_','Component_') @('Main',$component) }
    Add-MsiRow 'File' @('File','Component_','FileName','FileSize','Version','Attributes','Sequence') @('AIMON_EXE','App','ai-mon.exe',[int](Get-Item -LiteralPath $exe).Length,$fileVersion,512,1)
    Add-MsiRow 'Media' @('DiskId','LastSequence','Cabinet') @(1,1,'#payload.cab')
    Add-Stream '_Streams' 'payload.cab' (Join-Path $work 'payload.cab')
    Add-Stream 'Icon' 'AppIcon' (Join-Path $projectRoot 'resources\app.ico')
    Add-MsiRow 'Registry' @('Registry','Root','Key','Name','Value','Component_') @('AppRegistry',1,'Software\AI Mon\Installer','InstallLocation','[INSTALLDIR]','App')
    Add-MsiRow 'Registry' @('Registry','Root','Key','Name','Value','Component_') @('MenuRegistry',1,'Software\AI Mon\Installer','StartMenu','[AIMONMENUFOLDER]','Menu')
    Add-MsiRow 'Shortcut' @('Shortcut','Directory_','Name','Component_','Target','Description','Icon_','ShowCmd','WkDir') @('StartMenu','AIMONMENUFOLDER','AIMon|AI Mon','Menu','[#AIMON_EXE]','AI Mon local usage monitor','AppIcon',1,'INSTALLDIR')
    Add-MsiRow 'RemoveFile' @('FileKey','Component_','DirProperty','InstallMode') @('RemoveAppFolder','App','INSTALLDIR',2)
    Add-MsiRow 'RemoveFile' @('FileKey','Component_','DirProperty','InstallMode') @('RemoveMenuFolder','Menu','AIMONMENUFOLDER',2)
    Add-MsiRow 'Upgrade' @('UpgradeCode','VersionMin','VersionMax','Attributes','ActionProperty') @($product.upgradeCode,'0.0.0',$version,257,'OLDPRODUCTS')
    Add-MsiRow 'Upgrade' @('UpgradeCode','VersionMin','Attributes','ActionProperty') @($product.upgradeCode,$version,2,'NEWERPRODUCTS')
    # VersionNT can report 603 on Windows 10; use the actual OS build registry value.
    Add-MsiRow 'AppSearch' @('Property','Signature_') @('WINDOWSBUILDNUMBER','WindowsBuildNumber')
    Add-MsiRow 'RegLocator' @('Signature_','Root','Key','Name','Type') @('WindowsBuildNumber',2,'SOFTWARE\Microsoft\Windows NT\CurrentVersion','CurrentBuildNumber',18)
    Add-MsiRow 'AppSearch' @('Property','Signature_') @('INSTALLDIR','PreviousInstallDirectory')
    Add-MsiRow 'AppSearch' @('Property','Signature_') @('AIMONMENUFOLDER','PreviousMenuDirectory')
    Add-MsiRow 'RegLocator' @('Signature_','Root','Key','Name','Type') @('PreviousInstallDirectory',1,'Software\AI Mon\Installer','InstallLocation',18)
    Add-MsiRow 'RegLocator' @('Signature_','Root','Key','Name','Type') @('PreviousMenuDirectory',1,'Software\AI Mon\Installer','StartMenu',18)
    Add-MsiRow 'LaunchCondition' @('Condition','Description') @('Installed OR (VersionNT64 AND WINDOWSBUILDNUMBER >= 10240)',$ui.osRequired)
    Add-MsiRow 'LaunchCondition' @('Condition','Description') @('NOT ALLUSERS',$ui.userRequired)
    Add-MsiRow 'LaunchCondition' @('Condition','Description') @('Installed OR NOT NEWERPRODUCTS',$ui.newerInstalled)
    Add-MsiRow 'CustomAction' @('Action','Type','Source','Target') @('SetInstallLocation',51,'ARPINSTALLLOCATION','[INSTALLDIR]')
    $execute = @(
        @('FindRelatedProducts',$null,25), @('AppSearch',$null,50), @('LaunchConditions',$null,100), @('ValidateProductID',$null,700),
        @('CostInitialize',$null,800), @('FileCost',$null,900), @('CostFinalize',$null,1000),
        @('SetInstallLocation',$null,1100), @('MigrateFeatureStates',$null,1200),
        @('InstallValidate',$null,1400), @('InstallInitialize',$null,1500), @('RemoveExistingProducts','OLDPRODUCTS',1510),
        @('ProcessComponents',$null,1600), @('UnpublishFeatures',$null,1800), @('RemoveRegistryValues',$null,2600),
        @('RemoveShortcuts',$null,3200), @('RemoveFiles',$null,3500), @('RemoveFolders',$null,3600),
        @('CreateFolders',$null,3700), @('InstallFiles',$null,4000), @('CreateShortcuts',$null,4500),
        @('WriteRegistryValues',$null,5000), @('RegisterUser',$null,6000), @('RegisterProduct',$null,6100),
        @('PublishFeatures',$null,6300), @('PublishProduct',$null,6400), @('InstallFinalize',$null,6600)
    )
    foreach ($row in $execute) { Add-MsiRow 'InstallExecuteSequence' @('Action','Condition','Sequence') $row }
    foreach ($row in @(
        @('FindRelatedProducts',$null,25), @('AppSearch',$null,50), @('LaunchConditions',$null,100), @('CostInitialize',$null,800),
        @('FileCost',$null,900), @('CostFinalize',$null,1000), @('SetInstallLocation',$null,1100),
        @('WelcomeDlg','NOT Installed',1200), @('MaintenanceDlg','Installed AND NOT REMOVE AND NOT REINSTALL',1210),
        @('ProgressDlg',$null,1250), @('ExecuteAction',$null,1300),
        @('ExitDlg',$null,-1), @('CancelledDlg',$null,-2), @('ErrorDlg',$null,-3)
    )) { Add-MsiRow 'InstallUISequence' @('Action','Condition','Sequence') $row }
    Add-MsiRow 'TextStyle' @('TextStyle','FaceName','Size') @('BodyFont',$font,9)
    Add-MsiRow 'TextStyle' @('TextStyle','FaceName','Size','StyleBits') @('TitleFont',$font,14,1)
    foreach ($dialog in @('WelcomeDlg','MaintenanceDlg','ExitDlg','CancelledDlg','ErrorDlg','ProgressDlg')) {
        $first = if ($dialog -eq 'ProgressDlg') { 'Title' } elseif ($dialog -eq 'MaintenanceDlg') { 'Repair' } else { 'Primary' }
        $cancel = if ($dialog -in @('WelcomeDlg','MaintenanceDlg')) { 'Cancel' } elseif ($dialog -eq 'ProgressDlg') { $null } else { 'Primary' }
        $attributes = if ($dialog -eq 'ProgressDlg') { 1 } else { 3 }
        Add-MsiRow 'Dialog' @('Dialog','HCentering','VCentering','Width','Height','Attributes','Title','Control_First','Control_Default','Control_Cancel') @($dialog,50,50,370,240,$attributes,$ui.setupTitle,$first,$first,$cancel)
        $heading = switch ($dialog) { WelcomeDlg{$ui.welcomeTitle} MaintenanceDlg{$ui.maintenanceTitle} ExitDlg{$ui.successTitle} CancelledDlg{$ui.cancelledTitle} ErrorDlg{$ui.failedTitle} ProgressDlg{$ui.progressTitle} }
        $body = switch ($dialog) { WelcomeDlg{$ui.welcomeText} MaintenanceDlg{$ui.maintenanceText} ExitDlg{$ui.successText} CancelledDlg{$ui.cancelledText} ErrorDlg{$ui.failedText} ProgressDlg{$ui.progressText} }
        Add-Control $dialog 'Title' 'Text' 20 18 330 34 ('{\TitleFont}' + $heading)
        Add-Control $dialog 'Body' 'Text' 20 62 330 125 $body
        Add-Control $dialog 'Line' 'Line' 0 197 370 1 ''
        if ($dialog -eq 'ProgressDlg') { continue }
        if ($dialog -eq 'MaintenanceDlg') {
            Add-Control $dialog 'Repair' 'PushButton' 109 208 75 20 $ui.repair 'Remove'
            Add-Control $dialog 'Remove' 'PushButton' 192 208 75 20 $ui.remove 'Cancel'
            Add-Control $dialog 'Cancel' 'PushButton' 275 208 75 20 $ui.cancel 'Repair'
            Add-Event $dialog 'Repair' '[REINSTALL]' 'ALL' 1
            Add-Event $dialog 'Repair' '[REINSTALLMODE]' 'amus' 2
            Add-Event $dialog 'Repair' 'EndDialog' 'Return' 3
            Add-Event $dialog 'Remove' '[REMOVE]' 'ALL' 1
            Add-Event $dialog 'Remove' 'EndDialog' 'Return' 2
            Add-Event $dialog 'Cancel' 'EndDialog' 'Exit'
        } elseif ($dialog -eq 'WelcomeDlg') {
            Add-Control $dialog 'Primary' 'PushButton' 192 208 75 20 $ui.install 'Cancel'
            Add-Control $dialog 'Cancel' 'PushButton' 275 208 75 20 $ui.cancel 'Primary'
            Add-Event $dialog 'Primary' 'EndDialog' 'Return'
            Add-Event $dialog 'Cancel' 'EndDialog' 'Exit'
        } else {
            Add-Control $dialog 'Primary' 'PushButton' 275 208 75 20 $ui.close 'Primary'
            Add-Event $dialog 'Primary' 'EndDialog' 'Return'
        }
    }
    foreach ($row in @(@('InstallFiles',$ui.copyFiles), @('RemoveFiles',$ui.removeFiles), @('CreateShortcuts',$ui.shortcuts))) {
        Add-MsiRow 'ActionText' @('Action','Description') $row
    }
    $summary = Get-MsiProperty $database 'SummaryInformation' @(20)
    try {
        foreach ($entry in @(@(1,$codepage),@(2,'AI Mon Setup'),@(3,'AI Mon user installation'),@(4,$product.manufacturer),@(7,"x64;$languageId"),@(9,$packageCode),@(14,500),@(15,10),@(18,'AI Mon MSI builder'),@(19,2))) {
            Set-MsiProperty $summary 'Property' $entry
        }
        Invoke-MsiMethod $summary 'Persist' | Out-Null
    } finally { Close-MsiObject $summary }
    Invoke-MsiMethod $database 'Commit' | Out-Null
} catch {
    Write-Warning $_.ScriptStackTrace
    throw
} finally { Close-MsiObject $database; Close-MsiObject $installer }
$size = (Get-Item -LiteralPath $msiPath).Length
if ($size -gt 1000000) { throw "Installer exceeds 1,000,000 bytes: $size" }
$report = [ordered]@{
    package=(Split-Path $msiPath -Leaf); bytes=$size; limit=1000000; version=$version;
    productCode=$productCode; packageCode=$packageCode; upgradeCode=$product.upgradeCode;
    sha256=(Get-FileHash -LiteralPath $msiPath -Algorithm SHA256).Hash.ToLowerInvariant();
    payloadSha256=$payloadHash; payloadBytes=(Get-Item -LiteralPath $exe).Length;
    context='per-user'; architecture='x64'; language=$Language
}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output "installer-check.$Language.json") -Encoding UTF8
Write-Output "MSI ready: $msiPath ($size bytes)"
