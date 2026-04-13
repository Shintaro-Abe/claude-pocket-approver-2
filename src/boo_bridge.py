#!/usr/bin/env python3
"""
boo_bridge.py
Claude Code ↔ ブー(Boo)デバイス ブリッジスクリプト

使い方 (stdio モード):
  python boo_bridge.py --port COM4
  → Claude Code の settings.json に command/args で登録する

使い方 (SSE モード):
  python boo_bridge.py --port COM4 --transport sse [--http-port 8765]
  → Dev Container の settings.json に url: http://host.docker.internal:8765/sse を登録する

MCP ツール:
  - approve_request(tool, details, danger, timeout) → {approved: bool}
  - notify_working(tool_name)
  - notify_idle()
  - update_tokens(total, today)

依存ライブラリ:
  pip install pyserial mcp                         # stdio モード
  pip install pyserial mcp starlette uvicorn       # SSE モード
"""

import asyncio
import json
import sys
import time
import argparse
import threading
from typing import Optional
import serial
import serial.tools.list_ports
from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp import types

# ============================================================
# シリアルデバイス管理
# ============================================================
class BooDevice:
    def __init__(self, port: str, baud: int = 115200):
        self.port     = port
        self.baud     = baud
        self._ser: Optional[serial.Serial] = None
        self._lock    = threading.Lock()
        self._pending: Optional[asyncio.Future] = None
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._read_thread: Optional[threading.Thread] = None
        self._approval_lock = asyncio.Lock()  # 同時承認リクエストを直列化

    def connect(self) -> bool:
        try:
            self._ser = serial.Serial(
                self.port, self.baud,
                timeout=0.1,
                write_timeout=2.0
            )
            time.sleep(1.5)   # デバイスリセット待ち
            self._loop = asyncio.get_event_loop()
            self._read_thread = threading.Thread(
                target=self._reader, daemon=True
            )
            self._read_thread.start()
            print(f"[boo] Connected to {self.port}", file=sys.stderr)
            return True
        except serial.SerialException as e:
            print(f"[boo] Serial error: {e}", file=sys.stderr)
            return False

    def disconnect(self):
        if self._ser and self._ser.is_open:
            self._ser.close()

    def _send(self, obj: dict):
        if not self._ser or not self._ser.is_open:
            return
        data = json.dumps(obj, ensure_ascii=False) + "\n"
        with self._lock:
            self._ser.write(data.encode("utf-8"))

    def _reader(self):
        """バックグラウンドで受信を監視する"""
        buf = b""
        while True:
            try:
                if self._ser and self._ser.is_open:
                    chunk = self._ser.read(64)
                    if chunk:
                        buf += chunk
                        while b"\n" in buf:
                            line, buf = buf.split(b"\n", 1)
                            self._on_line(line.decode("utf-8", errors="ignore").strip())
                else:
                    time.sleep(0.1)
            except Exception as e:
                print(f"[boo] Reader error: {e}", file=sys.stderr)
                time.sleep(0.5)

    def _on_line(self, line: str):
        """デバイスからの応答を処理"""
        if not line:
            return
        try:
            obj = json.loads(line)
            if "approved" in obj and self._pending and self._loop:
                fut = self._pending
                self._pending = None
                self._loop.call_soon_threadsafe(fut.set_result, obj["approved"])
        except json.JSONDecodeError:
            pass

    # ---- 公開 API ----

    async def request_approval(
        self,
        tool: str,
        details: str = "",
        danger: bool = False,
        timeout: int = 30
    ) -> bool:
        """承認リクエストを送信し、結果を待つ（同時リクエストは直列化）"""
        async with self._approval_lock:
            fut = asyncio.get_event_loop().create_future()
            self._pending = fut
            self._send({
                "type":    "approve",
                "tool":    tool[:47],
                "details": details[:95],
                "danger":  danger,
                "timeout": timeout
            })
            try:
                approved = await asyncio.wait_for(fut, timeout=timeout + 5)
                return approved
            except asyncio.TimeoutError:
                self._pending = None
                return False

    def notify_working(self, tool_name: str = ""):
        self._send({"type": "working", "tool": tool_name})

    def notify_idle(self):
        self._send({"type": "idle"})

    def update_tokens(self, total: int, today: int):
        self._send({"type": "tokens", "total": total, "today": today})


