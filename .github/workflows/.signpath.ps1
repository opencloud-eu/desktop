# For Craft to create sideload packages it needs CodeSigning to be enabled.
# We are setting a custom command (this script) to skip singing.
# We later sign using the SignPath GitHub Action.

Write-Output "use github action for deep signing, return 0 to force unsigned sideload artifact"
