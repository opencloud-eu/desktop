Param
(
  [Parameter(Mandatory=$true)]
  [string]$ProductName="OpenCloud"
)

# OC-TEST is used by our unit tests
# https://github.com/MicrosoftDocs/winrt-api/issues/1130
$SyncRootManager = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\SyncRootManager"
$GetRootItem = Get-Item $SyncRootManager
Get-ChildItem -Path  $SyncRootManager | ForEach-Object {
    $name = $_.Name.Substring($GetRootItem.Name.Length + 1)
    if ($name.StartsWith("OC-TEST", "CurrentCultureIgnoreCase") -or $name.StartsWith("$ProductName", "CurrentCultureIgnoreCase")) {
        Write-Host $_
        Remove-Item -Recurse $_.PsPath 
    }
}

Get-ChildItem -Path "HKCU:\Software\Classes\CLSID\"  | ForEach-Object {
    $key = (get-itemproperty $_.PsPath)."(default)"
    if ($key) {
        if ($key.StartsWith("OC-TEST", "CurrentCultureIgnoreCase") -or $key.StartsWith("$ProductName", "CurrentCultureIgnoreCase")) {
            Write-Host $key, $_
            Remove-Item -Recurse $_.PsPath
        }
    }
}
Get-Process explorer | Stop-Process
Pause
