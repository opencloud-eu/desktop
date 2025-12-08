Param
(
  [string]$ProductName="OpenCloud"
)
<#
.SYNOPSIS
    Cleans up leftover Windows VFS sync roots and CLSID entries.
.DESCRIPTION
    Removes registry entries for specified product and test artifacts.
    WARNING: This script will restart Windows Explorer.
.PARAMETER ProductName
    The product name to clean up. Defaults to "OpenCloud".
.NOTES
    Requires Administrator privileges.
#>

# OC-TEST is used by our unit tests
# https://github.com/MicrosoftDocs/winrt-api/issues/1130
$SyncRootManager = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\SyncRootManager"
$GetRootItem = Get-Item $SyncRootManager
Get-ChildItem -Path  $SyncRootManager | ForEach-Object {
    $name = $_.Name.Substring($GetRootItem.Name.Length + 1)
    if ($name.StartsWith("OC-TEST", [System.StringComparison]::CurrentCultureIgnoreCase) -or $name.StartsWith("$ProductName", [System.StringComparison]::CurrentCultureIgnoreCase)) {
        Write-Host "Removing sync root: ${name} `"${_}`""
        Remove-Item -Recurse $_.PsPath 
    }
}

Get-ChildItem -Path "HKCU:\Software\Classes\CLSID\"  | ForEach-Object {
    $key = (get-itemproperty $_.PsPath)."(default)"
    if ($key) {
        if ($key.StartsWith("OC-TEST", [System.StringComparison]::CurrentCultureIgnoreCase) -or $key.StartsWith("$ProductName", [System.StringComparison]::CurrentCultureIgnoreCase)) {
            Write-Host "Removing: ${key} `"${_}`""
            Remove-Item -Recurse $_.PsPath
        }
    }
}
Get-Process explorer | Stop-Process
Pause
