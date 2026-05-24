#!/bin/bash
set -eu

# Test that node exits gracefully when port is already in use.
# Verifies:
# - No uncaught exception (terminate called)
# - Non-zero exit code for error
# - Exit code is not from signal termination

DATADIR1=$(mktemp -d)
DATADIR2=$(mktemp -d)
RUNTIME_INFO="$DATADIR1/runtime_info.json"

cleanup() {
    kill $NODE1_PID 2>/dev/null || true
    rm -rf "$DATADIR1" "$DATADIR2"
}
trap cleanup EXIT

# Start first node with ephemeral port
$LUMEX_NODE_EXE --daemon --network dev --data_path "$DATADIR1" \
    --runtime_info_file "$RUNTIME_INFO" \
    --config node.peering_port=0 &
NODE1_PID=$!

# Wait for runtime_info.json to appear (indicates node is ready)
for i in {1..30}; do
    if [ -f "$RUNTIME_INFO" ]; then
        break
    fi
    sleep 1
done

[ -f "$RUNTIME_INFO" ] || { echo "FAIL: runtime_info.json not created"; exit 1; }

# Get the peering port from runtime info
PORT=$(jq -r '.peering_port' "$RUNTIME_INFO")
[ "$PORT" != "0" ] && [ -n "$PORT" ] && [ "$PORT" != "null" ] || { echo "FAIL: invalid peering_port"; exit 1; }

# Start second node on same port - should fail gracefully
set +e
OUTPUT=$($LUMEX_NODE_EXE --daemon --network dev --data_path "$DATADIR2" --config node.peering_port=$PORT 2>&1)
EXIT_CODE=$?
set -e

# Check for any uncaught exception causing terminate
if echo "$OUTPUT" | grep -q "terminate called"; then
    echo "FAIL: Node crashed with uncaught exception"
    echo "$OUTPUT"
    exit 1
fi

# Check exit code is non-zero (indicates error was detected)
if [ $EXIT_CODE -eq 0 ]; then
    echo "FAIL: Node should exit with non-zero code on bind failure"
    exit 1
fi

# Check exit code is not from signal (128+ indicates killed by signal)
if [ $EXIT_CODE -ge 128 ]; then
    echo "FAIL: Node terminated by signal (exit code $EXIT_CODE)"
    echo "$OUTPUT"
    exit 1
fi

echo "PASS: Node exited gracefully with code $EXIT_CODE"
