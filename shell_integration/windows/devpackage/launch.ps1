param(
    [Parameter(Mandatory)][string]$PackageName,
    [Parameter(Mandatory)][string]$ApplicationId
)
$ErrorActionPreference = "Stop"

Add-Type -TypeDefinition @"
using System.Runtime.InteropServices;

namespace OCDevPackage
{
    [ComImport, Guid("2e941141-7f97-4756-ba1d-9decde894a3d"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IApplicationActivationManager
    {
        void ActivateApplication([MarshalAs(UnmanagedType.LPWStr)] string appUserModelId, [MarshalAs(UnmanagedType.LPWStr)] string arguments, int options, out uint processId);
    }

    [ComImport, Guid("45BA127D-10A8-46EA-8AB7-56EA9078943C")]
    class ApplicationActivationManager
    {
    }

    public static class Launcher
    {
        public static uint Activate(string appUserModelId)
        {
            uint processId;
            ((IApplicationActivationManager)new ApplicationActivationManager()).ActivateApplication(appUserModelId, null, 0, out processId);
            return processId;
        }
    }
}
"@

$package = Get-AppxPackage -Name $PackageName
if (-not $package) {
    throw "$PackageName is not registered, build the target register_dev_package"
}
$appUserModelId = "$($package.PackageFamilyName)!$ApplicationId"
$processId = [OCDevPackage.Launcher]::Activate($appUserModelId)
Write-Host "Activated $appUserModelId, pid $processId"
