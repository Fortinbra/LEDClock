#pragma once

#include <cstdint>
#include <ctime>

namespace timekeeping {

bool init();
// Accepts one of the IANA names supported by settings::validate().
void set_timezone(const char* iana_name);
// Switches NTP servers, resynchronizing immediately if sync is running.
void set_server(const char* host_name);
// Begins (or resumes) NTP synchronization; call once the station link is up.
void start();
void stop();
void poll();

bool now_unix(int64_t& seconds);
bool local_time(std::tm& out);

}  // namespace timekeeping
