run-clang-tidy -p "$env:BUILD_DIR" | Tee-Object -Path "$([System.IO.Path]::GetTempPath())/clang-tidy.log"
