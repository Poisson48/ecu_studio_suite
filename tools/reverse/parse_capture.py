#!/usr/bin/env python3
"""
parse_capture.py — Analyse mpps_protocol_capture.log
Reconstruit le protocole MPPS depuis les trames capturées
Usage : python3 parse_capture.py mpps_protocol_capture.log
"""
import sys
import re
from dataclasses import dataclass, field
from typing import Optional

@dataclass
class Frame:
    timestamp: str
    direction: str   # 'TX' ou 'RX'
    raw: bytes
    stx: Optional[int] = None
    length: Optional[int] = None
    cmd: Optional[int] = None
    payload: Optional[bytes] = None
    checksum: Optional[int] = None
    valid_checksum: bool = False

def parse_log(log_path: str) -> list:
    frames = []
    with open(log_path) as f:
        content = f.read()

    pattern = r'\[(\d{2}:\d{2}:\d{2}\.\d{3})\] (TX →|← RX) \((\d+) bytes\): ([0-9A-F ]+)'
    for m in re.finditer(pattern, content):
        ts, direction, length, hex_str = m.groups()
        raw = bytes(int(h, 16) for h in hex_str.strip().split() if h)
        frame = Frame(timestamp=ts, direction='TX' if '→' in direction else 'RX', raw=raw)

        if len(raw) >= 3 and raw[0] == 0x68:
            frame.stx     = raw[0]
            frame.length  = raw[1]
            frame.cmd     = raw[2]
            frame.payload = raw[3:-1] if len(raw) > 3 else b''
            frame.checksum = raw[-1]
            xor = 0
            for b in raw[:-1]: xor ^= b
            frame.valid_checksum = (xor == frame.checksum)

        frames.append(frame)
    return frames

def analyze(frames: list):
    print(f"Total trames : {len(frames)}")
    print(f"TX : {sum(1 for f in frames if f.direction == 'TX')}")
    print(f"RX : {sum(1 for f in frames if f.direction == 'RX')}")

    cmds = {}
    for f in frames:
        if f.direction == 'TX' and f.cmd is not None:
            if f.cmd not in cmds:
                cmds[f.cmd] = {'count': 0, 'payloads': []}
            cmds[f.cmd]['count'] += 1
            if f.payload:
                cmds[f.cmd]['payloads'].append(f.payload.hex())

    print("\n=== COMMANDES DÉTECTÉES ===")
    for cmd, info in sorted(cmds.items()):
        print(f"\nCMD 0x{cmd:02X} — {info['count']} occurrences")
        for p in list(set(info['payloads'][:5]))[:3]:
            print(f"  payload : {p}")

    stx_values = set(f.raw[0] for f in frames if f.direction == 'TX' and f.raw)
    print(f"\n=== STX observés (TX) : {[hex(v) for v in stx_values]} ===")

    tx_frames = [f for f in frames if f.direction == 'TX']
    valid = sum(1 for f in tx_frames if f.valid_checksum)
    print(f"Checksums XOR valides : {valid}/{len(tx_frames)}")
    if valid == 0:
        print("→ Hypothèse XOR INCORRECTE — tester ADD, CRC8, CRC16")
    else:
        print("→ Checksum XOR confirmé ✓")

    tx_cmds = [f.cmd for f in frames if f.direction == 'TX' and f.cmd]
    if tx_cmds:
        print(f"\n=== Séquence : {[hex(c) for c in tx_cmds[:20]]}...")
        print(f"Première commande (init) : CMD 0x{tx_cmds[0]:02X}")

if __name__ == '__main__':
    log = sys.argv[1] if len(sys.argv) > 1 else 'mpps_protocol_capture.log'
    try:
        frames = parse_log(log)
        analyze(frames)
    except FileNotFoundError:
        print(f"[ATTENTE] {log} pas encore disponible")
        print("Lancer d'abord l'exe MPPS avec le proxy DLL ftd2xx")
