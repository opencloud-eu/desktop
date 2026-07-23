param(
    [Parameter(ValueFromRemainingArguments=$true)]
    [string[]]$nargs
)

$ErrorActionPreference = "Stop"

Write-Output "Installing SignPath module..."
Install-Module -Name SignPath -Force

Write-Output "Begin Signing artifacts with SignPath"

if ($nargs.Count -gt 1) {
    $zipPath = "artifacts.zip"
    Write-Output "Creating zip archive: $zipPath"
    Compress-Archive -Path $nargs -DestinationPath $zipPath -Force

    Write-Output "Signing artifact: $zipPath"
    Submit-SigningRequest `
    -InputArtifactPath "$zipPath" `
    -ApiToken "${Env:SIGNPATH_API_TOKEN}" `
    -OrganizationId "${Env:SIGNPATH_ORGANIZATION_ID}" `
    -ProjectSlug "desktop" `
    -SigningPolicySlug "test-signing" `
    -ArtifactConfigurationSlug "oc_zip" `
    -OutputArtifactPath "$zipPath" `
    -Force `
    -WaitForCompletion

    Write-Output "Finished signing artifact: $zipPath"
} else {
    Write-Output "Signing artifact: $($nargs)"

    Submit-SigningRequest `
    -InputArtifactPath "$($nargs)" `
    -ApiToken "${Env:SIGNPATH_API_TOKEN}" `
    -OrganizationId "${Env:SIGNPATH_ORGANIZATION_ID}" `
    -ProjectSlug "desktop" `
    -SigningPolicySlug "test-signing" `
    -ArtifactConfigurationSlug "msix" `
    -OutputArtifactPath "$($nargs)" `
    -Force `
    -WaitForCompletion

    Write-Output "Finished signing artifact: $($nargs)"
}

Write-Output "Finished signing"
