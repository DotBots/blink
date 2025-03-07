#ifndef __BLINK_H
#define __BLINK_H

#include <stdint.h>
#include <stdbool.h>
#include "models.h"
#include "protocol.h"
#include "scheduler.h"

//=========================== defines ==========================================

#define BLINK_MAX_NODES 10 // TODO: find a way to sync with the pre-stored schedules
#define BLINK_BROADCAST_ADDRESS 0xFFFFFFFFFFFFFFFF

//=========================== prototypes ==========================================

void bl_init(bl_node_type_t node_type, schedule_t *app_schedule, bl_event_cb_t app_event_callback);
void bl_tx(uint8_t *packet, uint8_t length);
void bl_get_joined_nodes(uint64_t *nodes, uint8_t *num_nodes);
bl_node_type_t bl_get_node_type(void);
void bl_set_node_type(bl_node_type_t node_type);

#endif // __BLINK_H
