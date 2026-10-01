#include "settings.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "pico/flash.h"

namespace settings {
namespace {

constexpr uint32_t kMagic = 0x4c454443;  // "LEDC"
constexpr uint16_t kVersion = 2;
constexpr size_t kSlotCount = 2;
// Two slots alternate so the previous good record survives an interrupted write.
constexpr uint32_t kStorageOffset = PICO_FLASH_SIZE_BYTES - kSlotCount * FLASH_SECTOR_SIZE;

struct Record {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t sequence;
    Settings settings;
    uint32_t crc;
};
static_assert(sizeof(Record) <= FLASH_PAGE_SIZE, "settings record must fit in one flash page");

// Version 1 layout, before the NTP server was configurable.
struct RecordV1 {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t sequence;
    char ssid[33];
    char password[64];
    char timezone[40];
    char zip[11];
    char theme[8];
    uint32_t crc;
};

const char* const kTimezones[] = {
    "UTC", "America/New_York", "America/Chicago", "America/Denver", "America/Los_Angeles",
};
const char* const kThemes[] = {"system", "light", "dark"};
const char* const kNtpServers[] = {
    kDefaultNtpServer, "time.google.com", "time.cloudflare.com", "time.nist.gov", "time.windows.com", "time.apple.com",
};

uint8_t write_page[FLASH_PAGE_SIZE];

uint32_t crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

template <typename T>
uint32_t record_crc(const T& record) {
    return crc32(reinterpret_cast<const uint8_t*>(&record), offsetof(T, crc));
}

template <size_t N>
bool terminated(const char (&text)[N]) {
    return std::memchr(text, '\0', N) != nullptr;
}

template <size_t N>
bool one_of(const char* value, const char* const (&choices)[N]) {
    for (const char* choice : choices) {
        if (std::strcmp(value, choice) == 0) {
            return true;
        }
    }
    return false;
}

bool is_digits(const char* text, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
    }
    return true;
}

bool read_slot(size_t slot, Record& out) {
    const void* stored = reinterpret_cast<const void*>(XIP_BASE + kStorageOffset + slot * FLASH_SECTOR_SIZE);
    std::memcpy(&out, stored, sizeof(out));
    bool intact = out.magic == kMagic && out.version == kVersion && out.size == sizeof(Record) && out.crc == record_crc(out);

    if (out.magic == kMagic && out.version == 1) {
        RecordV1 old;
        std::memcpy(&old, stored, sizeof(old));
        intact = old.size == sizeof(RecordV1) && old.crc == record_crc(old);
        if (intact) {
            out = {};
            out.sequence = old.sequence;
            std::memcpy(out.settings.ssid, old.ssid, sizeof(old.ssid));
            std::memcpy(out.settings.password, old.password, sizeof(old.password));
            std::memcpy(out.settings.timezone, old.timezone, sizeof(old.timezone));
            std::memcpy(out.settings.zip, old.zip, sizeof(old.zip));
            std::memcpy(out.settings.theme, old.theme, sizeof(old.theme));
            std::memcpy(out.settings.ntp_server, kDefaultNtpServer, sizeof(kDefaultNtpServer));
        }
    }

    const Settings& s = out.settings;
    return intact && terminated(s.ssid) && terminated(s.password) && terminated(s.timezone) && terminated(s.zip) &&
           terminated(s.theme) && terminated(s.ntp_server) && validate(s) == nullptr;
}

int newest_slot(uint32_t& sequence) {
    int newest = -1;
    for (size_t slot = 0; slot < kSlotCount; ++slot) {
        Record record;
        if (read_slot(slot, record) &&
            (newest < 0 || static_cast<int32_t>(record.sequence - sequence) > 0)) {
            newest = static_cast<int>(slot);
            sequence = record.sequence;
        }
    }
    return newest;
}

void write_slot(void* arg) {
    const uint32_t offset = *static_cast<const uint32_t*>(arg);
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
    flash_range_program(offset, write_page, FLASH_PAGE_SIZE);
}

}  // namespace

const char* validate(const Settings& value) {
    const size_t ssid_length = std::strlen(value.ssid);
    if (ssid_length == 0) {
        return "Choose or enter a Wi-Fi network.";
    }
    for (size_t i = 0; i < ssid_length; ++i) {
        if (static_cast<unsigned char>(value.ssid[i]) < 0x20) {
            return "The network name contains invalid characters.";
        }
    }

    const size_t password_length = std::strlen(value.password);
    bool password_ok = password_length == 0 || (password_length >= 8 && password_length <= 63);
    for (size_t i = 0; i < password_length; ++i) {
        if (value.password[i] < 0x20 || value.password[i] > 0x7e) {
            password_ok = false;
        }
    }
    if (!password_ok) {
        return "The Wi-Fi password must be 8 to 63 characters, or blank for an open network.";
    }

    if (!one_of(value.timezone, kTimezones)) {
        return "Choose a supported time zone.";
    }

    const size_t zip_length = std::strlen(value.zip);
    const bool zip_ok = zip_length == 0 || (zip_length == 5 && is_digits(value.zip, 5)) ||
                        (zip_length == 10 && is_digits(value.zip, 5) && value.zip[5] == '-' &&
                         is_digits(value.zip + 6, 4));
    if (!zip_ok) {
        return "Enter a 5-digit ZIP code or ZIP+4, or leave it blank.";
    }

    if (!one_of(value.theme, kThemes)) {
        return "Choose a supported appearance.";
    }
    if (!one_of(value.ntp_server, kNtpServers)) {
        return "Choose a supported time server.";
    }
    return nullptr;
}

bool load(Settings& out) {
    uint32_t sequence = 0;
    const int slot = newest_slot(sequence);
    if (slot < 0) {
        return false;
    }
    Record record;
    read_slot(static_cast<size_t>(slot), record);
    out = record.settings;
    return true;
}

bool save(const Settings& value) {
    if (validate(value) != nullptr) {
        return false;
    }

    uint32_t sequence = 0;
    const int newest = newest_slot(sequence);
    const size_t target = newest == 0 ? 1 : 0;

    Record record{};
    record.magic = kMagic;
    record.version = kVersion;
    record.size = sizeof(Record);
    record.sequence = newest < 0 ? 1 : sequence + 1;
    record.settings = value;
    record.crc = record_crc(record);

    std::memset(write_page, 0xff, sizeof(write_page));
    std::memcpy(write_page, &record, sizeof(record));
    uint32_t offset = kStorageOffset + target * FLASH_SECTOR_SIZE;
    if (flash_safe_execute(write_slot, &offset, 1000) != PICO_OK) {
        return false;
    }

    Record check;
    return read_slot(target, check) && check.sequence == record.sequence;
}

}  // namespace settings
