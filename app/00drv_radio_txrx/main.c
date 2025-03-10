/**
 * @file
 * @ingroup     net_maclow
 *
 * @brief       Example on how to use the maclow driver
 *
 * @author Geovane Fedrecheski <geovane.fedrecheski@inria.fr>
 *
 * @copyright Inria, 2024
 */
#include <nrf.h>
#include <stdio.h>
#include <stdlib.h>

#include "device.h"
#include "radio.h"
#include "timer_hf.h"
#include "protocol.h"
#include "scheduler.h"
#include "mac.h"

//=========================== debug ============================================

#include "gpio.h"
gpio_t pin0 = { .port = 1, .pin = 2 }; // variable names reflect the logic analyzer channels
gpio_t pin1 = { .port = 1, .pin = 3 };
gpio_t pin2 = { .port = 1, .pin = 4 };
gpio_t pin3 = { .port = 1, .pin = 5 };
#define DEBUG_GPIO_TOGGLE(pin) db_gpio_toggle(pin)
#define DEBUG_GPIO_SET(pin) db_gpio_set(pin)
#define DEBUG_GPIO_CLEAR(pin) db_gpio_clear(pin)

//=========================== defines =========================================

typedef struct {
    uint64_t asn;
} txrx_vars_t;

//=========================== variables =======================================

txrx_vars_t txrx_vars = { 0 };

extern schedule_t schedule_only_beacons, schedule_huge;

//=========================== prototypes ======================================

static void send_beacon_prepare(void);
static void send_beacon_dispatch(void);

static void isr_radio_start_frame(uint32_t ts);
static void isr_radio_end_frame(uint32_t ts);

//=========================== main ============================================

int main(void) {
    bl_timer_hf_init(BLINK_TIMER_DEV);
    db_gpio_init(&pin0, DB_GPIO_OUT);
    db_gpio_init(&pin1, DB_GPIO_OUT);

    bl_radio_init(&isr_radio_start_frame, &isr_radio_end_frame, DB_RADIO_BLE_2MBit);
    bl_radio_set_channel(BLINK_FIXED_SCAN_CHANNEL);

    printf("BLINK_FIXED_SCAN_CHANNEL = %d\n", BLINK_FIXED_SCAN_CHANNEL);

    bl_timer_hf_set_periodic_us(BLINK_TIMER_DEV, 0, 5000, send_beacon_prepare); // 5 ms

    while (1) {
        __WFE();
    }
}

//=========================== private =========================================

static void send_beacon_prepare(void) {
    printf("Sending beacon from %llx\n", db_device_id());
    uint8_t packet[BLINK_PACKET_MAX_SIZE] = { 0 };
    size_t len = bl_build_packet_beacon(packet, txrx_vars.asn++, 10, schedule_huge.id);
    bl_radio_disable();
    bl_radio_tx_prepare(packet, len);
    DEBUG_GPIO_SET(&pin0);
    // give 100 us for radio to ramp up
    bl_timer_hf_set_oneshot_us(BLINK_TIMER_DEV, 1, 100, &send_beacon_dispatch);
}

static void send_beacon_dispatch(void) {
    bl_radio_tx_dispatch();
    DEBUG_GPIO_CLEAR(&pin0);

    // beacon = 20 bytes = TOA 80 us
    // schedule radio for RX in 200 us
    bl_timer_hf_set_oneshot_us(BLINK_TIMER_DEV, 1, 200, bl_radio_rx);
}

static void isr_radio_start_frame(uint32_t ts) {
    DEBUG_GPIO_SET(&pin1);
    printf("Start frame at %d\n", ts);
}

static void isr_radio_end_frame(uint32_t ts) {
    DEBUG_GPIO_CLEAR(&pin1);
    printf("End frame at %d\n", ts);

    if (bl_radio_pending_rx_read()) { // interrupt came from RX
        uint8_t packet[BLINK_PACKET_MAX_SIZE];
        uint8_t length;
        bl_radio_get_rx_packet(packet, &length);
        printf("Received packet of length %d\n", length);
        for (size_t i = 0; i < length; i++) {
            printf("%02x ", packet[i]);
        }
        puts("");
    } else { // interrupt came from TX
        // handle, if needed
    }
}
