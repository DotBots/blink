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
#include <stdbool.h>
#include <nrf.h>

#include "models.h"

//=========================== defines ==========================================

#define BLINK_BACKOFF_N_MIN 5
#define BLINK_BACKOFF_N_MAX 9

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

int16_t bl_scheduler_assign_next_available_uplink_cell(uint64_t node_id);

bool bl_scheduler_assign_myself_to_cell(uint16_t cell_index);

bool bl_scheduler_deassign_uplink_cell(uint64_t node_id);

uint8_t bl_scheduler_remaining_capacity(void);

uint8_t bl_scheduler_get_active_schedule_slot_count(void);

cell_t bl_scheduler_node_peek_slot(uint64_t asn);

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
