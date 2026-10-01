# Client Setup and Provisioning

## Purpose

On first startup, or whenever no usable Wi-Fi configuration is stored, LEDClock provides a temporary setup network. A client can connect to that network, complete the setup form, and then use the same web interface from the normal local network for future settings updates.

The current firmware slice starts the `LEDClock-Setup` access point and serves the responsive setup page with system, light, and dark appearance options. The scan endpoint and settings submission endpoint are present as UI integration points, but they deliberately report not implemented until radio scanning, credential storage, and station connection are added.

Weather is not part of the current product. The ZIP code is collected and stored now so a future weather feature can use it without changing the initial setup flow.

## First-Time Setup Flow

1. Power on the board.
2. The board starts a temporary Wi-Fi access point with a device-specific LEDClock network name.
3. The board starts a captive portal web service on that access point.
4. Connect a phone or computer to the LEDClock setup network.
5. The captive portal opens the setup page. If the operating system does not open it automatically, browse to the setup address shown by the device documentation.
6. Complete the Wi-Fi and regional settings form.
7. Submit the form. The board validates the input, attempts to join the selected network, and reports the result.
8. On success, the board leaves setup mode and joins the configured local network.
9. Open the device's local-network address to confirm the saved settings and access the settings page.

While the device is waiting for its first usable network connection, the LED array shows a flowing full-array rainbow gradient. Once the board has a usable local IP address for the first time, it displays that address as a marquee for 30 seconds. This gives the client a visible handoff from setup to normal network operation.

If the connection attempt fails, the board must return to the setup page with the entered values preserved where possible, explain the failure in plain language, and keep the setup network available for another attempt.

## Setup Form

### Wi-Fi network

- **SSID**: a dropdown populated from the latest Wi-Fi scan performed by the radio.
- **Rescan**: a clear action that starts a new scan and replaces the SSID options with the latest results.
- The interface should show a loading state during a scan and should not submit stale results as if they were current.
- If the desired network is hidden or not detected, provide a manual SSID entry path.
- The list should display network names and, where useful, signal strength. Security type may be shown to help the user choose the correct network.

A scan is a radio operation and may take several seconds. The page should state that scanning temporarily interrupts other Wi-Fi work rather than appearing frozen.

### Wi-Fi password

- The password is masked by default.
- An eye icon toggles between hidden and visible password text.
- The toggle must not clear, submit, or modify the password.
- Passwords must not appear in URLs, normal logs, diagnostic output, or error messages.

### Regional settings

- **Time zone**: a dropdown containing the supported time-zone choices. Store an unambiguous identifier, preferably an IANA time-zone name, rather than only a display label or UTC offset.
- **ZIP code**: a validated local text field reserved for future weather integration. It is stored as user configuration but is not used to make network requests in the current release.

The form should identify required fields, reject invalid input before attempting a connection, and display a clear completion or error result.

## Captive Portal Behavior

The setup mode needs three cooperating services:

1. A Wi-Fi access point for the client device.
2. A small DNS responder that directs setup lookups to the board's portal address.
3. A lightweight HTTP server for the setup page, scan endpoint, submit action, and status result.

The portal should answer common connectivity-check requests with a redirect to the setup page. Requests for unknown setup hostnames should resolve to the board while setup mode is active. The portal is only a configuration service; it must not proxy arbitrary internet traffic.

The page should be served from fixed, embedded assets with bounded request and response buffers. It should work on a phone-sized viewport without requiring JavaScript for the basic form submission; JavaScript may improve rescan and password-toggle behavior.

## Transition to the Local Network

After credentials are accepted:

1. Stop or suspend the setup portal as needed to avoid serving stale configuration.
2. Join the selected Wi-Fi network with a bounded timeout.
3. Obtain and retain the local IP address, hostname, or both.
4. Start the settings web service on the station interface.
5. Show the connection result and local address to the client before leaving setup mode, where the hardware flow allows it.
6. Begin NTP synchronization only after the network connection is usable.

The settings page on the local network should expose the same editable Wi-Fi and regional settings as the captive portal. It should also show connection status, IP address, clock synchronization status, and firmware/application version without exposing the Wi-Fi password.

The IP-address marquee is a convenience for initial setup, not the only discovery mechanism. The address should also be available through the settings page, device hostname where supported, and development diagnostics.

If the device cannot reconnect using saved credentials, it should fall back to setup mode after a bounded retry and backoff period. The fallback must not create an endless rapid reconnect loop.

## Security and Reliability Requirements

- Never commit Wi-Fi credentials or test credentials to the repository.
- Do not include passwords in logs, crash output, URLs, or HTML responses after submission.
- Validate all lengths and characters before copying data into fixed-size buffers.
- Use explicit bounds for SSID, password, hostname, time-zone, ZIP code, HTTP headers, and request bodies.
- Protect configuration writes against partial power loss. Use a versioned record with validation data and retain the last known-good record until the new one is confirmed.
- Do not claim that a connection succeeded until the station interface is associated and has usable network configuration.
- Keep configuration changes separate from display refresh and NTP timing work.
- Rate-limit scans and connection attempts to control radio use and prevent accidental rapid retries.

The exact credential protection mechanism must be selected before production deployment. The design must account for the limits of the selected Pico SDK flash/configuration storage and the physical access model of the product.

## Acceptance Criteria

The provisioning feature is ready for client testing when:

- A factory-fresh board exposes the setup network without a serial terminal.
- A phone and a desktop computer can open the captive portal.
- The SSID dropdown reflects a real radio scan.
- Rescan refreshes the list and provides visible progress or completion feedback.
- The password is hidden by default and the eye control works in both states.
- Invalid SSID, password, time-zone, and ZIP code input is rejected safely.
- A valid submission reports connection success or a useful failure without leaking the password.
- After success, the board is reachable through the local network settings page.
- Rebooting with valid settings returns to normal operation without requiring setup again.
- A failed or lost connection eventually returns to setup mode without a rapid retry loop.
- The saved ZIP code is retained but no weather service is contacted yet.
