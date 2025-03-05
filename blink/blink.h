#ifndef __BLINK_H
#define __BLINK_H

#include <stdint.h>
#include <stdbool.h>
#include "scheduler.h"
#include "protocol.h"

//=========================== defines ==========================================

#define BLINK_MAX_NODES 10 // TODO: find a way to sync with the pre-stored schedules

#define BLINK_BROADCAST_ADDRESS 0xFFFFFFFFFFFFFFFF

//=========================== variables ========================================

typedef enum {
    BLINK_CONNECTED,
    BLINK_DISCONNECTED,
    BLINK_NODE_JOINED,
    BLINK_NODE_LEFT,
} bl_event_t;

typedef enum {
    BLINK_GATEWAY = 'G',
    BLINK_NODE = 'D',
} bl_node_type_t;

// NOTE: could have these be a single callback (*bl_event_cb_t)(bl_event_t event, uint8_t *packet, uint8_t length), and then one of the events can be BLINK_RX
typedef void (*bl_rx_cb_t)(uint8_t *packet, uint8_t length);  ///< Function pointer to the callback function called on packet receive
typedef void (*bl_event_cb_t)(bl_event_t event);             ///< Function pointer to the callback function called for network events

//=========================== prototypes ==========================================

void bl_init(bl_node_type_t node_type, bl_rx_cb_t rx_app_callback, bl_event_cb_t events_callback);
void bl_tx(uint8_t *packet, uint8_t length);
void bl_get_joined_nodes(uint64_t *nodes, uint8_t *num_nodes);

#endif // __BLINK_H
