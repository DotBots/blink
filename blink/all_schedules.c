/**
 * @file
 * @ingroup     net_maclow
 *
 * @brief       Fixed schedules
 *
 * @author Geovane Fedrecheski <geovane.fedrecheski@inria.fr>
 *
 * @copyright Inria, 2024
 */
#include "scheduler.h"

#define BLINK_N_SCHEDULES 1 + 7 // account for the schedule that can be passed by the application during initialization

/* Schedule only for beacons. Used when scanning the network. */
schedule_t schedule_only_beacons = {
    .id = 0xBE,
    .max_nodes = 0,
    .backoff_n_min = 5,
    .backoff_n_max = 9,
    .n_cells = 3,
    .cells = {
        {'B', 0, NULL},
        {'B', 1, NULL},
        {'B', 2, NULL},
    }
};

/* Schedule only for beacons, used for network scanning, specifically when the BLINK_ENABLE_BACKGROUND_SCAN optimization is enabled. */
schedule_t schedule_only_beacons_optimized_scan = {
    .id = 0xBF,
    .max_nodes = 0,
    .backoff_n_min = 5,
    .backoff_n_max = 9,
    .n_cells = 1,
    .cells = {
        // the channel offset doesn't matter here
        {'U', 0, NULL},
    }
};

/* Schedule with 11 slot_durations, supporting up to 5 nodes */
schedule_t schedule_minuscule = {
    .id = 6,
    .max_nodes = 5,
    .backoff_n_min = 5,
    .backoff_n_max = 9,
    .n_cells = 11,
    .cells = {
        // Begin with beacon cells. They use their own channels and channel offsets.
        {'B', 0, NULL},
        {'B', 1, NULL},
        {'B', 2, NULL},
        // Continue with regular cells.
        {'S', 6, NULL},
        {'D', 3, NULL},
        {'U', 5, NULL},
        {'U', 1, NULL},
        {'D', 4, NULL},
        {'U', 0, NULL},
        {'U', 7, NULL},
        {'U', 2, NULL}
    }
};

/* Schedule with 17 slot_durations, supporting up to 11 nodes */
schedule_t schedule_tiny = {
    .id = 5,
    .max_nodes = 11,
    .backoff_n_min = 5,
    .backoff_n_max = 9,
    .n_cells = 17,
    .cells = {
        // Begin with beacon cells. They use their own channel offsets and frequencies.
        {'B', 0, NULL},
        {'B', 1, NULL},
        {'B', 2, NULL},
        // Continue with regular cells.
        {'S', 2, NULL},
        {'D', 5, NULL},
        {'U', 6, NULL},
        {'U', 13, NULL},
        {'U', 7, NULL},
        {'U', 0, NULL},
        {'D', 4, NULL},
        {'U', 10, NULL},
        {'U', 12, NULL},
        {'U', 1, NULL},
        {'U', 11, NULL},
        {'U', 8, NULL},
        {'U', 3, NULL},
        {'U', 9, NULL}
    }
};

/* Schedule with 41 slot_durations, supporting up to 29 nodes */
schedule_t schedule_small = {
    .id = 4,
    .max_nodes = 29,
    .backoff_n_min = 5,
    .backoff_n_max = 9,
    .n_cells = 41,
    .cells = {
        {'B', 0, NULL},
        {'B', 1, NULL},
        {'B', 2, NULL},
        {'S', 36, NULL},
        {'D', 20, NULL},
        {'U', 13, NULL},
        {'U', 27, NULL},
        {'U', 29, NULL},
        {'U', 9, NULL},
        {'D', 0, NULL},
        {'U', 4, NULL},
        {'U', 33, NULL},
        {'U', 3, NULL},
        {'U', 30, NULL},
        {'U', 31, NULL},
        {'S', 22, NULL},
        {'D', 15, NULL},
        {'U', 11, NULL},
        {'U', 16, NULL},
        {'U', 24, NULL},
        {'U', 21, NULL},
        {'D', 2, NULL},
        {'U', 19, NULL},
        {'U', 10, NULL},
        {'U', 25, NULL},
        {'U', 34, NULL},
        {'U', 14, NULL},
        {'S', 28, NULL},
        {'D', 32, NULL},
        {'U', 1, NULL},
        {'U', 5, NULL},
        {'U', 18, NULL},
        {'U', 7, NULL},
        {'D', 23, NULL},
        {'U', 12, NULL},
        {'U', 17, NULL},
        {'U', 6, NULL},
        {'U', 35, NULL},
        {'U', 8, NULL},
        {'U', 37, NULL},
        {'U', 26, NULL},
    }
};

