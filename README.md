# StrumLink

Ultra low-latency wireless guitar controller for rhythm games (YARG, Clone Hero, and any game supporting HID gamepads).

## Features

- **~2ms total latency** — BLE LLPM (1ms connection interval) + 2M PHY + USB HID 1000Hz
- **Santroller-compatible** — auto-detected by YARG/PlasticBand as Guitar Hero Guitar, no manual configuration needed
- **Auto-reconnect** — handles power cycles on either side gracefully
- **LED indication** — blinking = searching, solid = connected
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

- **TX** (guitar) — reads buttons via GPIO interrupts, sends over BLE
- **RX** (USB dongle) — receives BLE notifications, outputs USB HID gamepad

### Wiring (TX)

| Function | GPIO | Board pin |
|---|---|---|
| Fret Green | P0.06 | D0 |
| Fret Red | P0.08 | D1 |
| Fret Yellow | P0.17 | D2 |
| Fret Blue | P0.20 | D3 |
| Fret Orange | P0.22 | D4 |
| Strum Up | P0.24 | D5 |
| Strum Down | P0.13 | D6 |

Buttons connect pin to GND (internal pull-up enabled). No external components needed.

## Building

### Prerequisites

- Docker (or OrbStack on macOS)

### Setup (one time)

```bash
./build.sh setup    # builds Docker image with nRF Connect SDK (~20 min)
```

### Build firmware

```bash
./build.sh tx       # build TX (guitar) firmware
./build.sh rx       # build RX (USB dongle) firmware
./build.sh all      # build both
```

### Flash

1. Enter bootloader: short RST to GND twice quickly
2. Copy UF2 file to the NICENANO USB drive:

```bash
cp build/tx/zephyr/zephyr.uf2 /Volumes/NICENANO/    # TX
cp build/rx/zephyr/zephyr.uf2 /Volumes/NICENANO/    # RX
```

## Tech stack

- **nRF Connect SDK v3.2.4** (Zephyr RTOS)
- **SoftDevice Controller** with LLPM (Low Latency Packet Mode)
- **BLE 2M PHY** — double the throughput, required for LLPM
- **USB HID** (Zephyr USB device next stack) — Santroller GH Guitar format
- Board target: `promicro_nrf52840/nrf52840/uf2`
- Build flag: `--no-sysbuild` (required for correct UF2 flash offset)

## Project structure

```
├── Dockerfile          # NCS SDK build environment
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
└── monitor.sh          # Serial log monitor
```

## License

MIT
