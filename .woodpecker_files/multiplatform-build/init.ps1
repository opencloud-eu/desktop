git config --system --add safe.directory "*"
$env:CRAFT_PACKAGE_SYMBOLS=Write-Output ($env:CI_PIPELINE_EVENT -ne "pull_request")

if ($IsWindows) {
    Write-Output "Creating Junction"
    New-Item -itemtype Junction -path $HOME/craft -value $env:CI_WORKSPACE/woodpecker_craft
} else {
    Write-Output "Creating SymLink"
    New-Item -itemtype SymbolicLink -path $HOME/craft -value $env:CI_WORKSPACE/woodpecker_craft
}

return 0
