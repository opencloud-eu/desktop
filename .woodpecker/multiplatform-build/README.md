# OpenCloud Desktop - Multiplatform Build

These are the scritps used by the woodpecker pipeline "multiplatform-build.yaml"

- "init.ps1" is used by all scripts to link CraftMaster into $HOME, set ENV-Vars and prepare git
- "build.ps1" prepares the build environment and builds the desktop client using CraftMaster
- "test.ps1" runs the cTests
- "package.ps1" uploads binaries and the shelf
- "print_links.ps1" prints download links to the log for easy artifact download
