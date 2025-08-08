Write-Output "Artifact Downloads:";
get-childitem binaries | ForEach-Object {
    $n=$_.name;
    Write-Output "$env:CACHE_HOST/$env:BUCKET/$env:CI_REPO_NAME/pipeline_$env:CI_PIPELINE_NUMBER/$env:TARGET/binaries/$n";
}
exit 0