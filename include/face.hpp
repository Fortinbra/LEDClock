#pragma once

namespace face {

// Full-array rainbow shown until the first station connection.
void show_waiting();
// Scrolls "IP <address>" for 30 s, then returns to the clock. Restarts if the address changes.
void show_ip(const char* address);
// When false, the clock keeps running but shows a small offline indicator.
void set_network_ok(bool ok);
// Renders only when visible content changes or an animation step is due.
void poll();

}  // namespace face
