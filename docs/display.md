# LED Matrix Display

## Initial Hardware

The first supported display is the [BTF-LIGHTING WS2812B ECO 8x32 matrix](https://www.amazon.com/dp/B09XWR1Y5K) with these product properties:

- Grid: 8 vertical pixels by 32 horizontal pixels, 256 pixels total.
- Physical size: approximately 32 cm by 8 cm by 0.2 cm.
- LED package: 5050SMD, individually addressable WS2812B.
- Electrical connection: 3-pin JST connection with a single serial data strand; panels are chainable.
- Supply: DC 5 V only.
- Pixel protocol: WS2812B, 24-bit color, GRB byte order.
- Initial layout: flexible FPCB with serpentine or zig-zag wiring.
- Environmental rating: IP30, non-waterproof.
- Rated operating temperature: -20 C to +50 C.
- Package contents: two panels; this project initially targets one panel.
- Intended logical origin: top-left pixel.
- Intended logical endpoint: top-right pixel.

The exact panel orientation and strand route must be confirmed against the assembled hardware before the mapping is finalized. A conventional row-major serpentine route across eight rows ends at the bottom-left pixel, not the top-right pixel. The firmware must therefore make orientation, axis direction, and serpentine mapping configurable rather than assuming that the physical endpoint describes a standard row-major map.

## Configurable Mapping

The display configuration should describe the panel without changing rendering code:

- power-source mode and its enforced brightness/current limit;
- gamma-correction profile.

The mapper should convert a logical `(x, y)` coordinate to a bounded strand index. Mapping should be deterministic and testable on the host with a small table of known coordinates, including all four corners and the configured strand endpoints.

For the initial 32-column, 8-row row-major serpentine candidate, the index rule is:

```text
row = y
base = row * width
index = base + x                    when row is even
index = base + (width - 1 - x)      when row is odd
```

This candidate mapping is documentation only until the physical strip route is verified. The expected index for each corner must be recorded in the hardware test notes.

## Data and Refresh

A full RGB frame for the initial array requires:

```text
8 * 32 * 3 = 768 bytes
```

A second frame buffer would require another 768 bytes. Buffer count should be selected deliberately because WS2812B updates are serialized and the display must not be modified while a frame is being transmitted.

The WS2812B protocol is timing-sensitive. The implementation should use a Pico hardware peripheral such as PIO, with DMA considered if it reduces CPU work without adding unnecessary complexity. The main application should prepare a frame and request a transfer; it should not bit-bang the strand from the timekeeping or network path.

Render work should happen only when visible content changes. For a clock display, this normally means updating on a displayed-second or displayed-minute boundary according to the chosen visual design, then keeping the refresh engine idle between changes.

## Initial Clock Presentation

The first display experience should remain intentionally simple:

- Show the user's configured local time in 24-hour `HH:MM` format.
- Use a large, readable fixed-width font sized for the 8-pixel display height.
- Assign a smoothly changing rainbow hue across adjacent characters so each character is visually distinct without changing the text layout.
- Display the current date as a separate marquee message, using the compact ISO form `YYYY-MM-DD` unless the regional settings later require another format.
- Scroll the date at a bounded, configurable interval and pause briefly after the complete date has entered the display.
- Keep time visible and stable while the date scrolls; the marquee must not replace or delay timekeeping.
- Update the time at the display boundary selected by the UI, initially once per minute for `HH:MM`.

Rainbow coloring is a presentation effect, not a reason to refresh the entire matrix continuously. Precompute or update only the changed glyph rows and use the existing frame-transfer path. The color calculation should use integer or lookup-table arithmetic and must pass through the configured gamma and power limits before transmission.

Until NTP has provided a valid clock, the display should show an explicit unsynchronized state rather than presenting an arbitrary local time. During Wi-Fi or NTP recovery, retain the last valid time with an appropriate status indication.

## Network Wait Animation

While the device is waiting for its first usable network connection, show a full-array rainbow gradient as the wait animation:

- The gradient spans the complete 32x8 array rather than coloring only a text row.
- Hue should vary smoothly across the logical display coordinates.
- The gradient should flow continuously at a bounded animation rate, with no blocking delays in the network or display control paths.
- Use the configured gamma correction, power-source brightness limit, and current budget for every animation frame.
- Prefer a compact phase accumulator or lookup table over floating-point color calculations in the steady-state loop.
- Reduce or pause animation work while the radio is actively scanning or transmitting if measurements show a meaningful power benefit.
- Stop the wait animation when a usable connection is established or when the device enters a defined error/recovery state.

The animation is status feedback, not a reason to run the LEDs at full white. Its default brightness must remain within the active power-source limit.

## First-Connection IP Marquee

After the device obtains its first usable station connection, marquee the assigned local IP address for 30 seconds so the user can access the local settings page. The message should include a readable label and address, for example `IP 192.168.1.42`.

- Start the 30-second timer only after the station has an assigned, usable address.
- Keep the message visible long enough for a person to read it and scroll at a bounded speed.
- Do not include credentials or other sensitive configuration in the marquee.
- If the address changes during the interval, replace the message and restart the interval.
- After 30 seconds, return to the normal clock/date presentation.
- The local settings page must remain available after the marquee ends.

## Power and Electrical Requirements

The product listing specifies approximately 0.1 W to 0.3 W per pixel and 25 W to 75 W for a full 256-pixel panel, depending on the displayed colors. The 75 W maximum corresponds to approximately 15 A at 5 V. This is a design limit, not a target operating point; the firmware must impose a brightness limit and the assembled hardware must be measured.

Before connecting the array to a production supply, document:

- regulated 5 V supply voltage and current rating;
- current injection points and wire gauge;
- common ground between the Pico and LED supply;
- data-line level shifting requirements for the selected supply voltage;
- bulk and local decoupling;
- fuse or current limiting;
- maximum firmware brightness.

The listing recommends a 5 V 10 A or 5 V 20 A supply. The Pico must not power the matrix from a GPIO pin or from the USB 5 V path. Brightness limiting is a safety and battery-life control, not merely a visual preference. The product listing also warns against lighting all pixels white because of heat and wire current.

The panel does not include a power supply or controller. Power must be injected through the panel's power connections and sized independently from the Pico's logic supply. Use a common ground and verify the data voltage and level-shifting approach on the actual panel before sustained operation.

## Power-Source Brightness Policy

Brightness must be constrained by the selected power source, with the limit enforced by firmware rather than left to the user interface:

- **Pico USB power**: maximum configured brightness is 50 percent, and a separate absolute current limit must prevent the panel from exceeding the verified USB and board power budget.
- **External 5 V power**: brightness may be configured up to 100 percent only when the external supply, wiring, injection points, and thermal behavior have been verified for the selected limit.
- **Unknown or invalid power source**: use the USB-safe limit and do not allow the unrestricted setting.

A 50 percent brightness setting is not a current guarantee. With linear scaling, the panel's listed 75 W full-white worst case would still be approximately 7.5 A at 50 percent. That is far beyond a typical laptop USB port and may exceed the Pico board's power path. The implementation must use a measured or conservatively calculated current budget, and it must fail closed when the power source cannot be identified.

The configuration should expose the intended power mode and user brightness limit, but it must clamp the effective value to the hardware-enforced limit. Power-source detection and the current budget must be documented for the actual Pico carrier, USB path, external supply, and wiring. The LED matrix must not be powered through a Pico GPIO pin.

## Gamma Correction

Apply gamma correction before converting the requested color to the WS2812B output value. A lookup table is preferred over floating-point calculations in the display path; an initial 8-bit table can map each input channel value to an 8-bit corrected value.

The correction profile must be configurable or replaceable, with the initial profile selected through hardware testing. Gamma correction is intended to improve perceived color and brightness at low output levels; it does not increase the allowed electrical power.

The final pipeline should apply the power-source brightness/current clamp after gamma correction, or use a combined lookup table whose output is already bounded. This prevents gamma expansion from bypassing the USB or external-supply limit. Tests should verify black, primary colors, white, mid-level gray, and the maximum output for each power mode.

## Hardware Verification

The initial display bring-up should use a diagnostic pattern before the clock application:

1. Light one pixel at a time at low brightness.
2. Verify the four logical corners.
3. Verify each row transition and the direction of every zig-zag.
4. Verify red, green, and blue channel order.
5. Confirm no visible corruption while the Pico performs Wi-Fi work.
6. Measure idle, typical, and worst-case current.

Record the verified mapping and electrical limits in this document before treating the layout as the default configuration.
