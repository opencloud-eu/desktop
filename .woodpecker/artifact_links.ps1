echo "Artifact Downloads:";
get-childitem binaries | % {
    $n=$_.name;
    echo "https://$env:MC_HOST/public/$env:CI_REPO_NAME/pipeline/$env:CI_PIPELINE_NUMBER/$env:TARGET/$n";
}
exit 0