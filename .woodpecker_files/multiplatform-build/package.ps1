# Enforce Strict Mode to Prevent weird variable behavior
Set-StrictMode -Version Latest;

# exit on errors, and suppress status display
$ErrorActionPreference = 'Stop';
$PSNativeCommandUseErrorActionPreference = $true;
$ProgressPreference = 'SilentlyContinue';

.woodpecker_files/multiplatform-build/init.ps1

if ($env:CRAFT_TARGET -ne "macos-clang-arm64" ) {
    .github/workflows/.craft.ps1 -c --no-cache --package opencloud/opencloud-desktop
}
if ($IsWindows -and $env:CI_PIPELINE_EVENT -ne "pull_request") {
    .github/workflows/.craft.ps1 -c --no-cache --package --options "[Packager]PackageType=AppxPackager" --options "[Packager]Destination=$env:CI_WORKSPACE/appx/" opencloud/opencloud-desktop
}

Copy-Item ${HOME}/craft/binaries/* binaries/
try {
    Copy-Item appx/* binaries/
} catch {}

exit 0