# ============================================================
# デバイス自動検出
# ============================================================
def auto_detect_port() -> Optional[str]:
    candidates = [
        "usbserial", "usbmodem", "SLAB_USB", "CH340",
        "CP210", "FT232", "usbserial-"
    ]
    for port in serial.tools.list_ports.comports():
        desc = (port.description or "") + (port.hwid or "")
        if any(c.lower() in desc.lower() for c in candidates):
            print(f"[boo] Auto-detected: {port.device} ({port.description})",
                  file=sys.stderr)
            return port.device
    return None


# ============================================================
# MCP サーバー定義
# ============================================================
def build_mcp_server(device: BooDevice) -> Server:
    server = Server("boo-approval-server")

    @server.list_tools()
    async def list_tools() -> list[types.Tool]:
        return [
            types.Tool(
                name="approve_request",
                description=(
                    "ブー(Boo)デバイスに承認リクエストを表示し、"
                    "ユーザーの承認/否認結果を返す。"
                    "Claude Code が危険な操作を行う前に呼び出す。"
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "tool": {
                            "type": "string",
                            "description": "ツール名 (例: Bash, FileWrite)"
                        },
                        "details": {
                            "type": "string",
                            "description": "操作の詳細 (例: rm -rf /tmp/cache)"
                        },
                        "danger": {
                            "type": "boolean",
                            "description": "危険操作フラグ (デフォルト: false)",
                            "default": False
                        },
                        "timeout": {
                            "type": "integer",
                            "description": "タイムアウト秒数 (デフォルト: 30)",
                            "default": 30
                        }
                    },
                    "required": ["tool"]
                }
            ),
            types.Tool(
                name="notify_working",
                description="ブーに作業中であることを通知する (ビジーアニメを表示)",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "tool_name": {
                            "type": "string",
                            "description": "実行中のツール名"
                        }
                    }
                }
            ),
            types.Tool(
                name="notify_idle",
                description="ブーにアイドル状態を通知する",
                inputSchema={"type": "object", "properties": {}}
            ),
            types.Tool(
                name="update_tokens",
                description="トークン使用量をブーに通知する",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "total": {
                            "type": "integer",
                            "description": "累計トークン数"
                        },
                        "today": {
                            "type": "integer",
                            "description": "今日のトークン数"
                        }
                    },
                    "required": ["total", "today"]
                }
            ),
        ]

    @server.call_tool()
    async def call_tool(
        name: str, arguments: dict
    ) -> list[types.TextContent]:

        if name == "approve_request":
            approved = await device.request_approval(
                tool    = arguments.get("tool", "unknown"),
                details = arguments.get("details", ""),
                danger  = arguments.get("danger", False),
                timeout = arguments.get("timeout", 30)
            )
            return [types.TextContent(
                type = "text",
                text = json.dumps({"approved": approved})
            )]

        elif name == "notify_working":
            device.notify_working(arguments.get("tool_name", ""))
            return [types.TextContent(type="text", text='{"ok":true}')]

        elif name == "notify_idle":
            device.notify_idle()
            return [types.TextContent(type="text", text='{"ok":true}')]

        elif name == "update_tokens":
            device.update_tokens(
                total = arguments.get("total", 0),
                today = arguments.get("today", 0)
            )
            return [types.TextContent(type="text", text='{"ok":true}')]

        return [types.TextContent(type="text", text='{"error":"unknown tool"}')]

    return server


