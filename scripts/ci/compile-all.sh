#!/bin/bash
set -e

BUILD_SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )


cmake --preset dev-g++
cmake --preset dev-g++-release
cmake --preset dev-clang
cmake --preset dev-clang-release
cmake --preset dev-clang-undef-sanitizer

cmake --build --preset dev-g++-build
cmake --build --preset dev-g++-release-build
cmake --build --preset dev-clang-build
cmake --build --preset dev-clang-release-build
cmake --build --preset dev-clang-undef-sanitizer-build

ctest --preset test-clang-debug
ctest --preset test-g++-release
ctest --preset test-g++-debug
ctest --preset test-clang-release
ctest --preset test-clang-undef-sanitizer