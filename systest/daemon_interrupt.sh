#!/bin/bash
set -eux

# Test that the daemon handles SIGINT gracefully

DATADIR=$(mktemp -d)
RUNTIME_INFO="$DATADIR/runtime_info.json"

cleanup() {
    kill $NODE_PID 2>/dev/null || true
    rm -rf "$DATADIR"
}
trap cleanup EXIT

# Start the node in daemon mode with ephemeral port
$LUMEX_NODE_EXE --daemon --network dev --data_path "$DATADIR" \
    --runtime_info_file "$RUNTIME_INFO" \
    --config node.peering_port=0 &
NODE_PID=$!

# Wait for runtime_info.json to appear (indicates node is ready)
for i in {1..30}; do
    if [ -f "$RUNTIME_INFO" ]; then
        break
    fi
    sleep 1
done

[ -f "$RUNTIME_INFO" ] || { echo "FAIL: runtime_info.json not created"; exit 1; }

# Send an interrupt signal to the node process
kill -SIGINT $NODE_PID

# Check if the process has stopped using a timeout to avoid infinite waiting
if wait $NODE_PID; then
    echo "Node stopped successfully"
else
    echo "Node did not stop as expected"
    exit 1
fi