# ============================================================
# SSE トランスポート
# ============================================================
async def _run_sse(server: Server, device, port: int):
    """SSE トランスポートで MCP サーバーを起動する (starlette + uvicorn)"""
    try:
        from mcp.server.sse import SseServerTransport
        from starlette.applications import Starlette
        from starlette.routing import Route
        from starlette.responses import JSONResponse
        import uvicorn
    except ImportError as e:
        print(
            f"[boo] ERROR: SSE モードには追加パッケージが必要です: {e}\n"
            "  pip install starlette uvicorn",
            file=sys.stderr
        )
        sys.exit(1)

    sse_transport = SseServerTransport("/messages")

    async def handle_sse(request):
        async with sse_transport.connect_sse(
            request.scope, request.receive, request._send
        ) as streams:
            await server.run(
                streams[0], streams[1],
                server.create_initialization_options()
            )

    async def handle_messages(request):
        await sse_transport.handle_post_message(
            request.scope, request.receive, request._send
        )

    async def handle_request(request):
        """PreToolUse フックから呼ばれる REST 承認エンドポイント"""
        try:
            body = await request.json()
            approved = await device.request_approval(
                tool    = body.get("tool", "unknown"),
                details = body.get("details", ""),
                danger  = bool(body.get("danger", False)),
                timeout = int(body.get("timeout", 30)),
            )
            return JSONResponse({"approved": approved})
        except Exception as e:
            return JSONResponse({"error": str(e)}, status_code=500)

    app = Starlette(routes=[
        Route("/sse",      endpoint=handle_sse),
        Route("/messages", endpoint=handle_messages, methods=["POST"]),
        Route("/request",  endpoint=handle_request,  methods=["POST"]),
    ])

    print(
        f"[boo] MCP server starting (SSE on 0.0.0.0:{port})...\n"
        f"[boo] SSE endpoint: http://0.0.0.0:{port}/sse",
        file=sys.stderr
    )
    config = uvicorn.Config(app, host="0.0.0.0", port=port, log_level="warning")
    uv_server = uvicorn.Server(config)
    await uv_server.serve()


# ============================================================
# エントリポイント
# ============================================================
async def main():
    parser = argparse.ArgumentParser(
        description="Boo デバイス MCP ブリッジ"
    )
    parser.add_argument(
        "--port", "-p",
        help="シリアルポート (省略時は自動検出)",
        default=None
    )
    parser.add_argument(
        "--baud", "-b",
        type=int, default=115200,
        help="ボーレート (デフォルト: 115200)"
    )
    parser.add_argument(
        "--mock",
        action="store_true",
        help="デバイスなしでモック動作 (テスト用)"
    )
    parser.add_argument(
        "--transport",
        choices=["stdio", "sse"],
        default="stdio",
        help="MCP トランスポート (stdio | sse, デフォルト: stdio)"
    )
    parser.add_argument(
        "--http-port",
        type=int, default=8765,
        help="SSE トランスポートのリッスンポート (デフォルト: 8765)"
    )
    args = parser.parse_args()

    if args.mock:
        print("[boo] Running in MOCK mode (no device)", file=sys.stderr)
        # モックデバイス
        class MockDevice:
            async def request_approval(self, tool, details="",
                                       danger=False, timeout=30):
                print(f"[mock] approve? tool={tool} details={details}",
                      file=sys.stderr)
                await asyncio.sleep(2)
                return True
            def notify_working(self, tool_name=""): pass
            def notify_idle(self): pass
            def update_tokens(self, total, today): pass
        device = MockDevice()
    else:
        port = args.port or auto_detect_port()
        if not port:
            print(
                "[boo] ERROR: デバイスが見つかりません。\n"
                "  --port /dev/cu.usbserial-XXXX で指定してください。",
                file=sys.stderr
            )
            sys.exit(1)

        device = BooDevice(port, args.baud)
        if not device.connect():
            sys.exit(1)

    server = build_mcp_server(device)

    if args.transport == "sse":
        await _run_sse(server, device, args.http_port)
    else:
        print("[boo] MCP server starting (stdio)...", file=sys.stderr)
        async with stdio_server() as (read_stream, write_stream):
            await server.run(
                read_stream, write_stream,
                server.create_initialization_options()
            )


if __name__ == "__main__":
    asyncio.run(main())
