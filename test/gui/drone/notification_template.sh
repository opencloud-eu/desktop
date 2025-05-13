#!/usr/bin/env bash

# $1 - template directory
#
# Generates a template file for notification

COMMIT_SHA_SHORT=${DRONE_COMMIT:0:8}
SERVERS=("oc10" "ocis")
BUILD_STATUS=":white_check_mark:**Success**"
TEST_LOGS=""
BRANCH_NAME="${DRONE_BRANCH}"

if [ "${DRONE_BUILD_STATUS}" == "failure" ]; then
    BUILD_STATUS=":x:**Failure**"
fi

for server in "${SERVERS[@]}"; do
    LOG_URL_PATH="${CACHE_ENDPOINT}/${CACHE_BUCKET}/${DRONE_REPO}/${DRONE_BUILD_NUMBER}/${server}/guiReportUpload"
    CURL="curl --write-out %{http_code} --silent --output /dev/null"

    LOGS=""
    GUI_LOG="${LOG_URL_PATH}/index.html"

    GUI_STATUS_CODE=$($CURL "$GUI_LOG")

    if [[ "$GUI_STATUS_CODE" == "200" ]]; then
        LOGS+=": [Squish Report]($GUI_LOG)"
    fi

    if [ "${DRONE_BUILD_STATUS}" == "failure" ]; then
        SERVER_LOG="${LOG_URL_PATH}/serverlog.log"
        STACKTRACE="${LOG_URL_PATH}/stacktrace.log"

        SERVER_STATUS_CODE=$($CURL "$SERVER_LOG")
        STACKTRACE_STATUS_CODE=$($CURL "$STACKTRACE")

        if [[ "$SERVER_STATUS_CODE" == "200" ]]; then
            LOGS+="\n> [Server log]($SERVER_LOG)  " # 2 spaces at the end act as line-break
        fi
        if [[ "$STACKTRACE_STATUS_CODE" == "200" ]]; then
            LOGS+="\n> [Stacktrace]($STACKTRACE)"
        fi
    fi

    if [[ -n "${LOGS}" ]]; then
        LOGS="\n${server}${LOGS}"
        TEST_LOGS+="${LOGS}"
    fi
done

if [ "${DRONE_BUILD_EVENT}" == "tag" ]; then
    BRANCH_NAME="Tag: \`${DRONE_TAG}\`"
fi

echo -e "$BUILD_STATUS [${DRONE_REPO}#${COMMIT_SHA_SHORT}](${DRONE_BUILD_LINK}) (${BRANCH_NAME}) by **${DRONE_COMMIT_AUTHOR}** $TEST_LOGS" >"$1"/template.md
