#ifndef __ASSOCIATION_H
#define __ASSOCIATION_H

/**
 * @file
 * @ingroup     blink_assoc
 *
 * @brief       Association procedure for the blink protocol
 *
 * @author Geovane Fedrecheski <geovane.fedrecheski@inria.fr>
 *
 * @copyright Inria, 2024-now
 */

#include <stdint.h>
#include <stdbool.h>

#include "models.h"

//=========================== defines ==========================================

typedef enum {
    JOIN_STATE_IDLE = 1,
    JOIN_STATE_SCANNING = 2,
    JOIN_STATE_SYNCED = 4,
    JOIN_STATE_JOINING = 8,
    JOIN_STATE_JOINED = 16,
} bl_assoc_state_t;

//=========================== variables ========================================

//=========================== prototypes =======================================

void bl_assoc_init(bl_event_cb_t event_callback);
void bl_assoc_set_state(bl_assoc_state_t join_state);
bl_assoc_state_t bl_assoc_get_state(void);
bool bl_assoc_is_joined(void);
void bl_assoc_handle_beacon(uint8_t *packet, uint8_t length, uint8_t channel, uint32_t ts);
void bl_assoc_handle_packet(uint8_t *packet, uint8_t length);

bool bl_assoc_node_ready_to_join(void);
bool bl_assoc_node_gateway_is_lost(uint32_t asn);
void bl_assoc_node_keep_gateway_alive(uint64_t asn);

bool bl_assoc_gateway_node_is_joined(uint64_t node_id);

bool bl_assoc_gateway_keep_node_alive(uint64_t node_id, uint64_t asn);
void bl_assoc_gateway_clear_old_nodes(uint64_t asn);

#endif // __ASSOCIATION_H
