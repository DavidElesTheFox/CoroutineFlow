#!/bin/bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

SRC_DIR=${BUILD_DIR:-$SCRIPT_DIR/../build/_deps/tracylib-src}

echo "# Create directory: capture-build"
mkdir -p capture-build

echo "# Configure capture"
cmake -S"$SRC_DIR/capture" -B$SCRIPT_DIR/capture-build

echo "# Build capture"
cmake --build $SCRIPT_DIR/capture-build

ln -s $SCRIPT_DIR/capture-build/tracy-capture $SCRIPT_DIR/bin/tracy-capture