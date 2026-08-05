#!/usr/bin/env python3
"""Validation bout-en-bout du serveur MCP d'ecu_studio (stdio)."""
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

BIN = Path(__file__).resolve().parents[1] / "build_debug" / "apps" / "ecu-studio" / "ecu_studio"


def rpc(proc: subprocess.Popen, req_id: int, method: str, params=None) -> dict:
    msg = {"jsonrpc": "2.0", "id": req_id, "method": method}
    if params is not None:
        msg["params"] = params
    line = json.dumps(msg, ensure_ascii=False) + "\n"
    assert proc.stdin is not None and proc.stdout is not None
    proc.stdin.write(line)
    proc.stdin.flush()
    raw = proc.stdout.readline()
    if not raw:
        raise RuntimeError(f"EOF sur stdout après {method}")
    return json.loads(raw)


def main() -> int:
    if not BIN.exists():
        print(f"FAIL: binaire introuvable : {BIN}", file=sys.stderr)
        return 1

    proc = subprocess.Popen(
        [str(BIN), "--mcp"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )
    try:
        init = rpc(proc, 1, "initialize", {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {"name": "validate-mcp", "version": "1"},
        })
        assert init["result"]["protocolVersion"] == "2024-11-05", init
        assert init["result"]["serverInfo"]["name"] == "ecu-studio", init
        print("OK  initialize")

        listed = rpc(proc, 2, "tools/list", {})
        tools = listed["result"]["tools"]
        names = {t["name"] for t in tools}
        print(f"OK  tools/list → {len(tools)} outils")

        if "read_dtc" not in names:
            print("FAIL: read_dtc absent", file=sys.stderr)
            return 1
        print("OK  read_dtc présent")

        if "clear_dtc" in names:
            print("FAIL: clear_dtc ne doit PAS être exposé", file=sys.stderr)
            return 1
        print("OK  clear_dtc absent (contrat sûreté)")

        read_dtc = next(t for t in tools if t["name"] == "read_dtc")
        props = read_dtc["inputSchema"].get("properties", {})
        for key in ("port", "baud", "pending"):
            assert key in props, f"schema manquant: {key}"
        print("OK  schema read_dtc {port,baud,pending}")

        # Appel sans matériel : doit échouer proprement (isError), pas crasher.
        call = rpc(proc, 3, "tools/call", {
            "name": "read_dtc",
            "arguments": {"port": "/dev/null_does_not_exist_obd"},
        })
        assert "result" in call, call
        result = call["result"]
        assert result.get("isError") is True, call
        text = ""
        for block in result.get("content", []):
            if block.get("type") == "text":
                text += block.get("text", "")
        assert text, call
        print(f"OK  read_dtc sans matériel → erreur propre : {text[:120]!r}")

        # Outil ROM déterministe toujours dispo.
        recipes = rpc(proc, 4, "tools/call", {
            "name": "list_recipes",
            "arguments": {},
        })
        assert recipes["result"].get("isError") is not True, recipes
        print("OK  list_recipes round-trip")

        print("PASS validate_mcp")
        return 0
    finally:
        proc.stdin.close()
        proc.kill()
        proc.wait(timeout=5)


if __name__ == "__main__":
    sys.exit(main())
