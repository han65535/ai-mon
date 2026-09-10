# Windows Installer Automation exposes indexed properties through IDispatch.
function Convert-MsiArguments {
    param([object[]]$Values)
    $result = New-Object object[] $Values.Count
    for ($i=0; $i -lt $Values.Count; $i++) {
        if ($Values[$i] -is [string]) { $result[$i] = [string]$Values[$i] }
        elseif ($Values[$i] -is [int] -or $Values[$i] -is [long]) { $result[$i] = [int]$Values[$i] }
        elseif ($null -ne $Values[$i]) { $result[$i] = $Values[$i].PSObject.BaseObject }
    }
    return ,$result
}
function Invoke-MsiMethod {
    param($Object, [string]$Name, [object[]]$Arguments = @())
    $values = Convert-MsiArguments $Arguments
    return $Object.GetType().InvokeMember($Name, [Reflection.BindingFlags]::InvokeMethod, $null, $Object, $values, [Globalization.CultureInfo]::GetCultureInfo('en-US'))
}
function Get-MsiProperty {
    param($Object, [string]$Name, [object[]]$Arguments = @())
    $values = Convert-MsiArguments $Arguments
    return $Object.GetType().InvokeMember($Name, [Reflection.BindingFlags]::GetProperty, $null, $Object, $values, [Globalization.CultureInfo]::GetCultureInfo('en-US'))
}
function Set-MsiProperty {
    param($Object, [string]$Name, [object[]]$Arguments)
    $values = Convert-MsiArguments $Arguments
    $Object.GetType().InvokeMember($Name, [Reflection.BindingFlags]::SetProperty, $null, $Object, $values, [Globalization.CultureInfo]::GetCultureInfo('en-US')) | Out-Null
}
function Close-MsiObject {
    param($Object)
    if ($null -ne $Object -and [Runtime.InteropServices.Marshal]::IsComObject($Object)) {
        [Runtime.InteropServices.Marshal]::FinalReleaseComObject($Object) | Out-Null
    }
}
