/**
 * @file
 * @ingroup     blink_assoc
 *
 * @brief       Association procedure for the blink protocol
 *
 * @author Geovane Fedrecheski <geovane.fedrecheski@inria.fr>
 *
 * @copyright Inria, 2024
 */

#include <nrf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "bl_device.h"
#include "bl_radio.h"
#include "bl_rng.h"
#include "association.h"
#include "scan.h"
#include "queue.h"
#include "mac.h"
#include "blink.h"
#include "scheduler.h"

//=========================== debug ============================================

#ifndef DEBUG // FIXME: remove before merge. Just to make VS Code enable code behind `#ifdef DEBUG`
#define DEBUG
#endif

#ifdef DEBUG
#include "bl_gpio.h" // for debugging
// the 4 LEDs of the DK are on port 0, pins 13, 14, 15, 16
bl_gpio_t led0 = { .port = 0, .pin = 13 };
bl_gpio_t led1 = { .port = 0, .pin = 14 };
bl_gpio_t led2 = { .port = 0, .pin = 15 };
bl_gpio_t led3 = { .port = 0, .pin = 16 };
#define DEBUG_GPIO_TOGGLE(pin) bl_gpio_toggle(pin)
#define DEBUG_GPIO_SET(pin) bl_gpio_set(pin)
#define DEBUG_GPIO_CLEAR(pin) bl_gpio_clear(pin)
#else
// No-op when DEBUG is not defined
#define DEBUG_GPIO_TOGGLE(pin) ((void)0))
#define DEBUG_GPIO_SET(pin) ((void)0))
#define DEBUG_GPIO_CLEAR(pin) ((void)0))
#endif // DEBUG

//=========================== defines =========================================

#define BLINK_BACKOFF_N_MIN 5
#define BLINK_BACKOFF_N_MAX 9

typedef struct {
    uint64_t node_id;
    uint64_t asn;
} bl_received_from_node_t;

typedef struct {
    bl_assoc_state_t state;
    bl_event_cb_t blink_event_callback;

    // node
    uint32_t last_received_from_gateway_asn; ///< Last received packet when in joined state
    int16_t backoff_n;
    uint8_t backoff_random_time; ///< Number of slots to wait before re-trying to join

    // gateway
    // NOTE: this could be improved by merging with the scheduler table
    // however, the scheduler table already uses a lot of memory because everything is hardcoded
    bl_received_from_node_t last_received_from_node[BLINK_MAX_NODES];
} assoc_vars_t;

//=========================== variables =======================================

assoc_vars_t assoc_vars = { 0 };

//=========================== prototypes ======================================


//=========================== public ==========================================

void bl_assoc_init(bl_event_cb_t event_callback) {
#ifdef DEBUG
    bl_gpio_init(&led0, BL_GPIO_OUT);
    bl_gpio_init(&led1, BL_GPIO_OUT);
    bl_gpio_init(&led2, BL_GPIO_OUT);
    bl_gpio_init(&led3, BL_GPIO_OUT);
    // remember: the LEDs are active low
#endif

    assoc_vars.blink_event_callback = event_callback;

    bl_assoc_set_state(JOIN_STATE_IDLE);
    bl_assoc_node_reset_backoff();
}

inline void bl_assoc_set_state(bl_assoc_state_t state) {
    assoc_vars.state = state;

#ifdef DEBUG
    DEBUG_GPIO_SET(&led0); DEBUG_GPIO_SET(&led1); DEBUG_GPIO_SET(&led2); DEBUG_GPIO_SET(&led3);
    switch (state) {
        case JOIN_STATE_IDLE:
            // DEBUG_GPIO_CLEAR(&pin1);
            break;
        case JOIN_STATE_SCANNING:
            // DEBUG_GPIO_SET(&pin1);
            DEBUG_GPIO_CLEAR(&led0);
            break;
        case JOIN_STATE_SYNCED:
            // DEBUG_GPIO_CLEAR(&pin1);
            DEBUG_GPIO_CLEAR(&led1);
            break;
        case JOIN_STATE_JOINING:
            DEBUG_GPIO_CLEAR(&led2);
            break;
        case JOIN_STATE_JOINED:
            DEBUG_GPIO_CLEAR(&led3);
            break;
        default:
            break;
    }
#endif
}

// ------------ node functions ------------

bl_assoc_state_t bl_assoc_get_state(void) {
    return assoc_vars.state;
}

bool bl_assoc_is_joined(void) {
    return assoc_vars.state == JOIN_STATE_JOINED;
}

bool bl_assoc_node_ready_to_join(void) {
    return assoc_vars.state == JOIN_STATE_SYNCED && assoc_vars.backoff_random_time == 0;
}

