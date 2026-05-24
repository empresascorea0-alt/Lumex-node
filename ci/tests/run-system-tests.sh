#!/bin/bash
set -uo pipefail

source "$(dirname "$BASH_SOURCE")/common.sh"

# Path to the lumex-node repository can be provided as an argument
# Otherwise parent directory of working directory is assumed
LUMEX_REPO_DIR=${1:-../}
LUMEX_SYSTEST_DIR=${LUMEX_REPO_DIR}/systest

# Allow TEST_TIMEOUT to be set from an environment variable
TEST_TIMEOUT=${TEST_TIMEOUT:-300s}

echo "Running systests from: ${LUMEX_SYSTEST_DIR}"

# This assumes that the executables are in the current working directory
export LUMEX_NODE_EXE=./lumex_node$(get_exec_extension)
export LUMEX_RPC_EXE=./lumex_rpc$(get_exec_extension)

# Enable core dumps for this process
if [ -n "${COREDUMP_DIR-}" ]; then
    ulimit -c unlimited
fi

overall_status=0

for script in ${LUMEX_SYSTEST_DIR}/*.sh; do
    name=$(basename ${script})

    echo "::group::Running: $name"

    # Redirecting output to a file to prevent it from being mixed with the output of the action
    # Using timeout command to enforce time limits
    timeout $TEST_TIMEOUT ./$script > "${name}.log" 2>&1
    status=$?
    cat "${name}.log"
    
    # Show core dumps after each test
    if [ -n "${COREDUMP_DIR-}" ]; then
        "$(dirname "$BASH_SOURCE")/show-core-dumps.sh" "${LUMEX_NODE_EXE}"
    fi

    echo "::endgroup::"

    if [ $status -eq 0 ]; then
        echo "Passed: $name"
    elif [ $status -eq 124 ]; then
        echo "::error::Systest timed out: $name"
        overall_status=1
    else
        echo "::error::Systest failed: $name ($status)"
        overall_status=1
    fi
done

if [ $overall_status -eq 0 ]; then
    echo "All systests passed"
else
    echo "::error::Some systests failed"
    exit 1
fi
