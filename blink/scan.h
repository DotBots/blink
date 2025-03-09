#ifndef __SCAN_H
#define __SCAN_H

#include <nrf.h>
#include <stdint.h>
#include <stdbool.h>

#include "protocol.h"
#include "models.h"

//=========================== defines =========================================

#define BLINK_MAX_SCAN_LIST_SIZE (10)
#define BLINK_SCAN_OLD_US (1000*1000*3) // rssi reading considered old after 3 seconds
#define BLINK_HANDOVER_RSSI_HYSTERESIS (6) // hysteresis (in dBm) for handover

//=========================== variables =======================================

typedef struct {
    int8_t                      rssi;
    uint32_t                    timestamp;
    uint64_t                    captured_asn;
    bl_beacon_packet_header_t   beacon;
} bl_channel_info_t;

typedef struct {
    uint64_t            gateway_id;
    bl_channel_info_t   channel_info[BLINK_N_BLE_ADVERTISING_CHANNELS]; // channels 37, 38, 39
} bl_gateway_scan_t;

//=========================== prototypes ======================================

void bl_scan_add(bl_beacon_packet_header_t beacon, int8_t rssi, uint8_t channel, uint32_t ts_scan, uint64_t asn_scan);

bool bl_scan_select(bl_channel_info_t *best_channel_info, uint32_t ts_scan_started, uint32_t ts_scan_ended);

#endif // __SCAN_H
