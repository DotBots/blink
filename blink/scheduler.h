#ifndef __SCHEDULER_H
#define __SCHEDULER_H

/**
 * @defgroup    net_scheduler      SCHEDULER radio driver
 * @ingroup     drv
 * @brief       Driver for the TSCH scheduler
 *
 * @{
 * @file
 * @author Geovane Fedrecheski <geovane.fedrecheski@inria.fr>
 * @copyright Inria, 2024-now
 * @}
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <nrf.h>

#include "mac.h"

//=========================== defines ==========================================

#define BLINK_BACKOFF_N_MIN 5
#define BLINK_BACKOFF_N_MAX 9

#define BLINK_N_BLE_REGULAR_CHANNELS 37
#define BLINK_N_BLE_ADVERTISING_CHANNELS 3

// #ifndef BLINK_FIXED_CHANNEL
#define BLINK_FIXED_CHANNEL 37 // to hardcode the channel, use a valid value other than 0
// #endif

#define BLINK_N_CELLS_MAX 137

#define BLINK_ENABLE_BACKGROUND_SCAN 0

//=========================== variables ========================================

typedef struct {
    slot_type_t type;
    uint8_t channel_offset;
    uint64_t assigned_node_id;
} cell_t;

typedef struct {
    uint8_t id; // unique identifier for the schedule
    uint8_t max_nodes; // maximum number of nodes that can be scheduled, equivalent to the number of uplink slot_durations
    uint8_t backoff_n_min; // minimum exponent for the backoff algorithm
    uint8_t backoff_n_max; // maximum exponent for the backoff algorithm
    size_t n_cells; // number of cells in this schedule
    cell_t cells[BLINK_N_CELLS_MAX]; // cells in this schedule. NOTE(FIXME?): the first 3 cells must be beacons
} schedule_t;

//=========================== prototypes ==========================================

/**
 * @brief Initializes the scheduler
 *
 * If a schedule is not set, the first schedule in the available_schedules array is used.
 *
 * @param[in] schedule         Schedule to be used.
 */
void bl_scheduler_init(bl_node_type_t node_type, schedule_t *application_schedule);

/**
 * @brief Advances the schedule by one cell/slot.
 *
 * @return A configuration for the TSCH radio driver to follow in the next slot.
 */
bl_slot_info_t bl_scheduler_tick(uint64_t asn);

/**
 * @brief Activates a given schedule.
 *
 * This can be used at runtime to change the schedule, for example after receiving a beacon with a different schedule id.
 *
 * @param[in] schedule_id         Schedule ID
 *
 * @return true if the schedule was successfully set, false otherwise
 */
bool bl_scheduler_set_schedule(uint8_t schedule_id);

/**
 * @brief Assigns the next available uplink cell to a given node.
 *
 * @param[in] node_id         Node ID
 *
 * @return true if the uplink cell was successfully assigned, false otherwise (e.g., all uplink cells are already assigned)
 */
bool bl_scheduler_assign_next_available_uplink_cell(uint64_t node_id);

bool bl_scheduler_assign_myself_to_cell(uint8_t cell_index);

/**
 * @brief Deassigns the uplink cell assigned to a given node.
 *
 * @param[in] node_id         Node ID
 *
 * @return true if the uplink cell was successfully deassigned, false otherwise
 */
bool bl_scheduler_deassign_uplink_cell(uint64_t node_id);

/**
 * @brief Computes the channel to be used in a given slot.
 *
 * @param[in] slot_type         Type of slot
 * @param[in] asn               Absolute Slot Number
 * @param[in] channel_offset    Channel offset
 *
 * @return Channel to be used in the given slot
 *
 */
uint8_t bl_scheduler_get_channel(slot_type_t slot_type, uint64_t asn, uint8_t channel_offset);

uint8_t bl_scheduler_get_active_schedule_id(void);

#endif
