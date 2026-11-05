Param
(
  [string]$ProductName="OpenCloud"
)
<#
.SYNOPSIS
    Cleans up leftover Windows VFS sync roots and their Explorer namespace entries.
.DESCRIPTION
    Removes registry entries for specified product and test artifacts.
    WARNING: This script will restart Windows Explorer.
.PARAMETER ProductName
    The product name to clean up. Defaults to "OpenCloud".
.NOTES
    Requires Administrator privileges.
#>
#Requires -RunAsAdministrator

# OC-TEST is used by our unit tests
# https://github.com/MicrosoftDocs/winrt-api/issues/1130
$SyncRootManager = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\SyncRootManager"
$Classes = "HKCU:\Software\Classes"
$NameSpace = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Desktop\NameSpace"
$HideDesktopIcons = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\HideDesktopIcons\NewStartPanel"

function Test-OurSyncRootId([string]$id) {
    return $id -and ($id.StartsWith("OC-TEST", [System.StringComparison]::OrdinalIgnoreCase) -or $id.StartsWith($ProductName, [System.StringComparison]::OrdinalIgnoreCase))
}

# The Explorer integration of a sync root, the sync roots of an account share one namespace entry.
# Leftover parts of it make the registration of sync roots fail with 0x80070490: Element not found.
function Remove-Namespace([string]$clsid) {
    foreach ($key in "$Classes\CLSID\$clsid", "$Classes\WOW6432Node\CLSID\$clsid", "$NameSpace\$clsid") {
        if (Test-Path $key) {
            Write-Host "Removing: $key"
            Remove-Item -Recurse $key
        }
    }
    if ((Test-Path $HideDesktopIcons) -and $null -ne (Get-Item $HideDesktopIcons).GetValue($clsid)) {
        Write-Host "Removing: $HideDesktopIcons\$clsid"
        Remove-ItemProperty -Path $HideDesktopIcons -Name $clsid
    }
}

Get-ChildItem -Path $SyncRootManager | ForEach-Object {
    $name = $_.PSChildName
    if (Test-OurSyncRootId $name) {
        Write-Host "Removing sync root: ${name} `"${_}`""
        $clsid = $_.GetValue("NamespaceCLSID")
        if ($clsid) {
            Remove-Namespace $clsid
        }
        Remove-Item -Recurse $_.PsPath
    }
}

# namespace entries left behind by failed registrations or earlier versions of this script,
# their default value is the id of the sync root: provider!sid!account!hash
if (Test-Path $NameSpace) {
    Get-ChildItem -Path $NameSpace | ForEach-Object {
        $id = $_.GetValue("")
        if ($id -match "^[^!]+![^!]+!" -and (Test-OurSyncRootId $id)) {
            Write-Host "Removing namespace of sync root: ${id}"
            Remove-Namespace $_.PSChildName
        }
    }
}

Get-Process explorer | Stop-Process
Pause
