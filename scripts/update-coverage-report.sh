#!/bin/bash

set -e

usage() { echo "Usage: $0 -o <output-dir> [-h]"; } 


while getopts "o:h" o; do
    case "${o}" in
        o)
            UCR_OUTPUT_DIR=${OPTARG}
            ;;
        h)
            usage
            exit 1
            ;;
        *)
            usage
            exit 1
            ;;
    esac
done
shift $((OPTIND-1))

if [ -z "${UCR_OUTPUT_DIR}" ]
then
    usage
    exit 1
fi

UCR_OLD_DIR=`pwd`

rm -rf "${UCR_OUTPUT_DIR}"
cmake --preset=dev-clang-coverage -B "${UCR_OUTPUT_DIR}"
cmake --build ${UCR_OUTPUT_DIR}
cd "${UCR_OUTPUT_DIR}"
LLVM_PROFILE_FILE=coverage-%m-%p.profraw ctest
cd tests
llvm-profdata-19 merge -o tests.profdata $(find . -name "*.profraw")
llvm-cov-19 report ./unit.* ./functional.* --instr-profile=./tests.profdata -sources /app/include/coroutine_flow/* > "../coverage.report"

echo "Report: $UCR_OUTPUT_DIR/coverage.report"
cd "$UCR_OLD_DIR"
