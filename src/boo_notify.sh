#!/usr/bin/env bash
# boo_notify.sh [working|idle]
# PostToolUse フック (working) および Stop フック (idle) から呼び出される

BOO_URL="http://host.docker.internal:8765"
MODE="${1:-idle}"

if [ "$MODE" = "working" ]; then
    INPUT=$(cat)
    TOOL_NAME=$(echo "$INPUT" | jq -r '.tool_name // ""' 2>/dev/null || echo "")
    curl -s --max-time 3 -X POST "$BOO_URL/working" \
        -H "Content-Type: application/json" \
        -d "{\"tool\":\"$TOOL_NAME\"}" > /dev/null 2>&1 || true
else
    curl -s --max-time 3 -X POST "$BOO_URL/idle" \
        -H "Content-Type: application/json" \
        -d '{}' > /dev/null 2>&1 || true
fi

exit 0
