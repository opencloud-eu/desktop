# Enforce Strict Mode to Prevent weird variable behavior
Set-StrictMode -Version Latest;

# exit on errors, and suppress status display
$ErrorActionPreference = 'Stop';
$PSNativeCommandUseErrorActionPreference = $true;
$ProgressPreference = 'SilentlyContinue';

.woodpecker_files/multiplatform-build/init.ps1

.github/workflows/.craft.ps1 -c -v --no-cache --test opencloud/opencloud-desktop

exit 0
