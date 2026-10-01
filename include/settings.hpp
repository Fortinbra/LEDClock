#pragma once

namespace settings {

struct Settings {
    char ssid[33];
    char password[64];
    char timezone[40];
    char zip[11];
    char theme[8];
    char ntp_server[32];
};

constexpr char kDefaultNtpServer[] = "pool.ntp.org";

// Returns nullptr when valid, otherwise a user-facing reason.
const char* validate(const Settings& value);

bool load(Settings& out);
bool save(const Settings& value);

}  // namespace settings
