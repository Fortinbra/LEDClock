# LEDClock Architecture

## Purpose

LEDClock displays network-synchronized time on an LED matrix using a Raspberry Pi Pico 2 W. The firmware must remain responsive, predictable, and economical with both CPU time and electrical power.

## Runtime Components

### Application state

The application should use explicit states rather than letting network or display code control the whole main loop:

- `Initializing`: configure SDK services and hardware.
- `Provisioning`: expose the setup access point and captive portal when configuration is missing or unusable.
- `Connecting`: establish Wi-Fi with bounded retries.
- `Synchronizing`: request and validate NTP time.
- `Running`: maintain local time and update the display as needed.
- `Recovering`: back off, reconnect, and resynchronize after failure.

State transitions should be observable in development logs and should not require dynamic allocation.

### Wi-Fi and NTP

- Use the Pico W Wi-Fi stack supplied by the Pico SDK.
- Use UDP NTP requests to port 123.
- Validate the response format, transmit/origin relationship, stratum, and timestamp range before accepting time.
- Apply a finite connection and request timeout.
- Use exponential or stepped backoff for repeated failures.
- Synchronize periodically, with the interval chosen to balance clock drift and radio energy use.
- Do not perform DNS, Wi-Fi scans, or repeated NTP requests in the display refresh path.
- Credentials must be supplied through a local or deployment-specific mechanism and must not be committed.

When configuration is missing or unusable, the device should enter a provisioning mode with a temporary access point, captive DNS behavior, and a small HTTP server. The client workflow, form requirements, and local-network settings behavior are defined in [provisioning.md](provisioning.md). Provisioning must use bounded buffers and must not block display refresh or timekeeping work.

The initial implementation should use UTC internally. Local time-zone rules and daylight-saving behavior should be added only after the core UTC path is reliable.

The initial user-facing display is intentionally limited to a 24-hour local-time readout in `HH:MM` format with rainbow-colored characters and a separate date marquee. The presentation requirements and refresh constraints are defined in [display.md](display.md). Rendering must remain independent of Wi-Fi and NTP transport so a network retry cannot stall the display path.

### Timekeeping

NTP is a calibration source, not a per-frame clock source. After a successful synchronization, the firmware should use a monotonic local time source for display updates and retain the last known synchronization quality.

The implementation must define behavior for:

- startup before the first valid synchronization;
- a missed or malformed response;
- Wi-Fi loss after synchronization;
- large forward or backward corrections;
- rollover and timestamp range limits.

Time corrections should avoid visibly unstable output. A small correction may be applied gradually; a clearly invalid or stale clock should be represented explicitly.

### LED matrix

The initial display is an 8x32 WS2812B RGB array on a single serpentine strand. Before implementing the driver, document:

- matrix type and scan protocol;
- controller or shift-register requirements;
- GPIO assignments;
- refresh rate and bit depth;
- maximum current and supply voltage;
- whether refresh requires a timer, PIO, DMA, or another dedicated mechanism.

The display mapping, physical endpoint verification, WS2812B timing, and current budget are documented in [display.md](display.md). The renderer must consume a configurable mapping instead of embedding the first panel's wiring assumptions.

The application should render into a fixed-size frame buffer and update that buffer only when visible content changes. Continuous full-frame work should be avoided when the displayed minute is unchanged.

Brightness should be a configurable limit, not an unrestricted maximum. Peak and average current must be measured with the chosen matrix and power supply.

## Resource and Power Rules

- Prefer static storage with fixed capacities over heap allocation after startup.
- Keep buffers sized from compile-time constants.
- Avoid busy waits; use SDK sleep, timers, callbacks, or hardware peripherals where appropriate.
- Keep radio activity bounded and avoid unnecessary reconnects.
- Stop or reduce work when the display content is unchanged.
- Avoid logging in high-frequency refresh code.
- Measure CPU utilization, stack margin, heap usage, and current draw on hardware.

Any optimization should include the behavior being measured, the test conditions, and the reason the change matters.

## Quality Gates

A change is ready for hardware testing when:

1. The configured Pico build completes with no errors or warnings.
2. New code has clear ownership of buffers, timers, sockets, and hardware resources.
3. Timeouts and failure paths are bounded and recoverable.
4. Integer widths and conversions are intentional for wire formats and hardware registers.
5. The normal path avoids repeated allocation and unnecessary work.
6. The relevant behavior is covered by a deterministic test or a documented hardware test procedure.
7. Documentation reflects any new pins, protocol assumptions, power limits, or recovery behavior.

## Open Decisions

- LED matrix model and driver interface.
- Verified 8x32 strand orientation and corner mapping.
- WS2812B data pin, level shifting, power injection, and brightness limit.
- Display dimensions and font strategy.
- Wi-Fi credential provisioning.
- Captive portal AP naming, security, and recovery trigger.
- Configuration storage and power-loss protection.
- NTP server selection and synchronization interval.
- UTC-only display versus local time-zone support.
- Brightness control and user configuration.
- Production logging policy.
