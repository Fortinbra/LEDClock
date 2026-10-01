# LEDClock

LEDClock is an embedded C++ application for Raspberry Pi Pico 2 W that will obtain network time over Wi-Fi using NTP and render the current time on an LED matrix.

The project is intentionally small and hardware-oriented. Processing, memory, network traffic, and LED power consumption are treated as design constraints from the beginning.

## Current Status

The project currently contains the Pico SDK application skeleton and the first captive-portal UI slice. The setup access point, captive DNS responder, HTTP page, and theme controls are implemented; radio scanning, credential persistence, station connection, NTP, LED matrix driving, time-zone handling, and low-power behavior remain planned work.

The configured board is `pico2_w`, and the project uses C++17 with the Raspberry Pi Pico SDK.

## Project Layout

```text
.
├── CMakeLists.txt          # Pico SDK build configuration
├── pico_sdk_import.cmake   # Pico SDK CMake integration
├── src/
│   └── main.cpp            # Application entry point
├── include/                # Project headers as they are introduced
├── docs/
│   ├── architecture.md     # System design and engineering constraints
│   ├── display.md           # WS2812B matrix layout and power requirements
│   └── provisioning.md      # Client Wi-Fi and regional setup flow
└── .vscode/                # Local Pico/VS Code tooling configuration
```

Generated files belong in `build/` and must not be committed.

## Build

From the project root, configure the project if needed and build it with the existing VS Code task:

1. Run **Compile Project**.
2. Confirm that the task completes without errors or warnings.
3. Firmware outputs are generated in `build/`.

The equivalent command on Windows is:

```powershell
& "$env:USERPROFILE/.pico-sdk/ninja/v1.13.2/ninja.exe" -C build
```

The configured SDK, toolchain, board, and build settings are defined in `CMakeLists.txt` and the `.vscode/` project configuration.

## Flashing and Running

Use the existing VS Code tasks for the connected hardware:

- **Run Project**: load the generated program with `picotool`.
- **Flash**: program and verify through the CMSIS-DAP debugger.
- **Rescue Reset**: halt/reset a target that is not responding normally.

The initial display is an 8x32 WS2812B RGB array on a single serpentine strand. The configurable mapping, endpoint verification, timing, and power requirements are documented in [docs/display.md](docs/display.md). The physical strand route and top-right endpoint still need to be verified against the assembled array.

## Quality Requirements

Every change should preserve these requirements:

- The project builds cleanly with no compiler errors or warnings.
- Warnings are treated as defects, especially in timing, integer conversion, buffer, and lifetime code.
- Allocations in the steady-state loop are avoided.
- Blocking work is kept out of display refresh and timekeeping paths.
- Network retries use bounded delays and do not busy-loop.
- Time synchronization is periodic rather than continuous.
- LED brightness is bounded to control current draw and heat.
- Logging is useful during development and removable or disabled for production builds.
- Hardware behavior is testable through small, deterministic components where practical.

Performance decisions should be supported by measurements such as loop timing, free heap, network retry behavior, and measured current draw rather than assumptions.

## Design Direction

The intended runtime flow is:

1. Initialize the Pico SDK, clocks, GPIO, Wi-Fi, and display hardware.
2. Connect to the configured Wi-Fi network with a bounded retry strategy.
3. Synchronize the system clock with an NTP server over UDP.
4. Maintain time locally between synchronizations using the RP2350 time facilities.
5. Render only when the displayed minute or visible state changes.
6. Reconnect and resynchronize after connectivity or clock validity failures.

The initial visual output is a 24-hour local-time display with rainbow-colored characters and a marquee for the current date. See [docs/display.md](docs/display.md) for the presentation and refresh requirements.

See [docs/architecture.md](docs/architecture.md) for the current design constraints and decisions still to be made.

The planned client setup flow is documented in [docs/provisioning.md](docs/provisioning.md). It covers the first-boot captive portal, Wi-Fi scanning and rescan behavior, regional settings, connection confirmation, and local-network settings access.

## Contributions

Keep changes focused and build the project after each change. Update the relevant documentation when hardware, timing, power, or networking behavior changes.
