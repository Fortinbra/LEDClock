#include <stdio.h>
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "portal.hpp"



int main()
{
    stdio_init_all();

    if (cyw43_arch_init() != 0) {
        printf("Wi-Fi initialization failed\n");
        return 1;
    }

    cyw43_arch_enable_ap_mode("LEDClock-Setup", nullptr, CYW43_AUTH_OPEN);
    if (!portal::init()) {
        printf("Captive portal initialization failed\n");
        cyw43_arch_deinit();
        return 1;
    }

    while (true) {
        cyw43_arch_poll();
        portal::poll();
        sleep_ms(10);
    }
}