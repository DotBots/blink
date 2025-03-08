#ifndef __QUEUE_H
#define __QUEUE_H

#include <stdint.h>
#include <stdbool.h>

#include "models.h"

//=========================== defines =========================================

#define BLINK_PACKET_QUEUE_SIZE (8) // must be a power of 2

#define BLINK_AUTO_UPLINK_KEEPALIVE 1 // whether to send a keepalive packet when there is nothing to send

//=========================== prototypes ======================================

void bl_queue_add(uint8_t *packet, uint8_t length);
uint8_t bl_queue_next_packet(slot_type_t slot_type, uint8_t *packet);
uint8_t bl_queue_peek(uint8_t *packet);
bool bl_queue_pop(void);

// void bl_queue_set_join_packet(uint64_t node_id, bl_packet_type_t packet_type);
void bl_queue_set_join_request(uint64_t node_id);
void bl_queue_set_join_response(uint64_t node_id, uint8_t assigned_cell_id);

bool bl_queue_has_join_packet(void);
uint8_t bl_queue_get_join_packet(uint8_t *packet);

#endif // __QUEUE_H
