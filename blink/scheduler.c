/**
 * @file
 * @ingroup     net_scheduler
 *
 * @brief       The blink scheduler
 *
 * @author Geovane Fedrecheski <geovane.fedrecheski@inria.fr>
 *
 * @copyright Inria, 2024
 */
#include <nrf.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "scheduler.h"
#include "device.h"
#if defined(NRF5340_XXAA) && defined(NRF_NETWORK)
#include "ipc.h"
#endif

#include "all_schedules.c"

//=========================== defines ==========================================


//=========================== variables ========================================

typedef struct {
    bl_node_type_t node_type; // whether the node is a gateway or a dotbot

    // counters and indexes
    schedule_t *active_schedule_ptr; // pointer to the currently active schedule
    uint32_t slotframe_counter; // used to cycle beacon channels through slotframes (when listening for beacons at uplink slot_durations)

    // static data
    schedule_t available_schedules[BLINK_N_SCHEDULES];
    size_t available_schedules_len;
} schedule_vars_t;

static schedule_vars_t _schedule_vars = { 0 };

//========================== prototypes ========================================

// Compute the radio action when the node is a gateway
void _compute_gateway_action(cell_t cell, bl_slot_info_t *slot_info);

// Compute the radio action when the node is a dotbot
void _compute_dotbot_action(cell_t cell, bl_slot_info_t *slot_info);

//=========================== public ===========================================

void bl_scheduler_init(bl_node_type_t node_type, schedule_t *application_schedule) {
    _schedule_vars.node_type = node_type;

    if (_schedule_vars.available_schedules_len == BLINK_N_SCHEDULES) return; // FIXME: this is just to simplify debugging (allows calling init multiple times)

    _schedule_vars.available_schedules[_schedule_vars.available_schedules_len++] = schedule_only_beacons;
    _schedule_vars.available_schedules[_schedule_vars.available_schedules_len++] = schedule_only_beacons_optimized_scan;

    _schedule_vars.available_schedules[_schedule_vars.available_schedules_len++] = schedule_minuscule;
    _schedule_vars.available_schedules[_schedule_vars.available_schedules_len++] = schedule_tiny;
    _schedule_vars.available_schedules[_schedule_vars.available_schedules_len++] = schedule_huge;
    _schedule_vars.available_schedules[_schedule_vars.available_schedules_len++] = schedule_small;
    _schedule_vars.available_schedules[_schedule_vars.available_schedules_len++] = schedule_big;

    if (application_schedule != NULL) {
        _schedule_vars.available_schedules[_schedule_vars.available_schedules_len++] = *application_schedule;
        _schedule_vars.active_schedule_ptr = application_schedule;
    }
}

bool bl_scheduler_set_schedule(uint8_t schedule_id) {
    for (size_t i = 0; i < BLINK_N_SCHEDULES; i++) {
        if (_schedule_vars.available_schedules[i].id == schedule_id) {
            _schedule_vars.active_schedule_ptr = &_schedule_vars.available_schedules[i];
            return true;
        }
    }
    return false;
}

// to be called at the GATEWAY when processing a JOIN_REQUEST
int16_t bl_scheduler_assign_next_available_uplink_cell(uint64_t node_id) {
    for (size_t i = 0; i < _schedule_vars.active_schedule_ptr->n_cells; i++) {
        cell_t *cell = &_schedule_vars.active_schedule_ptr->cells[i];
        // normally the cell is available if empty, but it may also be that case that
        // the node just temporarily lost connection, so we can just re-assign the same cell_id
        if (cell->type == SLOT_TYPE_UPLINK && (cell->assigned_node_id == NULL || cell->assigned_node_id == node_id)) {
            cell->assigned_node_id = node_id;
            return i;
        }
    }
    return -1;
}

// to be called at the NODE when processing a JOIN_RESPONSE
bool bl_scheduler_assign_myself_to_cell(uint16_t cell_index) {
    for (size_t i = 0; i < _schedule_vars.active_schedule_ptr->n_cells; i++) {
        cell_t *cell = &_schedule_vars.active_schedule_ptr->cells[i];
        if (cell->type == SLOT_TYPE_UPLINK && i == cell_index) {
            cell->assigned_node_id = db_device_id();
            return true;
        }
    }
    return false;
}

// to be called at the GATEWAY when a node leaves
bool bl_scheduler_deassign_uplink_cell(uint64_t node_id) {
    for (size_t i = 0; i < _schedule_vars.active_schedule_ptr->n_cells; i++) {
        cell_t *cell = &_schedule_vars.active_schedule_ptr->cells[i];
        if (cell->type == SLOT_TYPE_UPLINK && cell->assigned_node_id == node_id) {
            cell->assigned_node_id = NULL;
            return true;
        }
    }
    return false;
}