/* Schedule with 101 slot_durations, supporting up to 74 nodes */
schedule_t schedule_big = {
    .id = 2,
    .max_nodes = 74,
    .backoff_n_min = 5,
    .backoff_n_max = 9,
    .n_cells = 101,
    .cells = {
        {'B', 0, NULL},
        {'B', 1, NULL},
        {'B', 2, NULL},
        {'S', 23, NULL},
        {'D', 74, NULL},
        {'U', 78, NULL},
        {'U', 97, NULL},
        {'U', 63, NULL},
        {'U', 32, NULL},
        {'D', 59, NULL},
        {'U', 21, NULL},
        {'U', 9, NULL},
        {'U', 48, NULL},
        {'U', 53, NULL},
        {'U', 79, NULL},
        {'S', 92, NULL},
        {'D', 71, NULL},
        {'U', 26, NULL},
        {'U', 81, NULL},
        {'U', 27, NULL},
        {'U', 89, NULL},
        {'D', 1, NULL},
        {'U', 56, NULL},
        {'U', 6, NULL},
        {'U', 46, NULL},
        {'U', 34, NULL},
        {'U', 19, NULL},
        {'S', 60, NULL},
        {'D', 15, NULL},
        {'U', 58, NULL},
        {'U', 72, NULL},
        {'U', 42, NULL},
        {'U', 41, NULL},
        {'D', 50, NULL},
        {'U', 73, NULL},
        {'U', 4, NULL},
        {'U', 55, NULL},
        {'U', 16, NULL},
        {'U', 90, NULL},
        {'S', 69, NULL},
        {'D', 7, NULL},
        {'U', 95, NULL},
        {'U', 24, NULL},
        {'U', 84, NULL},
        {'U', 33, NULL},
        {'D', 76, NULL},
        {'U', 94, NULL},
        {'U', 62, NULL},
        {'U', 93, NULL},
        {'U', 45, NULL},
        {'U', 83, NULL},
        {'S', 49, NULL},
        {'D', 13, NULL},
        {'U', 65, NULL},
        {'U', 39, NULL},
        {'U', 12, NULL},
        {'U', 67, NULL},
        {'D', 5, NULL},
        {'U', 36, NULL},
        {'U', 44, NULL},
        {'U', 10, NULL},
        {'U', 66, NULL},
        {'U', 88, NULL},
        {'S', 61, NULL},
        {'D', 47, NULL},
        {'U', 35, NULL},
        {'U', 87, NULL},
        {'U', 70, NULL},
        {'U', 2, NULL},
        {'D', 82, NULL},
        {'U', 17, NULL},
        {'U', 28, NULL},
        {'U', 14, NULL},
        {'U', 8, NULL},
        {'U', 22, NULL},
        {'S', 51, NULL},
        {'D', 91, NULL},
        {'U', 85, NULL},
        {'U', 68, NULL},
        {'U', 86, NULL},
        {'U', 80, NULL},
        {'D', 75, NULL},
        {'U', 25, NULL},
        {'U', 54, NULL},
        {'U', 57, NULL},
        {'U', 3, NULL},
        {'U', 38, NULL},
        {'S', 37, NULL},
        {'D', 20, NULL},
        {'U', 18, NULL},
        {'U', 64, NULL},
        {'U', 30, NULL},
        {'U', 31, NULL},
        {'D', 96, NULL},
        {'U', 11, NULL},
        {'U', 77, NULL},
        {'U', 29, NULL},
        {'U', 0, NULL},
        {'U', 43, NULL},
        {'U', 40, NULL},
        {'U', 52, NULL}
    }
};


