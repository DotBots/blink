/**
 * @file
 * @ingroup     app
 *
 * @brief       Blink Gateway application example
 *
 * @author Geovane Fedrecheski <geovane.fedrecheski@inria.fr>
 *
 * @copyright Inria, 2024
 */
#include <nrf.h>
#include <stdio.h>

#include "blink.h"
#include "maclow.h"
#include "protocol.h"
#include "timer_hf.h"

//=========================== defines ==========================================

#define DATA_LEN 4

//=========================== callbacks ========================================

static void rx_cb(uint8_t *packet, uint8_t length)
{
    printf("Gateway application received packet of length %d: ", length);
    for (int i = 0; i < length; i++) {
        printf("%02X ", packet[i]);
    }
    printf("\n");
}

//=========================== variables ========================================

uint8_t packet[BLINK_PACKET_MAX_SIZE] = { 0 };
uint8_t data[DATA_LEN] = { 0xFF, 0xFE, 0xFD, 0xFC };
uint64_t dst = 0x1;

//=========================== main =============================================

int main(void)
{
    printf("Hello Blink Gateway\n");
    bl_timer_hf_init(BLINK_TIMER_DEV);

    bl_init(NODE_TYPE_GATEWAY, &rx_cb, NULL);

    size_t i = 0;
    while (1) {
        printf("Sending packet %d\n", i++);

        // prepare and send packet (TODO: internally should enqueue it instead)
        size_t packet_len = bl_build_packet(packet, dst, data, DATA_LEN);
        bl_tx(packet, packet_len);

        bl_timer_hf_delay_ms(BLINK_TIMER_DEV, 1000);
    }

    while (1) {
        __SEV();
        __WFE();
        __WFE();
    }
}
