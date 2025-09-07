#!/bin/bash
set -e

# This script creates a release tarball of the model directory.

# The script should be run from the root of the repository.
if [ ! -d "model" ]; then
    echo "Error: 'model' directory not found. This script should be run from the root of the repository."
    exit 1
fi

RELEASE_DIR="release_weight"
mkdir -p $RELEASE_DIR

RELEASE_NAME="tiny_llm_server_model"
VERSION="0.1.0"
TARBALL_NAME="${RELEASE_NAME}-v${VERSION}.tar.gz"
TARBALL_PATH="${RELEASE_DIR}/${TARBALL_NAME}"

echo "Creating release tarball at ${TARBALL_PATH}..."
tar -czvf "${TARBALL_PATH}" model/

echo "Release created successfully: ${TARBALL_PATH}"
echo "Files in the release:"
tar -tf "${TARBALL_PATH}"
