/**
 * @file
 * @ingroup     blink
 *
 * @brief       Implementation of the blink protocol
 *
 * @author Geovane Fedrecheski <geovane.fedrecheski@inria.fr>
 *
 * @copyright Inria, 2024
 */

 #include <nrf.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "bl_device.h"
#include "models.h"
#include "packet.h"
#include "mac.h"
#include "scheduler.h"
#include "association.h"
#include "queue.h"
#include "blink.h"

//=========================== defines ==========================================

typedef struct {
    bl_node_type_t          node_type;
    bl_event_cb_t           app_event_callback;

    // gateway only
    uint64_t                joined_nodes[BLINK_MAX_NODES];
    uint8_t                 joined_nodes_len;
} blink_vars_t;

//=========================== variables ========================================

static blink_vars_t _blink_vars = { 0 };

//=========================== prototypes =======================================

static void event_callback(bl_event_t event, bl_event_data_t event_data);

//=========================== public ===========================================
// in this library, user-facing functions begin with blink_*, while internal functions begin with bl_*

// -------- common --------

void blink_init(bl_node_type_t node_type, schedule_t *app_schedule, bl_event_cb_t app_event_callback) {
    _blink_vars.node_type = node_type;
    _blink_vars.app_event_callback = app_event_callback;

    bl_assoc_init(event_callback);
    bl_scheduler_init(node_type, app_schedule);
    bl_mac_init(node_type, event_callback);
}

void blink_tx(uint8_t *packet, uint8_t length) {
    bl_queue_add(packet, length);
}

bl_node_type_t blink_get_node_type(void) {
    return _blink_vars.node_type;
}

void blink_set_node_type(bl_node_type_t node_type) {
    _blink_vars.node_type = node_type;
}

// -------- gateway ----------

size_t blink_gateway_get_nodes(uint64_t *nodes) {
    return bl_scheduler_get_nodes(nodes);
}

size_t blink_gateway_count_nodes(void) {
    return bl_scheduler_get_nodes_count();
}

// -------- node ----------

void blink_node_tx_payload(uint8_t *payload, uint8_t payload_len) {
    uint8_t packet[BLINK_PACKET_MAX_SIZE] = { 0 };
    uint8_t len = bl_build_packet_data(packet, blink_node_gateway_id(), payload, payload_len);
    bl_queue_add(packet, len);
}

bool blink_node_is_connected(void) {
    return bl_assoc_is_joined();
}

uint64_t blink_node_gateway_id(void) {
    return bl_mac_get_synced_gateway();
}

//=========================== iternal api =====================================

