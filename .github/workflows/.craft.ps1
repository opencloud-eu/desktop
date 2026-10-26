# enable handling of exit codes in powershell
$PSNativeCommandUseErrorActionPreference=$true

# locate the latest python 3 installed on the system
function Find-LatestPython {
    $candidates = @()
    if ($IsWindows) {
        # the py launcher lists all registered installations with their paths
        if (Get-Command py -ErrorAction SilentlyContinue) {
            $candidates += (py -0p) | ForEach-Object {
                if ($_ -match '([A-Za-z]:\\.*\.exe)\s*$') { $Matches[1] }
            }
        }
    }
    # python3, python3.12, python3.13, ... from PATH
    $candidates += Get-Command -CommandType Application -Name "python3*" -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^python3(\.\d+)?(\.exe)?$' } |
        ForEach-Object { $_.Source }

    $best = $null
    $bestVersion = $null
    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        try {
            $info = & $candidate -c "import sys; print('.'.join(map(str, sys.version_info[:3]))); print(sys.executable)"
            $version = [version]$info[0]
            if (-not $bestVersion -or $version -gt $bestVersion) {
                $bestVersion = $version
                $best = $info[1]
            }
        } catch {
            Write-Host "Ignoring broken python: ${candidate}"
        }
    }
    if (-not $best) {
        throw "Failed to locate a python 3 installation"
    }
    Write-Host "Using python ${bestVersion}: ${best}"
    return $best
}

$python = Find-LatestPython
if ($IsWindows) {
    $env:HOME = ${HOME}
}

# workaround, prevent CraftMaster detecting the platform as android
$env:ANDROID_NDK = $null

$RepoRoot = (Get-Item $PSScriptRoot).Parent.Parent
$command = @("${HOME}/craft/CraftMaster/CraftMaster/CraftMaster.py",
             "--config", "${RepoRoot}/.craft.ini",
             "--config-override", "${RepoRoot}/.github/workflows/craft_override.ini",
             "--target", "${env:CRAFT_TARGET}",
             "--variables", "WORKSPACE=${HOME}/craft") + $args

Write-Host "Exec: ${python} ${command}"

& $python @command
if ($LASTEXITCODE -ne 0) {
    exit 1
}