bool bl_assoc_node_gateway_is_lost(uint32_t asn) {
    return (asn - assoc_vars.last_received_from_gateway_asn) > bl_scheduler_get_active_schedule_slot_count() * BLINK_MAX_SLOTFRAMES_NO_RX_LEAVE;
}

void bl_assoc_node_keep_gateway_alive(uint64_t asn) {
    assoc_vars.last_received_from_gateway_asn = asn;
}

void bl_assoc_node_reset_backoff(void) {
    assoc_vars.backoff_n = -1;
    assoc_vars.backoff_random_time = 0;
}

void bl_assoc_node_tick_backoff(void) {
    if (assoc_vars.backoff_random_time > 0) {
        assoc_vars.backoff_random_time--;
    }
}

void bl_assoc_node_register_collision_backoff(void) {
    if (assoc_vars.backoff_n == -1) {
        // initialize backoff
        assoc_vars.backoff_n = BLINK_BACKOFF_N_MIN;
    } else {
        // increment the n in [0, 2^n - 1], but only if n is less than the max
        uint8_t new_n = assoc_vars.backoff_n + 1;
        assoc_vars.backoff_n = new_n < BLINK_BACKOFF_N_MAX ? new_n : BLINK_BACKOFF_N_MAX;
    }
    // choose a random number from [0, 2^n - 1] and set it as the backoff time
    uint16_t max = (1 << assoc_vars.backoff_n) - 1;
    bl_rng_read_range(&assoc_vars.backoff_random_time, 0, max);
}

// ------------ gateway functions ---------

bool bl_assoc_gateway_pending_join_response(void) {
    return assoc_vars.state == JOIN_STATE_JOINING;
}

bool bl_assoc_gateway_node_is_joined(uint64_t node_id) {
    for (size_t i = 0; i < BLINK_MAX_NODES; i++) {
        if (assoc_vars.last_received_from_node[i].node_id == node_id) {
            return true;
        }
    }
    return false;
}

bool bl_assoc_gateway_keep_node_alive(uint64_t node_id, uint64_t asn) {
    // save the node_id and asn
    // if the node_id is already in the list, update the asn
    // otherwise, add it to the first empty spot
    for (size_t i = 0; i < BLINK_MAX_NODES; i++) {
        if (assoc_vars.last_received_from_node[i].node_id == node_id) {
            assoc_vars.last_received_from_node[i].asn = asn;
            return true;
        }
        if (assoc_vars.last_received_from_node[i].node_id == 0) {
            assoc_vars.last_received_from_node[i].node_id = node_id;
            assoc_vars.last_received_from_node[i].asn = asn;
            return true;
        }
    }
    // should never reach here
    return false;
}

void bl_assoc_gateway_clear_old_nodes(uint64_t asn) {
    // clear all nodes that have not been heard from in the last N asn
    // also deassign the cells from the scheduler
    uint64_t max_asn_old = bl_scheduler_get_active_schedule_slot_count() * BLINK_MAX_SLOTFRAMES_NO_RX_LEAVE;
    for (size_t i = 0; i < BLINK_MAX_NODES; i++) {
        bl_received_from_node_t *node = &assoc_vars.last_received_from_node[i];
        if (node->node_id != 0 && asn - node->asn > max_asn_old) {
            bl_event_data_t event_data = (bl_event_data_t){ .data.node_info.node_id = node->node_id, .tag = BLINK_PEER_LOST };
            // deassign the cell
            bl_scheduler_deassign_uplink_cell(node->node_id);
            // clear the node
            node->node_id = 0;
            node->asn = 0;
            // inform the application
            assoc_vars.blink_event_callback(BLINK_NODE_LEFT, event_data);
        }
    }
}

// ------------ packet handlers -------

void bl_assoc_handle_beacon(uint8_t *packet, uint8_t length, uint8_t channel, uint32_t ts) {
    (void)length;

    if (packet[1] != BLINK_PACKET_BEACON) {
        return;
    }

    // now that we know it's a beacon packet, parse and process it
    bl_beacon_packet_header_t *beacon = (bl_beacon_packet_header_t *)packet;

    if (beacon->version != BLINK_PROTOCOL_VERSION) {
        // ignore packet with different protocol version
        return;
    }

    if (beacon->remaining_capacity == 0) { // TODO: what if I am joined to this gateway? add a check for it.
        // this gateway is full, ignore it
        return;
    }

    // save this scan info
    bl_scan_add(*beacon, bl_radio_rssi(), channel, ts, 0); // asn not used anymore during scan

    return;
}

//=========================== callbacks =======================================

//=========================== private =========================================
