# MPPS V21 — Reverse Engineering Master Report

> Goal: reconstruct the MPPS USB wire protocol + checksum logic so ECU Studio
> can talk to the MPPS hardware directly via libusb (Linux), replacing the
> Windows MPPS software.
>
> **FACT** = directly observed in the binaries/files.
> **HYPOTHESIS** = inferred, needs confirmation by a live USB capture.

All offsets are given as virtual addresses (VA, ImageBase 0x400000 for the
Check DLLs) or file RVAs where stated. Analysis tooling: `pefile`, `capstone`,
`strings`, `objdump`, a portable UPX 4.2.4 (for the Check DLLs).

---

## 1. Package inventory (FACT)

Extracted to `tools/reverse/mpps_extracted/` (gitignored, never committed):

| Item | Size | Notes |
|------|------|-------|
| `Mpps.exe` | 3.0 MB | Main flasher. **C++Builder (Embarcadero/Borland)** Win32, **packed**. |
| `MultiBoot.exe` | 2.65 MB | Boot-mode flasher, same toolchain, packed. |
| `Tricore Boot.exe` | 2.67 MB | Tricore boot-mode flasher, same toolchain, packed. |
| `Check/Check0NN.dll` | 61 files, ~26–33 KB | Per-ECU **checksum/validation** modules. **UPX-packed**, native C++Builder. HIGH VALUE — fully analysed (see `mpps-checksums.md`). |
| `Device Driver/AMT/` | — | **AmtFlash.sys** kernel driver, `USB\VID_1C43&PID_0500`. |
| `Device Driver/FTDI/` | — | FTDI D2XX driver + `ftd2xx.dll` (i386 + amd64) + `ftd2xx.h`. |
| `Database/DataBase.Ini` | 16.7 KB | **Encrypted** (high entropy, keyed by `*.Key`). ECU DB. |
| `Drives/A0NN.Drv` | 183 files | **Encrypted** ECU "drivers" (flash sequences/params), high entropy. |
| `DtcCodes/*.Ini` | 2 | DTC tables. |
| `Language/*.lang` | ~38 | UI translations. |
| `Mpps.Cfg`, `Tricore.Cfg` | — | Plain INI config (see below). |
| `*.Key` (`Mpps.key`, `MultiBoot.Key`, `41363130.Key`, …) | 312 B each | License/crypto keys; header magic `KEY\0`. |
| `Summary.Dat` | — | Last-session log (plain INI). |

### Version info (FACT — VS_VERSION_INFO of Mpps.exe)
```
CompanyName     = Amt-Cartech Ltd
FileDescription = Mpps®
FileVersion     = 18.12.3.8
ProductVersion  = 18.0.0.0
Comments        = Usb Flashing Tool. Mpps® is a registerd trade mark of AMT-Cartech Ltd
Manifest <description> = Amt Flash Tool
```
So the "V21" marketing name corresponds to internal **file version 18.x**, vendor
**Amt-Cartech Ltd**. The hardware is the **AMT flash interface**.

### Config files (FACT)
`Mpps.Cfg`:
```
[Latency] Value=10          ; FTDI latency-timer value (ms) — classic FT_SetLatencyTimer
[Last Car] Make=2 Model=2 Ecu=25
```
`Tricore.Cfg`:
```
[Config] Latency=10 Speed=0 EcuType=0 AutoBaud=1 AutoSel=1
```
`Summary.Dat` (real session): `Auto=Series 5`, `EcuType=EDC16C31`,
`Action=Read/Write`, `Hardware No'=1037390905` → confirms a BMW EDC16C31 read+write.

---

## 2. Hardware / transport (FACT + HYPOTHESIS)

### 2.1 Two device families bundled (FACT)
1. **AMT native USB device** — `Device Driver/AMT/AmtFlash.inf`:
   ```
   USB\VID_1C43&PID_0500   (Amt-Cartech Ltd)
   ServiceBinary = AmtFlash.sys   ; kernel-mode WDM driver
   ```
   This is the **genuine MPPS V21** dongle. It is **NOT a CDC/serial device** —
   it is a custom WinUSB-style bulk device driven by `AmtFlash.sys`.
2. **FTDI D2XX device** — `Device Driver/FTDI/`, standard FTDI VID `0x0403`.
   Used by older / clone MPPS interfaces (FT232-based). `ftd2xx.dll` (i386, 85
   exports) is the userland API.

> **Implication for ECU Studio (IMPORTANT):** the `KNOWN_DEVICES` list currently
> in `MppsDevice_libusb.cpp` only contains FTDI/CH340/CP2102 VIDs. The real V21
> dongle is **VID 0x1C43 / PID 0x0500**. This has been added (see §6).

