#include <cstdio>
#include <cstring>
#include <ctime>

#include "diagnostic.hpp"
#include "display.hpp"
#include "face.hpp"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "portal.hpp"
#include "settings.hpp"
#include "timekeeping.hpp"

namespace {

constexpr char kSetupSsid[] = "LEDClock-Setup";
// Runs the LED wiring checks instead of the clock face until the layout is verified.
constexpr bool kRunDisplayDiagnostic = false;
constexpr uint32_t kJoinTimeoutMs = 20000;
constexpr uint32_t kRetryDelayMs = 10000;
constexpr int kMaxJoinAttempts = 3;
// Keeps the setup AP up after a successful join so the phone can read the result.
constexpr uint32_t kSetupLingerMs = 15000;

enum class State { Setup, Joining, RetryWait, Connected };

State state = State::Setup;
settings::Settings active{};
bool have_active = false;
settings::Settings candidate{};
bool testing_candidate = false;
bool ap_enabled = false;
bool sta_enabled = false;
bool ap_shutdown_pending = false;
bool connected_since_boot = false;
int join_attempts = 0;
absolute_time_t deadline;
absolute_time_t ap_shutdown_at;

const char* station_ip() {
    return ip4addr_ntoa(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]));
}

void start_setup_ap() {
    if (ap_enabled) {
        return;
    }
    cyw43_arch_enable_ap_mode(kSetupSsid, nullptr, CYW43_AUTH_OPEN);
    ap_enabled = true;
    ap_shutdown_pending = false;
    printf("Setup AP '%s' up at 192.168.4.1\n", kSetupSsid);
}

void stop_setup_ap() {
    if (!ap_enabled) {
        return;
    }
    cyw43_arch_disable_ap_mode();
    ap_enabled = false;
    ap_shutdown_pending = false;
    printf("Setup AP stopped\n");
}

void begin_join(const settings::Settings& target) {
    if (!sta_enabled) {
        cyw43_arch_enable_sta_mode();
        sta_enabled = true;
    }
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    const bool open = target.password[0] == '\0';
    printf("Joining '%s'\n", target.ssid);
    state = State::Joining;
    deadline = make_timeout_time_ms(kJoinTimeoutMs);
    if (cyw43_arch_wifi_connect_async(target.ssid, open ? nullptr : target.password,
                                      open ? CYW43_AUTH_OPEN : CYW43_AUTH_WPA2_MIXED_PSK) != 0) {
        deadline = get_absolute_time();
    }
}

void enter_setup() {
    state = State::Setup;
    start_setup_ap();
}

void on_join_success() {
    printf("Connected to '%s', IP %s\n", testing_candidate ? candidate.ssid : active.ssid, station_ip());
    join_attempts = 0;
    state = State::Connected;
    if (!connected_since_boot || testing_candidate) {
        face::show_ip(station_ip());
    }
    connected_since_boot = true;
    if (testing_candidate) {
        testing_candidate = false;
        active = candidate;
        have_active = true;
        portal::set_saved(&active);
        timekeeping::set_timezone(active.timezone);
        timekeeping::set_server(active.ntp_server);
        if (settings::save(active)) {
            printf("Settings saved\n");
            portal::report_join(portal::JoinState::Connected, "Connected and saved.", station_ip());
        } else {
            printf("Saving settings failed\n");
            portal::report_join(portal::JoinState::Failed, "Connected, but the settings could not be saved.",
                                station_ip());
        }
    } else {
        portal::report_join(portal::JoinState::Connected, "Connected.", station_ip());
    }
    if (ap_enabled) {
        ap_shutdown_pending = true;
        ap_shutdown_at = make_timeout_time_ms(kSetupLingerMs);
    }
    timekeeping::start();
}

void on_join_failure(const char* reason) {
    printf("Join failed: %s\n", reason);
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);

    if (testing_candidate) {
        testing_candidate = false;
        portal::report_join(portal::JoinState::Failed, reason, nullptr);
        if (have_active && !ap_enabled) {
            // A settings change from the local network failed; return to the known-good network.
            join_attempts = 0;
            begin_join(active);
        } else {
            enter_setup();
        }
        return;
    }

    if (++join_attempts < kMaxJoinAttempts) {
        state = State::RetryWait;
        deadline = make_timeout_time_ms(kRetryDelayMs);
        return;
    }
    printf("Giving up on saved network, entering setup mode\n");
    join_attempts = 0;
    enter_setup();
}

