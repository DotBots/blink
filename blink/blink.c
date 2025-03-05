#include <nrf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "protocol.h"
#include "mac.h"
#include "scheduler.h"
#include "queue.h"
#include "radio.h"

//=========================== defines ==========================================

typedef struct {
    bl_node_type_t          node_type;
    bl_rx_cb_t              app_rx_callback;
    bl_event_cb_t           app_event_callback;

    // node data
    bool                    is_connected;

    // gateway data
    uint64_t                joined_nodes[BLINK_MAX_NODES];
    uint8_t                 joined_nodes_len;
} blink_vars_t;

//=========================== variables ========================================

static blink_vars_t _blink_vars = { 0 };

//=========================== prototypes =======================================

//static void _bl_callback(uint8_t *packet, uint8_t length);
static void isr_radio_start_frame(uint32_t ts);
static void isr_radio_end_frame(uint32_t ts);

//=========================== public ===========================================

void bl_init(bl_node_type_t node_type, bl_rx_cb_t rx_callback, bl_event_cb_t event_callback) {
    _blink_vars.node_type = node_type;
    _blink_vars.is_connected = true; // FIXME true just for tests
    _blink_vars.app_rx_callback = rx_callback;
    _blink_vars.app_event_callback = event_callback;

    bl_scheduler_init(node_type, NULL);

    // TODO: bl_mac_init(node_type, rx_callback);

    // TMP: using the radio directly
    bl_radio_init(&isr_radio_start_frame, &isr_radio_end_frame, DB_RADIO_BLE_2MBit);
    bl_radio_set_channel(BLINK_FIXED_CHANNEL); // temporary value
    bl_radio_rx();
}

void bl_tx(uint8_t *packet, uint8_t length) {
    // enqueue for transmission
    bl_queue_add(packet, length);

    // TMP: send immediately, for now just ignoring the queue
    bl_radio_disable();
    bl_radio_tx(packet, length); // TODO: use a packet queue
}

void bl_get_joined_nodes(uint64_t *nodes, uint8_t *num_nodes) {
    *num_nodes = _blink_vars.joined_nodes_len;
    memcpy(nodes, _blink_vars.joined_nodes, _blink_vars.joined_nodes_len * sizeof(uint64_t));
}

//=========================== callbacks ===========================================

static void isr_radio_start_frame(uint32_t ts) {
    (void)ts;
}
static void isr_radio_end_frame(uint32_t ts) {
    (void)ts;
}

//static void _bl_callback(uint8_t *packet, uint8_t length) {
//    printf("BLINK: Received packet of length %d\n", length);

//    // Check if the packet is big enough to have a header
//    size_t header_len = sizeof(bl_packet_header_t);
//    if (length < header_len) {
//        return;
//    }

//    bl_packet_header_t *header = (bl_packet_header_t *)packet;

//    switch (header->type) {
//        case BLINK_PACKET_JOIN_REQUEST: // NOTE: handle this in the maclow instead?
//            if (_blink_vars.node_type == BLINK_GATEWAY && _blink_vars.joined_nodes_len < BLINK_MAX_NODES) {
//                // TODO:
//                // - check if node can join
//                // - send/enqueue join response
//                _blink_vars.joined_nodes[_blink_vars.joined_nodes_len++] = header->src;
//                _blink_vars.app_event_callback(BLINK_NODE_JOINED);
//            }
//            break;
//        case BLINK_PACKET_JOIN_RESPONSE: // NOTE: handle this in the maclow instead?
//            if (_blink_vars.node_type == BLINK_NODE) {
//                _blink_vars.is_connected = true;
//                _blink_vars.app_event_callback(BLINK_CONNECTED);
//            }
//            break;
//        case BLINK_PACKET_DATA:
//            if (_blink_vars.app_rx_callback && (_blink_vars.node_type == BLINK_GATEWAY || _blink_vars.is_connected)) {
//                _blink_vars.app_rx_callback(packet + header_len, length - header_len);
//            }
//            break;
//        default:
//            printf("BLINK: Received packet of type %d\n", header->type);
//            break;
//    }
//}
