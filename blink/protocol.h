#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#include <stdint.h>
#include <stdlib.h>
#include <nrf.h>

//=========================== defines ==========================================

#define BLINK_PROTOCOL_VERSION 1

//=========================== variables ========================================

typedef enum {
    BLINK_PACKET_BEACON = 1,
    BLINK_PACKET_JOIN_REQUEST = 2,
    BLINK_PACKET_JOIN_RESPONSE = 4,
    BLINK_PACKET_KEEPALIVE = 8,
    BLINK_PACKET_DATA = 16,
} bl_packet_type_t;

// general packet header
typedef struct __attribute__((packed)) {
    uint8_t           version;
    bl_packet_type_t  type;
    uint64_t          dst;
    uint64_t          src;
} bl_packet_header_t;

// beacon packet
typedef struct __attribute__((packed)) {
    uint8_t           version;
    bl_packet_type_t  type;
    uint64_t          asn;
    uint64_t          src;
    uint8_t           remaining_capacity;
    uint8_t           active_schedule_id;
} bl_beacon_packet_header_t;

//=========================== prototypes =======================================

size_t bl_build_packet_data(uint8_t *buffer, uint64_t dst, uint8_t *data, size_t data_len);

size_t bl_build_packet_join_request(uint8_t *buffer, uint64_t dst);

size_t bl_build_packet_join_response(uint8_t *buffer, uint64_t dst);

size_t bl_build_packet_keepalive(uint8_t *buffer, uint64_t dst);

size_t bl_build_packet_beacon(uint8_t *buffer, uint64_t asn, uint8_t remaining_capacity, uint8_t active_schedule_id);

#endif
