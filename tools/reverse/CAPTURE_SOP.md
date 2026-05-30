# MPPS Protocol Capture — Standard Operating Procedure (for the USER, on Windows)

This is the hands-on procedure to capture the real MPPS PC↔dongle protocol on a
Windows machine that has the **MPPS dongle + a car/ECU** (or at least a bench
ECU). The capture fills the remaining `TODO_REVERSE` items in
`libs/mpps/` (frame structure, STX, command codes, wire checksum, baud).

> Why this is needed: `Mpps.exe` is a **packed** C++Builder binary — the protocol
> bytes only exist in memory after it unpacks itself at runtime, so they cannot be
> recovered by static analysis. The checksum side (`ecu::ChecksumEngine`) is
> already reverse-engineered and does NOT need a capture; only the **wire
> protocol** does. See `docs/mpps-reverse.md`.

---

## 0. First: which dongle do you have? (decides the method)

Open **Device Manager** with the MPPS dongle plugged in and read its USB IDs
(right-click → Properties → Details → "Hardware Ids"):

* **`USB\VID_1C43&PID_0500`** → genuine **AMT MPPS V21** (kernel driver
  `AmtFlash.sys`). This is **NOT** an FTDI/serial device, so the `ftd2xx.dll`
  proxy below will see nothing. Use **Method B (USBPcap)**.
* **`USB\VID_0403&PID_6001/6010/6015`** → FTDI-based MPPS (clone/older). Use
  **Method A (ftd2xx proxy DLL)** — cleanest, gives decoded frames.

If unsure, try Method A first; if `mpps_protocol_capture.log` stays empty after a
read, the dongle is the AMT type → switch to Method B.

---

## Method A — ftd2xx proxy DLL (FTDI-based MPPS)

### A.1 Build the proxy
The proxy forwards all 85 ftd2xx exports to the real DLL and logs the 14
data/config calls. It is **32-bit** (Mpps.exe is i386).

* **On Linux** (this repo, confirmed working — produces a PE32 i386 DLL with 71
  forwarders + 14 hooks):
  ```bash
  cd tools/reverse
  ./build_proxy.sh            # uses i686-w64-mingw32-gcc + ftd2xx_proxy.def
  ```
  Requires `gcc-mingw-w64-i686` (`sudo apt install gcc-mingw-w64-i686`).
  Output: `tools/reverse/ftd2xx.dll`.

* **On Windows with MSVC** (x86 "Developer Command Prompt"):
  ```bat
  cl /LD /Fe:ftd2xx.dll ftd2xx_proxy.c ftd2xx_proxy.def
  ```

### A.2 Install next to Mpps.exe
In the MPPS install folder (where `Mpps.exe` lives):
1. **Rename** the existing `ftd2xx.dll` → **`ftd2xx_real.dll`**.
   (If MPPS doesn't ship a local `ftd2xx.dll`, copy
   `Device Driver/FTDI/i386/ftd2xx.dll` there first, then rename it.)
2. **Copy** the proxy `ftd2xx.dll` (from A.1) into the same folder.

The folder must now contain: `Mpps.exe`, `ftd2xx.dll` (proxy),
`ftd2xx_real.dll` (original).

### A.3 Run the capture operations
Launch `Mpps.exe`. Perform this exact sequence (so the log is easy to parse):
1. **Init / connect** the dongle (let it identify the interface).
2. **Identify ECU** (select the car/ECU; let MPPS read the ECU ID).
3. **Read ROM** — read the full flash (or a small block if the UI allows).
4. **Write 1 byte** — write back a single unchanged byte / a tiny region
   (use a *read → write the same data back* so the ECU is not altered).
5. **Restore / disconnect**.

Each step's bytes are appended to `mpps_protocol_capture.log` (created in the
working directory — usually the MPPS folder).

### A.4 Send back
Send `mpps_protocol_capture.log` back to the dev (it is gitignored; do not
commit it). Then run the parser (next section).

---

## Method B — USBPcap + Wireshark (AMT VID_1C43 dongle)

The AMT device speaks raw USB bulk through `AmtFlash.sys`; there is no DLL to
proxy. Capture at the USB layer instead:

1. Install **Wireshark** with the **USBPcap** option.
2. Plug in the dongle, start Wireshark, choose the **USBPcap** interface that the
   dongle enumerated on.
3. Run the *same* A.3 operation sequence in `Mpps.exe`.
4. Stop the capture, **File → Export Packet Dissections → As Plain Text**, OR
   save the `.pcapng`.
5. Filter to the dongle's bus/device address; the `URB_BULK out`/`in` payloads
   are the PC↔dongle frames. Paste the hex of a few representative TX/RX URBs
   into a text file in the same `TX --> (N bytes): ..` / `<-- RX ..` format the
   proxy uses, so `parse_capture.py` can read it — or send the `.pcapng` and the
   dev will extract it.

---

## 2. Turning the log into the protocol — `parse_capture.py`

```bash
python3 tools/reverse/parse_capture.py mpps_protocol_capture.log
```

What it does (see the script):
* Parses every `[hh:mm:ss.mmm] TX/RX (N bytes): <hex>` line into frames.
* Reports total TX/RX counts and the **first command** (the init/handshake).
* Groups frames by candidate CMD byte and prints example payloads → reveals the
  command table.
* Lists the distinct **STX** byte(s) actually seen on TX (so we replace the
  `0x68` hypothesis with the real value).
* **Validates the wire checksum hypothesis**: it tests XOR-of-all-bytes and, if
  that fails, tells you to try ADD8 / CRC8 / CRC16. (The ECU-side *flash*
  checksum is CRC-16/ARC — see `ChecksumEngine` — but the PC↔dongle *wire*
  checksum is independent and must be confirmed from the capture.)

From its output the dev fills the real values into
`libs/mpps/include/mpps/MppsDevice.hpp` (`protocol` namespace) and the
`sendCommand`/`configureFTDI` bodies in `libs/mpps/src/MppsDevice_libusb.cpp`,
replacing the HYPOTHESIS placeholders.

### What to look for in the proxy log (key answers)
* `[FT_SetBaudRate] <N>` → the **real baud** to the dongle (replaces the 38400
  guess in `configureFTDI`).
* `[FT_SetLatencyTimer] <ms>` → confirms latency (expected 10 from `Mpps.Cfg`).
* `[FT_SetBitMode] mask=.. mode=..` → if present, the dongle uses bit-bang
  (e.g. K-line 5-baud slow-init); note the mode.
* First `TX -->` after init → the **handshake** frame: its first byte is the real
  STX; its structure reveals LEN field width/endianness and the checksum.
* The `Read ROM` frames → the **read command** + how address/length are encoded
  (this tells us `MAX_BLOCK_SIZE` and the address byte order).