### 2.2 How Mpps.exe selects the transport (FACT, partial)
Mpps.exe imports **only** kernel32/user32/advapi32/comctl32 (1 thunk each) →
**everything else is resolved dynamically** (`LoadLibrary`/`GetProcAddress`),
which is the normal C++Builder + packer pattern. The bundling of *both*
`AmtFlash.sys` and `ftd2xx.dll` means Mpps.exe probes for an AMT device first and
falls back to FTDI D2XX. The D2XX path is the one we can intercept cheaply with a
proxy DLL (see `tools/reverse/CAPTURE_SOP.md`); the AMT path needs a USBPcap/
Wireshark capture.

### 2.3 Packing blocks static recovery of the comm code (FACT)
`Mpps.exe` PE sections (pefile):
```
.didata  vsize 0x8b2000  rawsize 0x0      entropy 0     <- 8.9 MB virtual-only = unpack target
.adata   vsize 0x26000   rawsize 0x26000  entropy 5.65  <- leaked C++Builder RTTI/symbols
.rsrc    vsize 0x1d57c0                    entropy 7.99  <- compressed resources
.data    vsize 0x1a000                     entropy 8.00  <- packed code + unpacker stub (holds EP)
EntryPoint RVA 0xab3000  (inside .data)
```
The entry point disassembles to anti-disassembly junk (`jmp` into mid-instruction,
`popfd`, opaque predicates):
```
00eb3000  jmp 0xeb300a
00eb300b  add edi, [esi - 0x14afae34]   ; garbage / misaligned
...
```
A raw byte grep of all three exes for `ftd2xx.dll`, `FT_Open`, `FT_Write`,
`FT_SetBaudRate`, `FT_SetLatencyTimer`, `AmtFlash`, etc. returns **0 hits** — all
those literals live in the compressed `.didata`/`.data` and only appear after the
unpacker runs at load time. **Therefore the wire-frame builder cannot be recovered
by pure static analysis without first unpacking the running process.** No wine /
no execution is available in this environment, so the frame structure below is a
HYPOTHESIS to be confirmed by the live capture.

### 2.4 Leaked symbols that DID survive (FACT)
The `.adata` section leaks C++Builder RTTI (mangled `@...$qqr...` names). Most are
from the **"Jam" shell-control UI library** (TJamFileList, breadcrumb bar, etc.) —
not protocol. The protocol-relevant survivors are bare method names:
```
SetBaud          GetLatency        AskReadLen
SetLatency       GetSerialDelay    GetWriteFileLen
SetSerialDelay
```
plus ECU-name fragments `me71`, `Z-Me7`. These names match a **D2XX/serial comm
object**: `SetBaud` ↔ `FT_SetBaudRate`, `SetLatency` ↔ `FT_SetLatencyTimer`,
`SetSerialDelay`/`GetSerialDelay` ↔ inter-byte delay, `AskReadLen` ↔ expected RX
length, `GetWriteFileLen` ↔ flash write size. This is strong evidence the FTDI
path is a **raw serial / bit-banged byte stream**, not the CDC virtual COM port.

---

## 3. Protocol-relevant strings (FACT)

ASCII `strings -n 4` and UTF-16 `strings -el` were run on `Mpps.exe`.

* UTF-16 string count is tiny (82) — the unicode UI text is in the packed
  resources, not recoverable statically.
* No plaintext command tables, baud numbers, KWP/CAN keywords, or ECU lists are
  present in cleartext (all packed). The only ECU tokens that leaked are `me71`
  and `Z-Me7` (Bosch ME7 family) embedded in RTTI.
* `Summary.Dat` confirms a real **EDC16C31** read/write session.
* `Mpps.Cfg`/`Tricore.Cfg` confirm an **FTDI latency timer = 10 ms** and
  `AutoBaud=1` (the dongle auto-negotiates the K-line/CAN baud with the ECU).

> Conclusion: the cleartext string mining that the task method anticipated is
> **not productive here because the exe is packed**. The high-value, fully
> recoverable target is the **Check DLLs** (UPX, trivially unpacked) — see
> `mpps-checksums.md`, where a complete CRC-16/ARC algorithm was extracted.

---

## 4. ftd2xx API surface the dongle uses (FACT — from bundled dll + leaked symbols)

`Device Driver/FTDI/i386/ftd2xx.dll` exports 85 functions. The ones MPPS will use
(matching the leaked `Set*`/`Get*` symbol names) are:

