#include "timekeeping.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "pico/rand.h"
#include "pico/time.h"

namespace timekeeping {
namespace {

constexpr uint16_t kNtpPort = 123;
constexpr size_t kPacketSize = 48;
constexpr int64_t kNtpToUnixSeconds = 2208988800;
constexpr uint32_t kRequestTimeoutMs = 5000;
constexpr uint32_t kResyncIntervalMs = 60 * 60 * 1000;
constexpr uint32_t kRetryDelaysMs[] = {5000, 15000, 30000, 60000, 300000};
constexpr int64_t kMinValidUnix = 1767225600;  // 2026-01-01
constexpr int64_t kMaxValidUnix = 4102444800;  // 2100-01-01

struct Zone {
    const char* iana;
    const char* posix;
};

constexpr Zone kZones[] = {
    {"UTC", "UTC0"},
    {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
};

enum class Phase { Stopped, Waiting, Resolving, AwaitingReply };

Phase phase = Phase::Stopped;
char server_name[32] = "pool.ntp.org";
udp_pcb* ntp_pcb = nullptr;
absolute_time_t next_action;
size_t failures = 0;
ip_addr_t server;
uint8_t nonce[8];
uint64_t request_sent_us = 0;

bool synced = false;
int64_t sync_unix_us = 0;
uint64_t sync_monotonic_us = 0;

int64_t unix_us_at(uint64_t monotonic_us) {
    return sync_unix_us + static_cast<int64_t>(monotonic_us - sync_monotonic_us);
}

uint32_t read_be32(const uint8_t* bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) | bytes[3];
}

void wait_then_retry(const char* reason) {
    const size_t index = std::min(failures, std::size(kRetryDelaysMs) - 1);
    ++failures;
    std::printf("NTP: %s, retrying in %lu s\n", reason, static_cast<unsigned long>(kRetryDelaysMs[index] / 1000));
    phase = Phase::Waiting;
    next_action = make_timeout_time_ms(kRetryDelaysMs[index]);
}

void send_request() {
    uint8_t packet[kPacketSize]{};
    packet[0] = 0x23;  // LI 0, version 4, mode 3 (client)
    const uint32_t random[2] = {get_rand_32(), get_rand_32()};
    std::memcpy(nonce, random, sizeof(nonce));
    // The server echoes our transmit timestamp as its origin timestamp, so a random one rejects spoofed replies.
    std::memcpy(packet + 40, nonce, sizeof(nonce));

    pbuf* request = pbuf_alloc(PBUF_TRANSPORT, kPacketSize, PBUF_RAM);
    if (request == nullptr) {
        wait_then_retry("out of memory");
        return;
    }
    pbuf_take(request, packet, kPacketSize);
    request_sent_us = time_us_64();
    const err_t result = udp_sendto(ntp_pcb, request, &server, kNtpPort);
    pbuf_free(request);
    if (result != ERR_OK) {
        wait_then_retry("send failed");
        return;
    }
    phase = Phase::AwaitingReply;
    next_action = make_timeout_time_ms(kRequestTimeoutMs);
}

void dns_found(const char* name, const ip_addr_t* address, void*) {
    // Ignore late answers for a server that has since been replaced.
    if (phase != Phase::Resolving || std::strcmp(name, server_name) != 0) {
        return;
    }
    if (address == nullptr) {
        wait_then_retry("could not resolve server");
        return;
    }
    server = *address;
    send_request();
}

void start_request() {
    phase = Phase::Resolving;
    next_action = make_timeout_time_ms(kRequestTimeoutMs);
    const err_t result = dns_gethostbyname(server_name, &server, dns_found, nullptr);
    if (result == ERR_OK) {
        send_request();
    } else if (result != ERR_INPROGRESS) {
        wait_then_retry("DNS lookup failed");
    }
}

void ntp_receive(void*, udp_pcb*, pbuf* packet, const ip_addr_t* address, u16_t port) {
    const uint64_t received_us = time_us_64();
    uint8_t reply[kPacketSize];
    const bool complete = packet->tot_len >= kPacketSize && pbuf_copy_partial(packet, reply, kPacketSize, 0) == kPacketSize;
    pbuf_free(packet);
    if (phase != Phase::AwaitingReply || !complete || port != kNtpPort || !ip_addr_eq(address, &server) ||
        std::memcmp(reply + 24, nonce, sizeof(nonce)) != 0) {
        return;
    }

    const uint8_t leap = reply[0] >> 6;
    const uint8_t version = (reply[0] >> 3) & 0x07;
    const uint8_t mode = reply[0] & 0x07;
    const uint8_t stratum = reply[1];
    if (leap == 3 || mode != 4 || version < 3 || version > 4 || stratum == 0 || stratum > 15) {
        wait_then_retry("server reply rejected");
        return;
    }

    const uint32_t seconds = read_be32(reply + 40);
    const uint32_t fraction = read_be32(reply + 44);
    // NTP era 1 begins in 2036; timestamps with the top bit clear belong to it.
    const int64_t era_offset = (seconds & 0x80000000u) != 0 ? 0 : (int64_t{1} << 32);
    const int64_t unix_seconds = static_cast<int64_t>(seconds) + era_offset - kNtpToUnixSeconds;
    if (unix_seconds < kMinValidUnix || unix_seconds > kMaxValidUnix) {
        wait_then_retry("implausible time from server");
        return;
    }

    const int64_t transmit_us = unix_seconds * 1000000 + static_cast<int64_t>((uint64_t{fraction} * 1000000) >> 32);
    const uint64_t round_trip_us = received_us - request_sent_us;
    const int64_t now_us = transmit_us + static_cast<int64_t>(round_trip_us / 2);
    const bool was_synced = synced;
    const int64_t correction_ms = was_synced ? (now_us - unix_us_at(received_us)) / 1000 : 0;

    sync_unix_us = now_us;
    sync_monotonic_us = received_us;
    synced = true;
    failures = 0;
    phase = Phase::Waiting;
    next_action = make_timeout_time_ms(kResyncIntervalMs);

    std::tm local{};
    local_time(local);
    char text[40];
    std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S %Z", &local);
    if (was_synced) {
        std::printf("NTP sync from %s: %s (stratum %u, rtt %lu ms, correction %lld ms)\n", server_name, text, stratum,
                    static_cast<unsigned long>(round_trip_us / 1000), static_cast<long long>(correction_ms));
    } else {
        std::printf("NTP sync from %s: %s (stratum %u, rtt %lu ms)\n", server_name, text, stratum,
                    static_cast<unsigned long>(round_trip_us / 1000));
    }
}

}  // namespace

bool init() {
    set_timezone("UTC");
    ntp_pcb = udp_new();
    if (ntp_pcb == nullptr) {
        return false;
    }
    udp_recv(ntp_pcb, ntp_receive, nullptr);
    return true;
}

void set_timezone(const char* iana_name) {
    const char* posix = kZones[0].posix;
    for (const Zone& zone : kZones) {
        if (std::strcmp(zone.iana, iana_name) == 0) {
            posix = zone.posix;
        }
    }
    setenv("TZ", posix, 1);
    tzset();
}

void set_server(const char* host_name) {
    if (std::strcmp(host_name, server_name) == 0) {
        return;
    }
    std::snprintf(server_name, sizeof(server_name), "%s", host_name);
    std::printf("NTP server: %s\n", server_name);
    if (phase != Phase::Stopped) {
        failures = 0;
        start_request();
    }
}

void start() {
    if (phase != Phase::Stopped) {
        return;
    }
    failures = 0;
    start_request();
}

void stop() {
    phase = Phase::Stopped;
}

void poll() {
    if (phase == Phase::Stopped || !time_reached(next_action)) {
        return;
    }
    if (phase == Phase::Waiting) {
        start_request();
    } else {
        wait_then_retry(phase == Phase::Resolving ? "DNS lookup timed out" : "no reply from server");
    }
}

bool now_unix(int64_t& seconds) {
    if (!synced) {
        return false;
    }
    seconds = unix_us_at(time_us_64()) / 1000000;
    return true;
}

bool local_time(std::tm& out) {
    int64_t seconds = 0;
    if (!now_unix(seconds)) {
        return false;
    }
    const time_t value = static_cast<time_t>(seconds);
    return localtime_r(&value, &out) != nullptr;
}

}  // namespace timekeeping
