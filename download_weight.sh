#!/bin/bash
set -e

# This script should be run from the root of the repository.
if [ ! -d "script" ] || [ ! -f ".gitignore" ]; then
    echo "Error: This script must be run from the root of the repository."
    exit 1
fi

# This script downloads and extracts a model release from a GitHub URL.

RELEASE_NAME="tiny_llm_server_model"
VERSION="0.1.0" # This should match the version in release_weight.sh
TARBALL_NAME="${RELEASE_NAME}-v${VERSION}.tar.gz"

# GITHUB URL
DOWNLOAD_URL="https://github.com/entropicemergence/tiny_llm_server/releases/download/v${VERSION}/${TARBALL_NAME}"

DOWNLOAD_DIR="downloads"
mkdir -p $DOWNLOAD_DIR

DOWNLOADED_FILE_PATH="${DOWNLOAD_DIR}/${TARBALL_NAME}"

echo "Downloading release from ${DOWNLOAD_URL}..."
# Using curl to download. -L follows redirects.
curl -L -o "${DOWNLOADED_FILE_PATH}" "${DOWNLOAD_URL}"

if [ ! -f "${DOWNLOADED_FILE_PATH}" ] || [ ! -s "${DOWNLOADED_FILE_PATH}" ]; then
    echo "Error: Download failed or the downloaded file is empty. Please check the URL."
    # clean up empty file if it exists
    [ -f "${DOWNLOADED_FILE_PATH}" ] && rm "${DOWNLOADED_FILE_PATH}"
    exit 1
fi

if [ -d "model" ]; then
    echo "Found existing 'model' directory. Backing it up to 'model.bak'..."
    if [ -d "model.bak" ]; then
        rm -rf "model.bak"
    fi
    mv "model" "model.bak"
fi

echo "Download complete. Extracting..."
# This will extract the 'model' directory into the current directory.
tar -xzvf "${DOWNLOADED_FILE_PATH}"

echo "Extraction complete."
echo "Model files are now in the 'model' directory."

echo "Cleaning up downloaded file..."
rm -rf "${DOWNLOAD_DIR}"

echo "Done."