/* Schedule with 137 slot_durations, supporting up to 101 nodes */
schedule_t schedule_huge = {
    .id = 1,
    .max_nodes = 101,
    .backoff_n_min = 5,
    .backoff_n_max = 9,
    .n_cells = 137,
    .cells = {
        {'B', 0, NULL},
        {'B', 1, NULL},
        {'B', 2, NULL},
        {'S', 9, NULL},
        {'D', 30, NULL},
        {'U', 33, NULL},
        {'U', 91, NULL},
        {'U', 43, NULL},
        {'U', 13, NULL},
        {'D', 103, NULL},
        {'U', 102, NULL},
        {'U', 83, NULL},
        {'U', 90, NULL},
        {'U', 0, NULL},
        {'U', 92, NULL},
        {'S', 11, NULL},
        {'D', 38, NULL},
        {'U', 59, NULL},
        {'U', 52, NULL},
        {'U', 114, NULL},
        {'U', 31, NULL},
        {'D', 7, NULL},
        {'U', 63, NULL},
        {'U', 104, NULL},
        {'U', 111, NULL},
        {'U', 53, NULL},
        {'U', 22, NULL},
        {'S', 130, NULL},
        {'D', 26, NULL},
        {'U', 80, NULL},
        {'U', 3, NULL},
        {'U', 125, NULL},
        {'U', 20, NULL},
        {'D', 65, NULL},
        {'U', 18, NULL},
        {'U', 96, NULL},
        {'U', 10, NULL},
        {'U', 37, NULL},
        {'U', 16, NULL},
        {'S', 101, NULL},
        {'D', 110, NULL},
        {'U', 12, NULL},
        {'U', 15, NULL},
        {'U', 55, NULL},
        {'U', 100, NULL},
        {'D', 123, NULL},
        {'U', 112, NULL},
        {'U', 40, NULL},
        {'U', 2, NULL},
        {'U', 21, NULL},
        {'U', 4, NULL},
        {'S', 47, NULL},
        {'D', 84, NULL},
        {'U', 58, NULL},
        {'U', 17, NULL},
        {'U', 60, NULL},
        {'U', 107, NULL},
        {'D', 49, NULL},
        {'U', 115, NULL},
        {'U', 126, NULL},
        {'U', 35, NULL},
        {'U', 36, NULL},
        {'U', 68, NULL},
        {'S', 93, NULL},
        {'D', 124, NULL},
        {'U', 79, NULL},
        {'U', 28, NULL},
        {'U', 14, NULL},
        {'U', 6, NULL},
        {'D', 72, NULL},
        {'U', 70, NULL},
        {'U', 86, NULL},
        {'U', 71, NULL},
        {'U', 81, NULL},
        {'U', 128, NULL},
        {'S', 97, NULL},
        {'D', 131, NULL},
        {'U', 45, NULL},
        {'U', 23, NULL},
        {'U', 50, NULL},
        {'U', 98, NULL},
        {'D', 106, NULL},
        {'U', 118, NULL},
        {'U', 77, NULL},
        {'U', 61, NULL},
        {'U', 8, NULL},
        {'U', 116, NULL},
        {'S', 108, NULL},
        {'D', 69, NULL},
        {'U', 119, NULL},
        {'U', 82, NULL},
        {'U', 74, NULL},
        {'U', 89, NULL},
        {'D', 99, NULL},
        {'U', 56, NULL},
        {'U', 109, NULL},
        {'U', 57, NULL},
        {'U', 46, NULL},
        {'U', 132, NULL},
        {'S', 44, NULL},
        {'D', 34, NULL},
        {'U', 39, NULL},
        {'U', 19, NULL},
        {'U', 85, NULL},
        {'U', 1, NULL},
        {'D', 27, NULL},
        {'U', 41, NULL},
        {'U', 5, NULL},
        {'U', 29, NULL},
        {'U', 32, NULL},
        {'U', 54, NULL},
        {'S', 25, NULL},
        {'D', 24, NULL},
        {'U', 120, NULL},
        {'U', 64, NULL},
        {'U', 117, NULL},
        {'U', 78, NULL},
        {'D', 94, NULL},
        {'U', 88, NULL},
        {'U', 127, NULL},
        {'U', 48, NULL},
        {'U', 87, NULL},
        {'U', 42, NULL},
        {'S', 75, NULL},
        {'D', 62, NULL},
        {'U', 51, NULL},
        {'U', 113, NULL},
        {'U', 73, NULL},
        {'U', 67, NULL},
        {'D', 121, NULL},
        {'U', 66, NULL},
        {'U', 122, NULL},
        {'U', 76, NULL},
        {'U', 95, NULL},
        {'U', 133, NULL},
        {'U', 105, NULL},
        {'U', 129, NULL}
    }
};
