#include <nrf.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "protocol.h"
#include "mac.h"
#include "scheduler.h"
#include "association.h"
#include "blink.h"
#include "queue.h"

//=========================== defines ==========================================

#define BLINK_PACKET_QUEUE_SIZE (8) // must be a power of 2

typedef struct {
    uint8_t length;
    uint8_t buffer[BLINK_PACKET_MAX_SIZE];
} blink_packet_t;

typedef struct {
    uint8_t         current;                            ///< Current position in the queue
    uint8_t         last;                               ///< Position of the last item added in the queue
    blink_packet_t  packets[BLINK_PACKET_QUEUE_SIZE];
} blink_packet_queue_t;

typedef struct {
    blink_packet_queue_t    packet_queue;
    blink_packet_t          join_packet;
} queue_vars_t;

//=========================== variables ========================================

static queue_vars_t queue_vars = { 0 };

//=========================== prototypes =======================================

//=========================== public ===========================================

uint8_t bl_queue_next_packet(slot_type_t slot_type, uint8_t *packet) {
    uint8_t len = 0;

    if (bl_get_node_type() == BLINK_GATEWAY) {
        if (slot_type == SLOT_TYPE_BEACON) {
            // prepare a beacon packet with current asn, remaining capacity and active schedule id
            len = bl_build_packet_beacon(
                packet,
                bl_mac_get_asn(),
                bl_scheduler_remaining_capacity(),
                bl_scheduler_get_active_schedule_id()
            );
        } else if (slot_type == SLOT_TYPE_DOWNLINK) {
            if (bl_queue_has_join_packet()) {
                len = bl_queue_get_join_packet(packet);
            } else {
                // load a packet from the queue, if any is available
                len = bl_queue_peek(packet);
                if (len) {
                    // actually pop the packet from the queue
                    bl_queue_pop();
                }
            }
        }
    } else if (bl_get_node_type() == BLINK_NODE) {
        if (slot_type == SLOT_TYPE_SHARED_UPLINK) {
            if (bl_assoc_node_ready_to_join()) {
                bl_assoc_set_state(JOIN_STATE_JOINING);
                len = bl_queue_get_join_packet(packet);
            }
        } else if (slot_type == SLOT_TYPE_UPLINK) {
            // load a packet from the queue, if any is available
            len = bl_queue_peek(packet);
            if (len) {
                // actually pop the packet from the queue
                bl_queue_pop();
            } else if (BLINK_AUTO_UPLINK_KEEPALIVE) {
                // send a keepalive packet
                len = bl_build_packet_keepalive(packet, bl_mac_get_synced_gateway());
            }
        }
    }

    return len;
}

void bl_queue_add(uint8_t *packet, uint8_t length) {
    // enqueue for transmission
    memcpy(queue_vars.packet_queue.packets[queue_vars.packet_queue.last].buffer, packet, length);
    queue_vars.packet_queue.packets[queue_vars.packet_queue.last].length = length;
    // increment the `last` index
    queue_vars.packet_queue.last = (queue_vars.packet_queue.last + 1) % BLINK_PACKET_QUEUE_SIZE;
}

uint8_t bl_queue_peek(uint8_t *packet) {
    if (queue_vars.packet_queue.current == queue_vars.packet_queue.last) {
        return 0;
    }

    memcpy(packet, queue_vars.packet_queue.packets[queue_vars.packet_queue.current].buffer, queue_vars.packet_queue.packets[queue_vars.packet_queue.current].length);
    // do not increment the `current` index here, as this is just a peek
    return queue_vars.packet_queue.packets[queue_vars.packet_queue.current].length;
}

bool bl_queue_pop(void) {
    if (queue_vars.packet_queue.current == queue_vars.packet_queue.last) {
        return false;
    } else {
        // increment the `current` index
        queue_vars.packet_queue.current = (queue_vars.packet_queue.current + 1) % BLINK_PACKET_QUEUE_SIZE;
        return true;
    }
}

void bl_queue_set_join_request(uint64_t node_id) {
    queue_vars.join_packet.length = bl_build_packet_join_request(queue_vars.join_packet.buffer, node_id);
}

void bl_queue_set_join_response(uint64_t node_id, uint8_t assigned_cell_id) {
    uint8_t len = bl_build_packet_join_response(queue_vars.join_packet.buffer, node_id);
    queue_vars.join_packet.buffer[len++] = assigned_cell_id;
    queue_vars.join_packet.length = len;
}

bool bl_queue_has_join_packet(void) {
    return queue_vars.join_packet.length > 0;
}

uint8_t bl_queue_get_join_packet(uint8_t *packet) {
    memcpy(packet, queue_vars.join_packet.buffer, queue_vars.join_packet.length);
    uint8_t len = queue_vars.join_packet.length;

    // clear the join request
    queue_vars.join_packet.length = 0;

    return len;
}
