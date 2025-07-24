$ErrorActionPreference = 'Stop'
dir $HOME
New-Item -Path $HOME/cache -ItemType Directory -ErrorAction SilentlyContinue
dir $HOME
.github/workflows/.craft.ps1 --setup
.github/workflows/.craft.ps1 -c --unshelve .craft.shelf
.github/workflows/.craft.ps1 -c craft
New-Item -ItemType Directory binaries
.github/workflows/.craft.ps1 -c --set "forceAsserts$($env:CI_PIPELINE_EVENT -ne "tag")" opencloud/opencloud-desktop
.github/workflows/.craft.ps1 -c --set "srcDir=$env:CI_WORKSPACE" opencloud/opencloud-desktop
.github/workflows/.craft.ps1 -c --set "buildNumber=$env:CI_PIPELINE_NUMBER" opencloud/opencloud-desktop
.github/workflows/.craft.ps1 -c dev-utils/nsis
.github/workflows/.craft.ps1 -c --install-deps opencloud/opencloud-desktop
.github/workflows/.craft.ps1 -c --shelve .craft.shelf
Copy-Item .craft.shelf binaries/craft.shelf
.github/workflows/.craft.ps1 -c --no-cache opencloud/opencloud-desktop
.github/workflows/.craft.ps1 -c --no-cache --test opencloud/opencloud-desktop
.github/workflows/.craft.ps1 -c --no-cache --package opencloud/opencloud-desktop
.github/workflows/.craft.ps1 -c --no-cache --package --options "[Packager]PackageType=AppxPackager" --options "[Packager]Destination=$env:CI_WORKSPACE/appx/" opencloud/opencloud-desktop
Copy-Item ${HOME}/craft/binaries/* binaries/
Copy-Item appx/* binaries/
dir binaries