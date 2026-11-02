param(
    [Parameter(Mandatory)][string]$Manifest,
    [Parameter(Mandatory)][string]$PackageName,
    [string]$LoaderLinks
)
$ErrorActionPreference = "Stop"

$devMode = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock" -Name AllowDevelopmentWithoutDevLicense -ErrorAction SilentlyContinue
if ($devMode.AllowDevelopmentWithoutDevLicense -ne 1) {
    throw "Registering an unpacked package requires Developer Mode: Settings > System > For developers"
}

$packageRoot = Split-Path $Manifest
foreach ($link in ($LoaderLinks -split ";" | Where-Object { $_ })) {
    $name, $target = $link -split "=", 2
    $path = Join-Path $packageRoot $name
    $existing = Get-Item $path -Force -ErrorAction SilentlyContinue
    if ($existing -and $existing.LinkType -eq "Junction" -and $existing.Target -eq $target) {
        continue
    }
    if ($existing) {
        if (-not $existing.LinkType) {
            throw "$path exists and is not a link"
        }
        [IO.Directory]::Delete($path)
    }
    New-Item -ItemType Junction -Path $path -Target $target | Out-Null
}

Get-AppxPackage -Name $PackageName | Remove-AppxPackage
Add-AppxPackage -Register $Manifest -ForceApplicationShutdown
Get-AppxPackage -Name $PackageName | Format-List Name, PackageFullName, PackageFamilyName, InstallLocation
