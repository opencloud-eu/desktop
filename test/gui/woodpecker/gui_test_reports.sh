#!/bin/bash

REPORT_PATH="$PUBLIC_BUCKET/desktop/testlogs/$CI_PIPELINE_NUMBER/reports"
REPORT_URL="$MC_HOST/$REPORT_PATH"

echo ""
echo "--- GUI Test Reports ---"
echo "Test Report: $REPORT_URL/report.html"
echo "Client Log: $REPORT_URL/opencloud.log"
echo "AT_SPI Driver Log: $REPORT_URL/atspi_webdriver.log"

screenshots=$(mc find s3/$REPORT_PATH/screenshots/ 2>/dev/null || true)
if [[ -n "$screenshots" ]]; then
  echo "Screenshots:"
  for f in $screenshots; do
    # remove 's3/' prefix
    f=${f/s3\//}
    echo "  - $MC_HOST/$f"
  done
else
  echo "No screenshots found."
fi
