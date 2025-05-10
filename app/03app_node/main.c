/**
 * @file
 * @ingroup     app
 *
 * @brief       Blink Node application example
 *
 * @author Geovane Fedrecheski <geovane.fedrecheski@inria.fr>
 *
 * @copyright Inria, 2025
 */
#include <nrf.h>
#include <stdio.h>
#include <stdbool.h>

#include "bl_gpio.h"
#include "bl_device.h"
#include "bl_radio.h"
#include "sec.h"
#include "bl_device.h"
#include "bl_timer_hf.h"
#include "packet.h"
#include "queue.h"
#include "blink.h"

#include "minimote.h"

//=========================== defines ==========================================

#define BLINK_APP_TIMER_DEV 1

typedef struct {
    bool dummy;
} node_vars_t;

//=========================== variables ========================================

node_vars_t node_vars = { 0 };

uint8_t payload[] = { 0xF0, 0xF0, 0xF0, 0xF0, 0xF0 };
uint8_t payload_len = 5;

extern schedule_t schedule_minuscule, schedule_tiny, schedule_huge;

schedule_t *schedule_app = &schedule_minuscule;

//=========================== prototypes =======================================

static void blink_event_callback(bl_event_t event, bl_event_data_t event_data);
static void tx_if_connected(void);

//=========================== main =============================================

int main(void)
{
    printf("Hello Blink Node %016llX\n", bl_device_id());
    bl_timer_hf_init(BLINK_APP_TIMER_DEV);

    // bl_timer_hf_set_periodic_us(BLINK_APP_TIMER_DEV, 0, 1000 * 500, &tx_if_connected);
    bl_timer_hf_set_oneshot_us(BLINK_APP_TIMER_DEV, 0, 0, &tx_if_connected); // do not tx

    board_init();

    blink_init(BLINK_NODE, schedule_app, &blink_event_callback);

    while (1) {
        __SEV();
        __WFE();
        __WFE();

        blink_event_loop();
    }
}

//=========================== callbacks ========================================

static void blink_event_callback(bl_event_t event, bl_event_data_t event_data) {
    switch (event) {
        case BLINK_NEW_PACKET: {
            blink_packet_t packet = event_data.data.new_packet;
            printf("RX %u B: src=%016llX dst=%016llX (rssi %d) payload=", packet.len, packet.header->src, packet.header->dst, bl_radio_rssi());
            for (int i = 0; i < packet.payload_len; i++) {
                printf("%02X ", packet.payload[i]);
            }
            printf("\n");
            blink_node_tx_payload(payload, payload_len);
            break;
        }
        case BLINK_CONNECTED: {
            uint64_t gateway_id = event_data.data.gateway_info.gateway_id;
            printf("Connected to gateway %016llX\n", gateway_id);
            if (gateway_id == 0xCEA467E20BACC0AB) {
                board_set_rgb(GREEN);
            } else {
                board_set_rgb(OTHER);
            }
            break;
        }
        case BLINK_DISCONNECTED: {
            uint64_t gateway_id = event_data.data.gateway_info.gateway_id;
            printf("Disconnected from gateway %016llX, reason: %u\n", gateway_id, event_data.tag);
            board_set_rgb(RED);
            break;
        }
        case BLINK_ERROR:
            printf("Error\n");
            break;
        default:
            break;
    }
}

//=========================== private =========================================

static void tx_if_connected(void) {
    if (blink_node_is_connected()) {
        blink_node_tx_payload(payload, payload_len);
    }
}
