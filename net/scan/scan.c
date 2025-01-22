/**
 * @file
 * @ingroup     net_scan_list
 *
 * @brief       Scan list implementation
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

#include "scan.h"
#include "timer_hf.h"

//=========================== variables =======================================
typedef struct {
    dl_scan_t scans[BLINK_MAX_SCAN_LIST_SIZE];
} scan_vars_t;

scan_vars_t scan_vars = { 0 };

//=========================== prototypes ======================================

void _save_rssi(size_t idx, int8_t rssi, uint8_t channel, uint32_t ts_now);
uint32_t _get_ts_latest(dl_scan_t scan);
bool _scan_is_too_old(dl_scan_t scan, uint32_t ts_now);

//=========================== public ===========================================

// this function is a bit more complicated than it needed to be, but by
// inserting the rssi reading in a smart way, later it beacomes very easy and efficient to compute the average rssi
void dl_scan_add(uint64_t gateway_id, int8_t rssi, uint8_t channel, uint32_t ts_now) {
    bool found = false;
    int16_t empty_spot_idx = -1;
    uint32_t ts_oldest_all = ts_now;
    uint32_t ts_oldest_all_idx = 0;
    for (size_t i = 0; i < BLINK_MAX_SCAN_LIST_SIZE; i++) {
        // if found this gateway_id, update its respective rssi entry and mark as found.
        if (scan_vars.scans[i].gateway_id == gateway_id) {
            _save_rssi(i, rssi, channel, ts_now);
            found = true;
            continue;
        }

        // if newest rssi entry is too old, remove it (just set gateway_id = 0)
        if (_scan_is_too_old(scan_vars.scans[i], ts_now)) {
            // scan_vars.scans[i].gateway_id = 0;
            memset(&scan_vars.scans[i], 0, sizeof(dl_scan_t));
        }

        // try and save the first empty spot we see
        // if gateway_id == 0, there is an empty spot here, save the index (will only do this once)
        if (scan_vars.scans[i].gateway_id == 0 && empty_spot_idx < 0) {
            empty_spot_idx = i;
        }

        uint32_t ts_cmp = _get_ts_latest(scan_vars.scans[i]);
        if (scan_vars.scans[i].gateway_id != 0 && ts_cmp < ts_oldest_all) {
            ts_oldest_all = ts_cmp;
            ts_oldest_all_idx = i;
        }
    }
    // if found a matching gateway_id, nothing else to do
    if (found) return;

    // either save onto empty spot or overwrite the latest one
    if (empty_spot_idx >= 0) { // there is an empty spot
        scan_vars.scans[empty_spot_idx].gateway_id = gateway_id;
        _save_rssi(empty_spot_idx, rssi, channel, ts_now);
    } else {
        // last case: didn't match the gateeway_id, and didn't find an empty slot,
        // so overwrite on top of the oldest reading
        memset(&scan_vars.scans[ts_oldest_all_idx], 0, sizeof(dl_scan_t));
        scan_vars.scans[ts_oldest_all_idx].gateway_id = gateway_id;
        _save_rssi(ts_oldest_all_idx, rssi, channel, ts_now);
    }
}

uint64_t dl_scan_select(uint32_t ts_now) {
    // compute the average rssi for each gateway, and save the highest one
    uint64_t best_gateway_id = 0;
    int8_t best_gateway_rssi = INT8_MIN;
    for (size_t i = 0; i < BLINK_MAX_SCAN_LIST_SIZE; i++) {
        if (scan_vars.scans[i].gateway_id == 0) {
            continue;
        }
        // compute average rssi, only including the rssi readings that are not too old
        int8_t avg_rssi = 0;
        int8_t n_rssi = 0;
        for (size_t j = 0; j < DOTLINK_N_BLE_ADVERTISING_FREQUENCIES; j++) {
            if (scan_vars.scans[i].rssi[j].timestamp == 0) { // no rssi reading here
                continue;
            }
            if (ts_now - scan_vars.scans[i].rssi[j].timestamp > BLINK_SCAN_OLD_US) { // rssi reading is too old
                continue;
            }
            avg_rssi += scan_vars.scans[i].rssi[j].rssi;
            n_rssi++;
        }
        if (n_rssi == 0) {
            continue;
        }
        avg_rssi /= n_rssi;
        if (avg_rssi > best_gateway_rssi) {
            best_gateway_rssi = avg_rssi;
            best_gateway_id = scan_vars.scans[i].gateway_id;
        }
    }
    return best_gateway_id;
}

//=========================== private ==========================================

void _save_rssi(size_t idx, int8_t rssi, uint8_t channel, uint32_t ts_now) {
    size_t channel_idx = (channel % DOTLINK_N_BLE_REGULAR_FREQUENCIES) - 1;
    scan_vars.scans[idx].rssi[channel_idx].rssi = rssi;
    scan_vars.scans[idx].rssi[channel_idx].timestamp = ts_now;
}

inline bool _scan_is_too_old(dl_scan_t scan, uint32_t ts_now) {
    uint32_t ts_latest = _get_ts_latest(scan);
    return (ts_now - ts_latest) > BLINK_SCAN_OLD_US;
}

inline uint32_t _get_ts_latest(dl_scan_t scan) {
    uint32_t latest = 0;
    for (size_t i = 0; i < DOTLINK_N_BLE_ADVERTISING_FREQUENCIES; i++) {
        if (scan.rssi[i].timestamp > latest) {
            latest = scan.rssi[i].timestamp;
        }
    }
    return latest;
}
