#!/bin/bash
set -euox pipefail

LUMEX_TEST=ON \
LUMEX_NETWORK=dev \
LUMEX_GUI=ON \
$(dirname "$BASH_SOURCE")/build.sh all_tests