"""ZenoC MCP stdio test server: newline-delimited JSON-RPC 2.0 over stdin/stdout.

Tools:
- add: {"a": number, "b": number} -> "a + b = <sum>"
- echo: {"text": string} -> text
Handshakes with the MCP initialize method like a real server.
"""
import json
import sys


def main():
    initialized = False
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            request = json.loads(line)
        except ValueError:
            continue
        method = request.get("method", "")
        request_id = request.get("id")
        if method == "initialize":
            result = {
                "protocolVersion": "2024-11-05",
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "zeno-test-server", "version": "1.0.0"},
            }
        elif method == "notifications/initialized":
            initialized = True
            continue
        elif method == "tools/list":
            result = {
                "tools": [
                    {"name": "add", "description": "Add two numbers",
                     "inputSchema": {"type": "object",
                                     "properties": {"a": {"type": "number"},
                                                    "b": {"type": "number"}},
                                     "required": ["a", "b"]}},
                    {"name": "echo", "description": "Echo text back",
                     "inputSchema": {"type": "object",
                                     "properties": {"text": {"type": "string"}},
                                     "required": ["text"]}},
                ]
            }
        elif method == "tools/call":
            params = request.get("params", {})
            name = params.get("name")
            arguments = params.get("arguments", {})
            if name == "add":
                text = f"{arguments.get('a', 0)} + {arguments.get('b', 0)} = {arguments.get('a', 0) + arguments.get('b', 0)}"
            elif name == "echo":
                text = str(arguments.get("text", ""))
            else:
                emit({"jsonrpc": "2.0", "id": request_id,
                      "error": {"code": -32602, "message": f"unknown tool: {name}"}})
                continue
            result = {"content": [{"type": "text", "text": text}]}
        elif "error" in request:
            # Response to nothing we sent; ignore.
            continue
        else:
            emit({"jsonrpc": "2.0", "id": request_id,
                  "error": {"code": -32601, "message": f"unknown method: {method}"}})
            continue
        emit({"jsonrpc": "2.0", "id": request_id, "result": result})
    return 0


def emit(payload):
    sys.stdout.write(json.dumps(payload) + "\n")
    sys.stdout.flush()


if __name__ == "__main__":
    sys.exit(main())
