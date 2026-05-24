#!/bin/bash
set -euo pipefail

qt_dir=${1}
build_target=${2:-all}

OS=$(uname)

source "$(dirname "$BASH_SOURCE")/impl/code-inspector.sh"
code_inspect "${ROOTPATH:-.}"

mkdir -p build
pushd build

if [[ "${RELEASE:-false}" == "true" ]]; then
    BUILD_TYPE="RelWithDebInfo"
fi

if [[ ${ASAN_INT:-0} -eq 1 ]]; then
    SANITIZERS="-DLUMEX_ASAN_INT=ON"
elif [[ ${ASAN:-0} -eq 1 ]]; then
    SANITIZERS="-DLUMEX_ASAN=ON"
elif [[ ${TSAN:-0} -eq 1 ]]; then
    SANITIZERS="-DLUMEX_TSAN=ON"
elif [[ ${LCOV:-0} -eq 1 ]]; then
    SANITIZERS="-DCOVERAGE=ON"
fi

ulimit -S -n 8192

cmake \
-G'Unix Makefiles' \
-DACTIVE_NETWORK=lumex_dev_network \
-DLUMEX_TEST=ON \
-DLUMEX_GUI=ON \
-DPORTABLE=1 \
-DLUMEX_WARN_TO_ERR=ON \
-DCMAKE_BUILD_TYPE=${BUILD_TYPE:-Debug} \
-DQt5_DIR=${qt_dir} \
${SANITIZERS:-} \
..

if [[ "$OS" == 'Linux' ]]; then
    if [[ ${LCOV:-0} == 1 ]]; then
        cmake --build ${PWD} --target generate_coverage -- -j2
    else
        cmake --build ${PWD} --target ${build_target} -- -j2
    fi
else
    sudo cmake --build ${PWD} --target ${build_target} -- -j2
fi

popd