```
FT_Open / FT_OpenEx / FT_ListDevices / FT_GetDeviceInfoList   ; enumerate + open
FT_SetBaudRate            ; <- "SetBaud"
FT_SetLatencyTimer        ; <- "SetLatency"  (Mpps.Cfg Latency=10)
FT_SetTimeouts
FT_SetUSBParameters       ; USB transfer in/out buffer sizes
FT_SetDataCharacteristics ; 8N1 etc.
FT_SetFlowControl
FT_Purge                  ; flush before each transaction
FT_Write / FT_Read        ; <- the frame bytes (TX/RX)
FT_GetStatus / FT_GetQueueStatus   ; <- "AskReadLen" polls RX queue
FT_SetBitMode             ; possibly bit-bang for K-line init / 5-baud wakeup
FT_Close
```

These are exactly the functions the capture proxy hooks. `FT_SetBitMode` is the
one to watch: if MPPS bit-bangs the K-line slow-init (5 baud address) it will
appear here.

---

## 5. Reconstructed frame hypothesis (HYPOTHESIS — confirm with capture)

Because the builder is packed, the frame layout below is the **working
hypothesis** already coded in `MppsDevice_libusb.cpp`. It must be validated/
replaced from `mpps_protocol_capture.log`. Treat every value as provisional.

```
TX frame (host -> dongle):
  [STX=0x68?] [LEN] [CMD] [PAYLOAD...] [CHK]
RX frame (dongle -> host):
  [STX/ACK]  [LEN] [STATUS] [DATA...] [CHK]
```
* `STX = 0x68` — **HYPOTHESIS only.** 0x68 is the KWP2000/ISO-14230 "format byte
  with length" start delimiter, which is a *plausible* default for a K-line tool,
  but there is **no binary evidence** for it in this packaged build. The proxy's
  `parse_capture.py` is written to auto-test 0x68 and report the real STX.
* `CHK` — currently modelled as XOR of all preceding bytes (KWP-style). **Also a
  hypothesis.** `parse_capture.py` validates XOR and, if it fails, instructs to
  try ADD8 / CRC8 / CRC16. (Note the ECU-side flash checksum is CRC-16/ARC — the
  *wire* checksum may differ and is independent.)
* Command codes (`CMD_*`) in `MppsDevice.hpp` are **placeholders** — no command
  table survived packing.

### What the dongle actually does (HYPOTHESIS, well-grounded)
The MPPS dongle is a protocol *bridge*: the PC speaks a simple proprietary
framing to the dongle over the FTDI/AMT bulk pipe; the dongle's firmware then
speaks the real ECU protocol (KWP2000-on-K-line, or UDS-on-CAN, or boot-mode BSL)
to the car. `AutoBaud=1` and the K-line vs CAN split (separate `MultiBoot.exe` /
`Tricore Boot.exe` for boot modes) support this. So the bytes we capture on the
FTDI pipe are the **PC↔dongle** protocol, which is what ECU Studio must
re-implement; the dongle handles the car side.

---

## 6. What was filled into the ECU Studio code (FACT — see git diff)

Evidence-backed only:

* `MppsDevice_libusb.cpp` → `KNOWN_DEVICES`: **added `{0x1C43, 0x0500}` (AMT
  MPPS V21)** and `chipTypeStr` returns `"AMT MPPS V21"` for it.
  Source: `Device Driver/AMT/AmtFlash.inf` (FACT).
* `MppsDevice.hpp` `protocol` namespace: each constant is annotated as
  HYPOTHESIS with its (lack of) source; the `STX`/`CMD_*`/checksum values are
  explicitly marked `TODO_REVERSE — no binary evidence, confirm from capture`.
* The wire-protocol `sendCommand`/`configureFTDI` bodies remain `TODO_REVERSE`
  because they cannot be confirmed without a live capture (the builder is packed).

The **checksum** side is solid and has been implemented for real — see
`mpps-checksums.md` and `libs/ecu-core/ecu/ChecksumEngine.*`.

---

## 7. Open items requiring the live capture (handoff to user)

1. **Confirm transport**: is the real V21 dongle the AMT bulk device (VID 1C43)
   or an FTDI clone? (USB descriptor / Device Manager will say.)
2. **Confirm framing**: STX, LEN field width/endianness, CMD table, wire
   checksum (XOR vs ADD vs CRC).
3. **Confirm baud + latency + bitmode** sequence at connect.
4. **Capture an Identify → Read 256 B → Write 1 B → restore** transaction.

Procedure and tooling: `tools/reverse/CAPTURE_SOP.md`.
