#!/usr/bin/env bash
#
# boo_hook.sh
# Claude Code PreToolUse フック
#
# Claude Code がツールを実行する前に Boo デバイスで承認を求める。
# boo_bridge.py が SSE モードで起動している必要がある。
#
# 依存: curl, jq
#
# 設定例 (.claude/settings.local.json):
#   {
#     "hooks": {
#       "PreToolUse": [
#         {
#           "matcher": "Bash",
#           "hooks": [{"type": "command", "command": "bash /path/to/boo_hook.sh"}]
#         }
#       ]
#     }
#   }

BOO_URL="http://host.docker.internal:8765/request"
TIMEOUT_SEC=30

# ---- stdin からフックペイロードを読む ----
INPUT=$(cat)

TOOL_NAME=$(echo "$INPUT" | jq -r '.tool_name // "unknown"')
COMMAND=$(echo "$INPUT"  | jq -r '.tool_input.command // ""')

# ---- 危険パターンの検出 ----
DANGER="false"
if echo "$COMMAND" | grep -qiE \
  'rm |rmdir|git push --force|git push -f|git reset --hard|drop table|drop database|sudo rm|chmod 777|mkfs|dd if='; then
  DANGER="true"
fi

# ---- リクエストボディを構築 ----
REQUEST_BODY=$(jq -nc \
  --arg     tool    "$TOOL_NAME" \
  --arg     details "$COMMAND" \
  --argjson danger  "$DANGER" \
  --argjson timeout "$TIMEOUT_SEC" \
  '{tool: $tool, details: $details, danger: $danger, timeout: $timeout}')

# ---- Boo デバイスに承認リクエストを送信 ----
RESULT=$(curl -s --max-time $((TIMEOUT_SEC + 5)) \
  -X POST "$BOO_URL" \
  -H "Content-Type: application/json" \
  -d "$REQUEST_BODY" 2>/dev/null) || RESULT=""

# ---- サーバー未起動: フェイルオープン ----
if [ -z "$RESULT" ]; then
  echo "[boo_hook] Warning: boo server unreachable" >&2
  echo '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"allow"}}'
  exit 0
fi

# ---- 承認結果に応じて decision を返す ----
APPROVED=$(echo "$RESULT" | jq -r '.approved // false' 2>/dev/null || echo "false")

if [ "$APPROVED" = "true" ]; then
  echo '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"allow"}}'
else
  jq -nc \
    --arg reason "Boo デバイスで否認されました: $TOOL_NAME" \
    '{hookSpecificOutput: {hookEventName: "PreToolUse", permissionDecision: "deny", permissionDecisionReason: $reason}}'
fi

exit 0
