#!/bin/bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

SRC_DIR=${BUILD_DIR:-SCRIPT_DIR/../build/_deps/tracylib-src}

echo "# Create directory: capture-build"
mkdir capture-build

echo "# Configure capture"
cmake -S"$SRC_DIR/capture" -Bcature-build

echo "# Build capture"
cmake --build capture-build