uint8_t bl_scheduler_remaining_capacity(void) {
    uint8_t remaining_capacity = 0;
    for (size_t i = 0; i < _schedule_vars.active_schedule_ptr->n_cells; i++) {
        cell_t *cell = &_schedule_vars.active_schedule_ptr->cells[i];
        if (cell->type == SLOT_TYPE_UPLINK && cell->assigned_node_id == NULL) {
            remaining_capacity++;
        }
    }
    return remaining_capacity;
}

bl_slot_info_t bl_scheduler_tick(uint64_t asn) {
    // get the current cell
    size_t cell_index = asn % (_schedule_vars.active_schedule_ptr)->n_cells;
    cell_t cell = (_schedule_vars.active_schedule_ptr)->cells[cell_index];

    bl_slot_info_t slot_info = {
        .radio_action = BLINK_RADIO_ACTION_SLEEP,
        .channel = bl_scheduler_get_channel(cell.type, asn, cell.channel_offset),
        .type = cell.type, // FIXME: only for debugging, remove before merge
    };
    if (_schedule_vars.node_type == BLINK_GATEWAY) {
        _compute_gateway_action(cell, &slot_info);
    } else {
        _compute_dotbot_action(cell, &slot_info);
    }

    // if the slotframe wrapped, keep track of how many slotframes have passed (used to cycle beacon channels)
    if (asn != 0 && cell_index == 0) {
        _schedule_vars.slotframe_counter++;
    }

    return slot_info;
}

uint8_t bl_scheduler_get_channel(slot_type_t slot_type, uint64_t asn, uint8_t channel_offset) {
#if(BLINK_FIXED_CHANNEL != 0)
    (void)slot_type;
    (void)asn;
    (void)channel_offset;
    return BLINK_FIXED_CHANNEL;
#endif
    if (slot_type == SLOT_TYPE_BEACON) {
#ifdef BLINK_FIXED_SCAN_CHANNEL
        return BLINK_FIXED_SCAN_CHANNEL;
#else
        // special handling in case the cell is a beacon
        return BLINK_N_BLE_REGULAR_CHANNELS + (asn % BLINK_N_BLE_ADVERTISING_CHANNELS);
#endif
    } else {
        // As per RFC 7554:
        //   frequency = F {(ASN + channelOffset) mod nFreq}
        return (asn + channel_offset) % BLINK_N_BLE_REGULAR_CHANNELS;
    }
}

uint8_t bl_scheduler_get_active_schedule_id(void) {
    return _schedule_vars.active_schedule_ptr->id;
}

uint8_t bl_scheduler_get_active_schedule_slot_count(void) {
    return _schedule_vars.active_schedule_ptr->n_cells;
}

bool bl_scheduler_node_next_slot_will_sleep(uint64_t asn) {
    size_t cell_index = (asn + 1) % (_schedule_vars.active_schedule_ptr)->n_cells;
    cell_t cell = (_schedule_vars.active_schedule_ptr)->cells[cell_index];

    return cell.type == SLOT_TYPE_UPLINK && cell.assigned_node_id != db_device_id();
}

//=========================== private ==========================================

void _compute_gateway_action(cell_t cell, bl_slot_info_t *slot_info) {
    switch (cell.type) {
        case SLOT_TYPE_BEACON:
        case SLOT_TYPE_DOWNLINK:
            slot_info->radio_action = BLINK_RADIO_ACTION_TX;
            break;
        case SLOT_TYPE_SHARED_UPLINK:
        case SLOT_TYPE_UPLINK:
            slot_info->radio_action = BLINK_RADIO_ACTION_RX;
            break;
    }
}

void _compute_dotbot_action(cell_t cell, bl_slot_info_t *slot_info) {
    switch (cell.type) {
        case SLOT_TYPE_BEACON:
        case SLOT_TYPE_DOWNLINK:
            slot_info->radio_action = BLINK_RADIO_ACTION_RX;
            break;
        case SLOT_TYPE_SHARED_UPLINK:
            slot_info->radio_action = BLINK_RADIO_ACTION_TX;
            break;
        case SLOT_TYPE_UPLINK:
            if (cell.assigned_node_id == db_device_id()) {
                slot_info->radio_action = BLINK_RADIO_ACTION_TX;
            } else {
                slot_info->radio_action = BLINK_RADIO_ACTION_SLEEP;
            }
            break;
        default:
            break;
    }
}
