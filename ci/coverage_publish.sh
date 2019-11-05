#!/bin/bash

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# CircleCI logs.
set -e

[[ -z "${ENVOY_BUILD_DIR}" ]] && ENVOY_BUILD_DIR=/build
COVERAGE_FILE="${ENVOY_BUILD_DIR}/envoy/generated/coverage/index.html"

if [ ! -f "${COVERAGE_FILE}" ]; then
  echo "ERROR: Coverage file not found."
  exit 1
fi

if [[ -z "${GCP_SERVICE_ACCOUNT_KEY}" ]]; then
  echo "Coverage report will not be uploaded for this build."
  exit 0
else
  echo ${GCP_SERVICE_ACCOUNT_KEY} | base64 --decode | gcloud auth activate-service-account --key-file=-
fi


if [[ "${NO_COVERAGE_UPLOAD}" == "1" ]]; then
  echo "Skipping upload coverage report..."
  exit 0
else
  echo "Uploading coverage report..."
fi

if [[ "${BUILD_REASON}" == "PullRequest" ]]; then
  GCS_LOCATION="envoy-pr/${SYSTEM_PULLREQUEST_PULLREQUESTNUMBER}/coverage"
else
  GCS_LOCATION="envoy-coverage/report-${BRANCH_NAME}"
fi

COVERAGE_DIR="$(dirname "${COVERAGE_FILE}")"

gsutil -m rsync -dr ${COVERAGE_DIR} gs://${GCS_LOCATION}
echo "Coverage report for branch '${BRANCH_NAME}': https://storage.googleapis.com/${GCS_LOCATION}/index.html"