void poll_join() {
    const int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    if (status == CYW43_LINK_UP) {
        on_join_success();
    } else if (status == CYW43_LINK_BADAUTH) {
        on_join_failure("Wrong Wi-Fi password.");
    } else if (status == CYW43_LINK_NONET) {
        on_join_failure("Network not found.");
    } else if (status == CYW43_LINK_FAIL) {
        on_join_failure("Could not connect to the network.");
    } else if (time_reached(deadline)) {
        on_join_failure("Timed out connecting to the network.");
    }
}

void handle_submission() {
    settings::Settings submitted{};
    if (!portal::take_submission(submitted)) {
        return;
    }
    // A blank password on the settings page means "keep the saved one".
    if (submitted.password[0] == '\0' && have_active && std::strcmp(submitted.ssid, active.ssid) == 0) {
        std::memcpy(submitted.password, active.password, sizeof(submitted.password));
    }

    const bool same_network = have_active && std::strcmp(submitted.ssid, active.ssid) == 0 &&
                              std::strcmp(submitted.password, active.password) == 0;
    if (state == State::Connected && same_network) {
        if (settings::save(submitted)) {
            active = submitted;
            timekeeping::set_timezone(active.timezone);
            timekeeping::set_server(active.ntp_server);
            printf("Settings saved\n");
            portal::report_join(portal::JoinState::Connected, "Settings saved.", station_ip());
        } else {
            portal::report_join(portal::JoinState::Failed, "The settings could not be saved.", station_ip());
        }
        return;
    }

    candidate = submitted;
    testing_candidate = true;
    begin_join(candidate);
}

void report_station_count() {
    static int last_stations = -1;
    static absolute_time_t next_check;
    if (!ap_enabled || !time_reached(next_check)) {
        return;
    }
    static uint8_t station_macs[8 * 6];
    int stations = 8;
    cyw43_wifi_ap_get_stas(&cyw43_state, &stations, station_macs);
    if (stations != last_stations) {
        printf("AP stations associated: %d\n", stations);
        last_stations = stations;
    }
    next_check = make_timeout_time_ms(1000);
}

void report_time() {
    static int64_t last_minute = -1;
    int64_t seconds = 0;
    if (!timekeeping::now_unix(seconds) || seconds / 60 == last_minute) {
        return;
    }
    last_minute = seconds / 60;
    std::tm local{};
    if (timekeeping::local_time(local)) {
        char text[32];
        std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M %Z", &local);
        printf("Local time %s\n", text);
    }
}

}  // namespace

int main()
{
    stdio_init_all();
    printf("\nLEDClock starting\n");
    if (!display::init()) {
        printf("Display initialization failed\n");
    }

    if (cyw43_arch_init() != 0) {
        printf("Wi-Fi initialization failed\n");
        return 1;
    }
    if (!portal::init() || !timekeeping::init()) {
        printf("Network service initialization failed\n");
        cyw43_arch_deinit();
        return 1;
    }

    if (settings::load(active)) {
        have_active = true;
        portal::set_saved(&active);
        timekeeping::set_timezone(active.timezone);
        timekeeping::set_server(active.ntp_server);
        printf("Saved settings found\n");
        begin_join(active);
    } else {
        printf("No saved settings\n");
        enter_setup();
    }

    while (true) {
        cyw43_arch_poll();
        portal::poll();
        handle_submission();

        switch (state) {
        case State::Setup:
            break;
        case State::Joining:
            poll_join();
            break;
        case State::RetryWait:
            if (time_reached(deadline)) {
                begin_join(active);
            }
            break;
        case State::Connected:
            if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
                printf("Wi-Fi connection lost\n");
                timekeeping::stop();
                join_attempts = 0;
                begin_join(active);
            } else if (ap_shutdown_pending && time_reached(ap_shutdown_at)) {
                stop_setup_ap();
            }
            break;
        }

        report_station_count();
        timekeeping::poll();
        report_time();
        if (kRunDisplayDiagnostic) {
            diagnostic::poll();
        } else {
            face::set_network_ok(state == State::Connected);
            face::poll();
        }
        sleep_ms(10);
    }
}