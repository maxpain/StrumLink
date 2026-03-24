# StrumLink

Ultra low-latency wireless guitar controller for rhythm games (YARG, Clone Hero, and any game supporting HID gamepads).

## Features

- **~2ms total latency** — BLE LLPM (1ms connection interval) + 2M PHY + USB HID 1000Hz
- **Santroller-compatible** — auto-detected by YARG/PlasticBand as Guitar Hero Guitar, no manual configuration needed
- **Auto-reconnect** — handles power cycles on either side gracefully
- **LED indication** — blinking = searching, solid = connected
- **Auto power-off** — System OFF after 5 min idle (~0.3µA), wakes on any button press
- **LiPo battery** — charges via USB on the TX board
- **Docker build** — no toolchain installation needed on your machine

## Latency comparison

| Controller | Connection | Latency |
|---|---|---|
| **StrumLink** | **Wireless (BLE LLPM)** | **~2ms** |
| Santroller (wired) | USB | 0.84ms |
| Santroller (Bluetooth) | BT Classic | 8ms |
| Xbox 360 Xplorer | USB | 7.5ms |
| Les Paul Wireless | RF dongle | 19ms |
| Rock Band Guitar | RF dongle | 30ms |

## Hardware

Two **ProMicro nRF52840** boards (or Nice!Nano v2 / SuperMini nRF52840 clones):

- **TX** (guitar) — reads buttons via GPIO interrupts, sends over BLE, battery powered
- **RX** (USB dongle) — receives BLE notifications, outputs USB HID gamepad

### Wiring (TX)

| Function | Board pin | GPIO |
|---|---|---|
| Fret Green | 017 | P0.17 |
| Fret Red | 020 | P0.20 |
| Fret Yellow | 022 | P0.22 |
| Fret Blue | 024 | P0.24 |
| Fret Orange | 100 | P1.00 |
| Strum Up | 106 | P1.06 |
| Strum Down | 104 | P1.04 |
| Tilt (mercury switch) | 006 | P0.06 |
| Start | 010 | P0.10 |
| Select | 111 | P1.11 |
| Guide | 009 | P0.09 |
| Battery + | B+ | — |
| Battery - | GND | — |

All buttons connect pin to GND (internal pull-up enabled). No external components needed.

Whammy bar is auto-active (oscillates in firmware for continuous star power in YARG).

### Santroller HID Report

The RX dongle presents as a Santroller Guitar Hero Guitar (VID `0x1209`, PID `0x2882`, bcdDevice `0x0300`):

| Byte | Content |
|---|---|
| 0 | Report ID (0x01) |
| 1 | Green, Red, Yellow, Blue, Orange, —, Select, Start |
| 2 | Guide, padding |
| 3 | Hat switch: Strum Up (0), Strum Down (4), Neutral (8) |
| 4 | Whammy (oscillates 0x80↔0xFF) |
| 5 | Slider (0x00) |
| 6 | Tilt (0x80 center, 0xFF tilted) |

## Building

### Prerequisites

- Docker (or OrbStack on macOS)
- If Docker build fails with pip timeout, set DNS in Docker daemon config (e.g., `~/.orbstack/config/docker.json` for OrbStack):
  ```json
  { "dns": ["8.8.8.8", "8.8.4.4"] }
  ```

### Setup (one time)

```bash
./build.sh setup    # builds Docker image with nRF Connect SDK (~20 min)
```

### Build firmware

```bash
./build.sh tx       # build TX (guitar) firmware
./build.sh rx       # build RX (USB dongle) firmware
./build.sh all      # build both
./build.sh shell    # open shell in build container
```

### Flash

1. Enter bootloader: short RST to GND twice quickly
2. Copy UF2 file to the NICENANO USB drive:

```bash
cp build/tx/zephyr/zephyr.uf2 /Volumes/NICENANO/    # TX
cp build/rx/zephyr/zephyr.uf2 /Volumes/NICENANO/    # RX
```

### Update bootloader

If you have an old Adafruit bootloader (< 0.10.0), update it first:

```bash
# Download latest bootloader for Nice!Nano
curl -LO https://github.com/adafruit/Adafruit_nRF52_Bootloader/releases/download/0.10.0/update-nice_nano_bootloader-0.10.0_nosd.uf2
# Enter bootloader (double-tap RST), then:
cp update-nice_nano_bootloader-0.10.0_nosd.uf2 /Volumes/NICENANO/
```

## Tech stack

- **nRF Connect SDK v3.2.4** (Zephyr RTOS)
- **SoftDevice Controller** with LLPM (Low Latency Packet Mode)
- **BLE 2M PHY** — required for LLPM 1ms connection interval
- **NCS bt_scan + bt_gatt_dm** — proper LLPM-aware scanning and GATT discovery
- **USB HID** (Zephyr USB device next stack) — composite CDC ACM + HID
- Board target: `promicro_nrf52840/nrf52840/uf2`
- Build flag: `--no-sysbuild` (required for correct UF2 flash offset)

## Known issues

- **macOS Apple Silicon** caps USB HID polling at 500Hz (2ms) even though the device requests 1000Hz (1ms). This is an Apple limitation.
- **Both boards must use bootloader 0.10.0+** — older versions may not work correctly.

## Project structure

```
├── Dockerfile          # NCS SDK + Zephyr SDK build environment
├── build.sh            # Build script (Docker-based)
├── tx/                 # TX firmware (guitar, BLE peripheral)
│   ├── CMakeLists.txt
│   ├── prj.conf
│   ├── boards/         # Board overlay (GPIO pin mapping)
│   └── src/main.c
├── rx/                 # RX firmware (dongle, BLE central + USB HID)
│   ├── CMakeLists.txt
│   ├── prj.conf
│   ├── boards/         # Board overlay (HID device node)
│   └── src/main.c
└── monitor.sh          # Serial log monitor (for debugging)
```

## License

MIT
