# Enforce Strict Mode to Prevent weird variable behavior
Set-StrictMode -Version Latest;

# exit on errors, and suppress status display
$ErrorActionPreference = 'Stop';
$PSNativeCommandUseErrorActionPreference = $true;
$ProgressPreference = 'SilentlyContinue';

git clone --depth=1 https://invent.kde.org/kde/craftmaster.git $env:CI_WORKSPACE/woodpecker_craft/CraftMaster/CraftMaster
.woodpecker_files/multiplatform-build/init.ps1

New-Item -Path $HOME/cache -ItemType Directory -ErrorAction SilentlyContinue
.github/workflows/.craft.ps1 --setup
.github/workflows/.craft.ps1 -c --unshelve .craft.shelf
# bootstrap in case the shelf was empty
.github/workflows/.craft.ps1 -c craft
# ccache
if (-not $IsWindows) {
    & ".github/workflows/.craft.ps1" -c dev-utils/ccache
    & ".github/workflows/.craft.ps1" -c --run "ccache -M500MB"
    & ".github/workflows/.craft.ps1" -c --run "ccache -s"
}

New-Item -ItemType Directory binaries
.github/workflows/.craft.ps1 -c --set "forceAsserts$($env:CI_PIPELINE_EVENT -ne "tag")" opencloud/opencloud-desktop
.github/workflows/.craft.ps1 -c --set "srcDir=$env:CI_WORKSPACE" opencloud/opencloud-desktop
.github/workflows/.craft.ps1 -c --set "buildNumber=$env:CI_PIPELINE_NUMBER" opencloud/opencloud-desktop
# optional deployment dependencies
if ($IsWindows) {
    & .github/workflows/.craft.ps1 -c dev-utils/nsis
} elseif($IsLinux) {
    & .github/workflows/.craft.ps1 -c dev-utils/linuxdeploy
}

.github/workflows/.craft.ps1 -c --install-deps opencloud/opencloud-desktop

if($env:CRAFT_TARGET -eq "linux-gcc-x86_64") {
    & ".github/workflows/.craft.ps1" -c libs/qt6/qttools
    & ".github/workflows/.craft.ps1" -c --run pwsh -c "git ls-files "*.qml" "*.js" "*.mjs" | %{ qmlformat -i `$_}"
    $diff = git diff
    if ($diff) {
        $diff
        exit 1
    }

    & ".github/workflows/.craft.ps1" -c libs/llvm
    if($env:CI_PIPELINE_EVENT -eq "pull_request") {
        & ".github/workflows/.craft.ps1" -c --run git clang-format --force  "$env:CI_PREV_COMMIT_SHA..$env:CI_COMMIT_SHA"
        $diff = git diff
        if ($diff) {
            $diff
            exit 1
        }
    }
}

.github/workflows/.craft.ps1 -c --shelve .craft.shelf
Copy-Item .craft.shelf binaries/craft.shelf

if ($env:CRAFT_TARGET -eq "macos-64-clang-debug" ) {
    # https://api.kde.org/ecm/module/ECMEnableSanitizers.html
    # address;leak;undefined
    # clang: error: unsupported option '-fsanitize=leak' for target 'x86_64-apple-darwin21.6.0'
    & .github/workflows/.craft.ps1" -c --set args="-DECM_ENABLE_SANITIZERS='address;undefined' opencloud/opencloud-desktop
}

.github/workflows/.craft.ps1 -c --no-cache opencloud/opencloud-desktop

exit 0