void bl_handle_packet(uint8_t *packet, uint8_t length) {
    bl_packet_header_t *header = (bl_packet_header_t *)packet;

    // check here if node received a packet other than Join Response (or Beacon) during state JOINING
    // if so, assume that the Join Request collided with another packet during the last Shared Uplink slot
    if (
        _blink_vars.node_type == BLINK_NODE && bl_assoc_get_state() == JOIN_STATE_JOINING &&
        !(header->type & (BLINK_PACKET_JOIN_RESPONSE | BLINK_PACKET_BEACON))
       ) {
        // assume a collision happened, so update the backoff window accordingly
        bl_assoc_node_register_collision_backoff();
        return;
    }

    if (header->dst != bl_device_id() && header->dst != BLINK_BROADCAST_ADDRESS && header->type != BLINK_PACKET_BEACON) {
        // ignore packets that are not for me, not broadcast, and not a beacon
        return;
    }

    if (blink_get_node_type() == BLINK_GATEWAY) {
        bool from_joined_node = bl_assoc_gateway_node_is_joined(header->src);

        switch (header->type) {
            case BLINK_PACKET_JOIN_REQUEST: {
                if (from_joined_node) {
                    // already joined, ignore
                    return;
                }
                // try to assign a cell to the node
                int16_t cell_id = bl_scheduler_assign_next_available_uplink_cell(header->src);
                if (cell_id >= 0) {
                    bl_queue_set_join_response(header->src, (uint8_t)cell_id);
                    _blink_vars.app_event_callback(BLINK_NODE_JOINED, (bl_event_data_t){ .data.node_info.node_id = header->src });
                    bl_assoc_gateway_keep_node_alive(header->src, bl_mac_get_asn()); // initialize this node's keep-alive
                } else {
                    _blink_vars.app_event_callback(BLINK_ERROR, (bl_event_data_t){ 0 });
                }
                break;
            }
            case BLINK_PACKET_DATA: {
                if (!from_joined_node) {
                    // ignore packets from nodes that are not joined
                    return;
                }
                // send the packet to the application
                bl_event_data_t event_data = {
                    .data.new_packet = {
                        .len = length,
                        .header = header,
                        .payload = packet + sizeof(bl_packet_header_t),
                        .payload_len = length - sizeof(bl_packet_header_t)
                    }
                };
                _blink_vars.app_event_callback(BLINK_NEW_PACKET, event_data);
                bl_assoc_gateway_keep_node_alive(header->src, bl_mac_get_asn()); // keep track of when the last packet was received
                break;
            }
            case BLINK_PACKET_KEEPALIVE:
                if (!from_joined_node) {
                    // ignore packets from nodes that are not joined
                    return;
                }
                bl_assoc_gateway_keep_node_alive(header->src, bl_mac_get_asn()); // keep track of when the last packet was received
                break;
            default:
                break;
        }

    } else if (blink_get_node_type() == BLINK_NODE) {
        bool from_my_gateway = header->src == bl_mac_get_synced_gateway() && bl_assoc_get_state() == JOIN_STATE_JOINED;

        switch (header->type) {
            case BLINK_PACKET_BEACON:
                // bl_assoc_handle_beacon(packet, length, BLINK_FIXED_SCAN_CHANNEL, bl_mac_get_asn());
                if (from_my_gateway) {
                    bl_assoc_handle_beacon(packet, length, BLINK_FIXED_SCAN_CHANNEL, bl_mac_get_asn());
                    bl_assoc_node_keep_gateway_alive(bl_mac_get_asn());
                } else {
                    bl_assoc_handle_beacon(packet, length, BLINK_FIXED_SCAN_CHANNEL, bl_mac_get_asn());
                }
                break;
            case BLINK_PACKET_JOIN_RESPONSE: {
                if (bl_assoc_get_state() != JOIN_STATE_JOINING) {
                    // ignore if not in the JOINING state
                    return;
                }
                // the first byte after the header contains the cell_id
                uint8_t cell_id = packet[sizeof(bl_packet_header_t)];
                if (bl_scheduler_assign_myself_to_cell(cell_id)) {
                    bl_assoc_set_state(JOIN_STATE_JOINED);
                    bl_event_data_t event_data = { .data.gateway_info.gateway_id = header->src };
                    _blink_vars.app_event_callback(BLINK_CONNECTED, event_data);
                    bl_assoc_node_keep_gateway_alive(bl_mac_get_asn()); // initialize the gateway's keep-alive
                    bl_assoc_node_reset_backoff();
                } else {
                    _blink_vars.app_event_callback(BLINK_ERROR, (bl_event_data_t){ 0 });
                }
                break;
            }
            case BLINK_PACKET_DATA: {
                if (!from_my_gateway) {
                    // ignore data packets from other gateways
                    return;
                }
                // send the packet to the application
                bl_event_data_t event_data = {
                    .data.new_packet = {
                        .len = length,
                        .header = header,
                        .payload = packet + sizeof(bl_packet_header_t),
                        .payload_len = length - sizeof(bl_packet_header_t)
                    }
                };
                _blink_vars.app_event_callback(BLINK_NEW_PACKET, event_data);
                bl_assoc_node_keep_gateway_alive(bl_mac_get_asn());
                break;
            }
            case BLINK_PACKET_KEEPALIVE:
                if (!from_my_gateway) {
                    // ignore keep-alives from other gateways
                    return;
                }
                bl_assoc_node_keep_gateway_alive(bl_mac_get_asn());
                break;
            default:
                break;
        }
    }
}

//=========================== callbacks ===========================================

static void event_callback(bl_event_t event, bl_event_data_t event_data) {
    // NOTE: this intermediate callback is rather useless now, but it could be used to store debug info, i.e, number of packets received, etc.
    if (_blink_vars.app_event_callback) {
        _blink_vars.app_event_callback(event, event_data);
    }
}
