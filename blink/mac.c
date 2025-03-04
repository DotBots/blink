/**
 * @file
 * @ingroup     net_mac
 *
 * @brief       Lower MAC driver for Blink
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

#include "blink.h"
#include "mac.h"
#include "scan.h"
#include "scheduler.h"
#include "radio.h"
#include "timer_hf.h"
#include "protocol.h"
#include "device.h"
#if defined(NRF5340_XXAA) && defined(NRF_NETWORK)
#include "ipc.h"
#endif

//=========================== debug ============================================

#ifndef DEBUG // FIXME: remove before merge. Just to make VS Code enable code behind `#ifdef DEBUG`
#define DEBUG
#endif

#ifdef DEBUG
#include "gpio.h" // for debugging
// pins connected to logic analyzer, variable names reflect the channel number
gpio_t pin0 = { .port = 1, .pin = 2 };
gpio_t pin1 = { .port = 1, .pin = 3 };
gpio_t pin2 = { .port = 1, .pin = 4 };
gpio_t pin3 = { .port = 1, .pin = 5 };
// the 4 LEDs of the DK are on port 0, pins 13, 14, 15, 16
gpio_t led0 = { .port = 0, .pin = 13 };
gpio_t led1 = { .port = 0, .pin = 14 };
gpio_t led2 = { .port = 0, .pin = 15 };
gpio_t led3 = { .port = 0, .pin = 16 };
#define DEBUG_GPIO_TOGGLE(pin) db_gpio_toggle(pin)
#define DEBUG_GPIO_SET(pin) db_gpio_set(pin)
#define DEBUG_GPIO_CLEAR(pin) db_gpio_clear(pin)
#else
// No-op when DEBUG is not defined
#define DEBUG_GPIO_TOGGLE(pin) ((void)0))
#define DEBUG_GPIO_SET(pin) ((void)0))
#define DEBUG_GPIO_CLEAR(pin) ((void)0))
#endif // DEBUG

//=========================== defines ==========================================

typedef enum {
    // common
    STATE_SLEEP,

    // scan
    STATE_SCAN_LISTEN = 1,
    STATE_SCAN_RX = 2,
    STATE_SCAN_PROCESS_PACKET = 3,
    STATE_SCAN_SYNC = 4,

    // sync
    STATE_SYNC_LISTEN = 11,
    STATE_SYNC_RX = 12,
    STATE_SYNC_PROCESS = 13,

    // transmitter
    STATE_TX_OFFSET = 21,
    STATE_TX_DATA = 22,

    // receiver
    STATE_RX_OFFSET = 31,
    STATE_RX_DATA_LISTEN = 32,
    STATE_RX_DATA = 33,

} bl_mac_state_t;

typedef enum {
    JOIN_STATE_IDLE = 1,
    JOIN_STATE_SCANNING = 2,
    JOIN_STATE_SYNCED = 4,
    JOIN_STATE_JOINING = 8,
    JOIN_STATE_JOINED = 16,
} bl_join_state_t;

typedef struct {
    uint8_t channel;
    int8_t rssi;
    uint32_t finished_ts;
    uint64_t captured_asn;
    uint8_t packet[BLINK_PACKET_MAX_SIZE];
    uint8_t packet_len;
} bl_received_packet_t;

typedef struct {
    // ---- common
    bl_node_type_t node_type; //< whether the node is a gateway or a dotbot
    uint64_t device_id; ///< Device ID

    bl_mac_state_t state; ///< State within the slot
    uint32_t start_slot_ts; ///< Timestamp of the start of the slot
    uint64_t asn; ///< Absolute slot number
    bl_slot_info_t current_slot_info; ///< Information about the current slot

    bl_rx_cb_t app_rx_callback; ///< Function pointer, stores the application callback

    bl_join_state_t join_state; ///< State regarding network join, from scan to sync to joined

    bl_received_packet_t received_packet; ///< Last received packet

    // SCANNING state
    uint8_t scan_max_slots; ///< Maximum number of slots to scan
    uint32_t scan_started_ts; ///< Timestamp of the start of the scan
    uint64_t scan_started_asn; ///< ASN when the scan started
    uint32_t current_scan_item_ts; ///< Timestamp of the current scan item
    bl_channel_info_t selected_channel_info;

    // SYNCED state
    uint32_t synced_ts; ///< Timestamp of the packet
    uint64_t synced_gateway; ///< ID of the gateway the node is synchronized with
    int8_t synced_gateway_rssi; ///< RSSI of the gateway the node is synchronized with

    // SYNC/JOINING state
    bool waiting_join_response;

    // JOINED state
    bool is_background_scanning; ///< Whether the node is scanning in the background
} mac_vars_t;

//=========================== variables ========================================

mac_vars_t mac_vars = { 0 };

bl_slot_durations_t slot_durations = {
    .tx_offset = BLINK_TS_TX_OFFSET,
    .tx_max = BLINK_PACKET_TOA_WITH_PADDING,

    .rx_guard = BLINK_RX_GUARD_TIME,
    .rx_offset = BLINK_TS_TX_OFFSET - BLINK_RX_GUARD_TIME,
    .rx_max = BLINK_RX_GUARD_TIME + BLINK_PACKET_TOA_WITH_PADDING, // same as rx_guard + tx_max

    .end_guard = BLINK_END_GUARD_TIME,

    .whole_slot = BLINK_WHOLE_SLOT_DURATION,
};

//=========================== prototypes =======================================

static inline void set_slot_state(bl_mac_state_t state);
static inline void set_join_state(bl_join_state_t join_state);

static void new_slot(void);
static void end_slot(void);
static void disable_radio_and_intra_slot_timers(void);

static void activity_ti1_or_ri1(void);

static void activity_ti1(void);
static void activity_ti2(void);
static void activity_tie1(void);
static void activity_ti3(void);

static void activity_ri1(void);
static void activity_ri2(void);
static void activity_ri3(uint32_t ts);
static void activity_rie1(void);
static void activity_ri4(uint32_t ts);
static void activity_rie2(void);

static void activity_scan_new_slot(void);
static void activity_scan_start_frame(uint32_t ts);
static void activity_scan_end_frame(uint32_t ts);
static void activity_scan_end_slot(void);
static bool select_gateway_and_sync(void);

static void isr_mac_radio_start_frame(uint32_t ts);
static void isr_mac_radio_end_frame(uint32_t ts);

//=========================== public ===========================================

void bl_mac_init(bl_node_type_t node_type, bl_rx_cb_t rx_callback) {
    (void)node_type;
    (void)rx_callback;
#ifdef DEBUG
    db_gpio_init(&pin0, DB_GPIO_OUT);
    db_gpio_init(&pin1, DB_GPIO_OUT);
    db_gpio_init(&pin2, DB_GPIO_OUT);
    db_gpio_init(&pin3, DB_GPIO_OUT);
    db_gpio_init(&led0, DB_GPIO_OUT);
    db_gpio_init(&led1, DB_GPIO_OUT);
    db_gpio_init(&led2, DB_GPIO_OUT);
    db_gpio_init(&led3, DB_GPIO_OUT);
#endif

    // initialize the high frequency timer
    bl_timer_hf_init(BLINK_TIMER_DEV);

    // initialize the radio
    bl_radio_init(&isr_mac_radio_start_frame, &isr_mac_radio_end_frame, DB_RADIO_BLE_2MBit);

    // node stuff
    mac_vars.node_type = node_type;
    mac_vars.device_id = db_device_id();

    // scan stuff
    mac_vars.scan_max_slots = BLINK_SCAN_MAX_SLOTS;

    // synchronization stuff
    mac_vars.asn = 0;

    // join stuff
    set_join_state(JOIN_STATE_IDLE);
    mac_vars.is_background_scanning = false;

    // application callback
    mac_vars.app_rx_callback = rx_callback;

    // begin the slot
    set_slot_state(STATE_SLEEP);
    new_slot();
}

//=========================== private ==========================================

static void set_slot_state(bl_mac_state_t state) {
    mac_vars.state = state;

    if (mac_vars.join_state == JOIN_STATE_SCANNING) {
        switch (state) {
            case STATE_SCAN_LISTEN:
                DEBUG_GPIO_SET(&pin3);
                break;
            case STATE_SCAN_RX:
                break;
            case STATE_SLEEP:
                DEBUG_GPIO_CLEAR(&pin3);
                break;
            default:
                break;
        }
        return;
    }

    switch (state) {
        case STATE_RX_DATA_LISTEN:
            // DEBUG_GPIO_SET(&pin3);
        case STATE_TX_DATA:
        case STATE_RX_DATA:
            DEBUG_GPIO_SET(&pin1);
            break;
        case STATE_SLEEP:
            DEBUG_GPIO_CLEAR(&pin1);
            // DEBUG_GPIO_CLEAR(&pin3);
            break;
        default:
            break;
    }
}

static inline void set_join_state(bl_join_state_t join_state) {
    mac_vars.join_state = join_state;

#ifdef DEBUG
    // remember: the LEDs are active low
    DEBUG_GPIO_SET(&led0); DEBUG_GPIO_SET(&led1); DEBUG_GPIO_SET(&led2); DEBUG_GPIO_SET(&led3);
    switch (join_state) {
        case JOIN_STATE_IDLE:
            DEBUG_GPIO_CLEAR(&pin1);
            break;
        case JOIN_STATE_SCANNING:
            DEBUG_GPIO_SET(&pin1);
            DEBUG_GPIO_CLEAR(&led0);
            break;
        case JOIN_STATE_SYNCED:
            DEBUG_GPIO_CLEAR(&pin1);
            DEBUG_GPIO_CLEAR(&led1);
            break;
        case JOIN_STATE_JOINING:
            DEBUG_GPIO_CLEAR(&led2);
            break;
        case JOIN_STATE_JOINED:
            DEBUG_GPIO_CLEAR(&led3);
            if (mac_vars.is_background_scanning) {
                DEBUG_GPIO_CLEAR(&led0);
            }
            break;
        default:
            break;
    }
#endif
}

static void new_slot(void) {
    mac_vars.start_slot_ts = bl_timer_hf_now(BLINK_TIMER_DEV);

    // NOTE: for some reason, the node slot tick is either perfetcly synced with the gateway, or 10 us off
    //       and it depends on whether the devices are being debugged or not
    int32_t node_correction = 0;//mac_vars.node_type == BLINK_GATEWAY ? 0 : -10;

    // set the timer for the next slot
    bl_timer_hf_set_oneshot_with_ref_us(
        BLINK_TIMER_DEV,
        BLINK_TIMER_INTER_SLOT_CHANNEL,
        mac_vars.start_slot_ts,
        slot_durations.whole_slot + node_correction,
        &new_slot
    );

    DEBUG_GPIO_SET(&pin0); DEBUG_GPIO_CLEAR(&pin0);
    if (mac_vars.join_state > JOIN_STATE_SCANNING) { DEBUG_GPIO_CLEAR(&pin1); DEBUG_GPIO_CLEAR(&pin2); DEBUG_GPIO_CLEAR(&pin3); }

    mac_vars.current_slot_info = bl_scheduler_tick(mac_vars.asn);

    // if (mac_vars.node_type == BLINK_NODE && (mac_vars.join_state & (JOIN_STATE_SYNCED | JOIN_STATE_JOINING | JOIN_STATE_JOINED))) {
    //     if (mac_vars.start_slot_ts - mac_vars.received_packet.finished_ts > BLINK_MAX_TIME_NO_RX_DESYNC) {
    //         // too long without receiving a packet = desync
    //         set_join_state(JOIN_STATE_IDLE);
    //         set_slot_state(STATE_SLEEP);
    //         disable_radio_and_intra_slot_timers();
    //         // return;
    //     }
    // }

    switch (mac_vars.join_state) {
        case JOIN_STATE_IDLE: // NODE or GATEWAY
            if (mac_vars.node_type == BLINK_GATEWAY) {
                // the gateway is always joined to itself
                set_join_state(JOIN_STATE_JOINED);
                mac_vars.asn = 0;
            } else {
                // play the scan procedure
                set_join_state(JOIN_STATE_SCANNING);
                mac_vars.scan_started_asn = mac_vars.asn;
                mac_vars.scan_started_ts = mac_vars.start_slot_ts;
                activity_scan_new_slot();
            }
            break;
        case JOIN_STATE_SCANNING: // only NODE
            if (mac_vars.asn - mac_vars.scan_started_asn < mac_vars.scan_max_slots) {
                // still have time to scan more
                activity_scan_new_slot();
            } else {
                // scan timeout reached
                set_slot_state(STATE_SLEEP);
                bl_radio_disable();
                if (select_gateway_and_sync()) {
                    // found a gateway and synchronized to it
                    set_join_state(JOIN_STATE_SYNCED);
                } else {
                    // no gateway found, go back to idle
                    set_join_state(JOIN_STATE_IDLE);
                }
                end_slot();
            }
            break;
        case JOIN_STATE_SYNCED: // only NODE
            // // FIXME(dbg): doing tx/rx here just for debugging, in reality will jump straight to JOIN_STATE_JOINING
            // activity_ti1_or_ri1();
            set_join_state(JOIN_STATE_JOINING);
            break;
        case JOIN_STATE_JOINING: // only NODE
            if (!mac_vars.waiting_join_response && mac_vars.current_slot_info.slot_can_join) {
                // put a JoinRequest at the head of the queue
                bl_queue_set_join_packet(mac_vars.synced_gateway, BLINK_PACKET_JOIN_REQUEST);
                mac_vars.waiting_join_response = true;
                activity_ti1();
            } else if (mac_vars.waiting_join_response && mac_vars.current_slot_info.type == SLOT_TYPE_DOWNLINK) {
                // receive a JoinResponse and change state to JOIN_STATE_JOINED
                activity_ri1();
            }
            break;
        case JOIN_STATE_JOINED: // NODE or GATEWAY
            // TODO: handle handover
            if (mac_vars.node_type == BLINK_GATEWAY) {
                // just normal tx/rx (normal tx packets from the queue, or join responses (depending on the slot type))
                activity_ti1_or_ri1();
                break;
            } else { // BLINK_NODE
                if (!mac_vars.is_background_scanning && !mac_vars.current_slot_info.available_for_scan) {
                    // no scan involved, just a regular slot doing its thing
                    activity_ti1_or_ri1();
                    break;
                }
                if (!mac_vars.is_background_scanning && mac_vars.current_slot_info.available_for_scan) {
                    // time to start a background scan
                    mac_vars.is_background_scanning = true;
                    mac_vars.scan_started_asn = mac_vars.asn;
                    mac_vars.scan_started_ts = mac_vars.start_slot_ts;
                    activity_scan_new_slot();
                    break;
                }
                if (mac_vars.is_background_scanning) {
                    // background scan in progress
                    if (mac_vars.asn - mac_vars.scan_started_asn < mac_vars.scan_max_slots) {
                        // still have time to scan more
                        if (mac_vars.current_slot_info.available_for_scan) {
                            activity_scan_new_slot();
                        }
                        break;
                    } else {
                        // scan timeout reached, may perform handover
                        mac_vars.is_background_scanning = false;
                        set_slot_state(STATE_SLEEP);
                        bl_radio_disable();
                        if (select_gateway_and_sync()) { // if possible/needed, will sync to a better gateway (begin of handover process)
                            // there is a better gateway to join, node is now synced to it
                            set_join_state(JOIN_STATE_SYNCED);
                        } else {
                            // no gateway found, nothing to do (will start a scan again in the next available slot)
                        }
                        end_slot();
                        break;
                    }
                }
            }
            // should never reach this point
            set_slot_state(STATE_SLEEP);
            end_slot();
            break;
        default:
            break;
    }

    mac_vars.asn++;
}

static void end_slot(void) {
    // do any needed cleanup
    bl_radio_disable();

    // NOTE: clean all numbered timers
    bl_timer_hf_cancel(BLINK_TIMER_DEV, BLINK_TIMER_CHANNEL_1);
    bl_timer_hf_cancel(BLINK_TIMER_DEV, BLINK_TIMER_CHANNEL_2);
    bl_timer_hf_cancel(BLINK_TIMER_DEV, BLINK_TIMER_CHANNEL_3);
}

static void disable_radio_and_intra_slot_timers(void) {
    bl_radio_disable();

    // NOTE: clean all timers
    bl_timer_hf_cancel(BLINK_TIMER_DEV, BLINK_TIMER_CHANNEL_1);
    bl_timer_hf_cancel(BLINK_TIMER_DEV, BLINK_TIMER_CHANNEL_2);
    bl_timer_hf_cancel(BLINK_TIMER_DEV, BLINK_TIMER_CHANNEL_3);
}

// --------------------- tx activities --------------------

inline static void activity_ti1_or_ri1(void) {
    if (mac_vars.current_slot_info.radio_action == BLINK_RADIO_ACTION_TX) {
        activity_ti1();
    } else if (mac_vars.current_slot_info.radio_action == BLINK_RADIO_ACTION_RX) {
        activity_ri1();
    } else if (mac_vars.current_slot_info.radio_action == BLINK_RADIO_ACTION_SLEEP) {
        set_slot_state(STATE_SLEEP);
        end_slot();
    }
}

static void activity_ti1(void) {
    // ti1: arm tx timers and prepare the radio for tx
    // called by: function new_slot
    set_slot_state(STATE_TX_OFFSET);

    bl_timer_hf_set_oneshot_with_ref_us( // TODO: use PPI instead
        BLINK_TIMER_DEV,
        BLINK_TIMER_CHANNEL_1,
        mac_vars.start_slot_ts,
        slot_durations.tx_offset,
        &activity_ti2
    );

    bl_timer_hf_set_oneshot_with_ref_us(
        BLINK_TIMER_DEV,
        BLINK_TIMER_CHANNEL_2,
        mac_vars.start_slot_ts,
        slot_durations.tx_offset + slot_durations.tx_max,
        &activity_tie1
    );

    // FIXME: get packets from a queue

    uint8_t packet[BLINK_PACKET_MAX_SIZE];
    uint8_t packet_len = 0;

    switch (mac_vars.current_slot_info.type) {
        case SLOT_TYPE_BEACON:
            if (mac_vars.node_type == BLINK_GATEWAY) {
                // send a beacon packet
                uint8_t dummy_remainig_capacity = 10; // FIXME
                packet_len = bl_build_packet_beacon(
                    packet,
                    mac_vars.asn,
                    dummy_remainig_capacity,
                    bl_scheduler_get_active_schedule_id()
                );
                bl_radio_tx_prepare(packet, packet_len);
                return; // STOP here
            }
            break;
        case SLOT_TYPE_SHARED_UPLINK:
            if (mac_vars.node_type == BLINK_NODE && bl_queue_has_join_packet()) {
                // send a join request
                bl_queue_get_join_packet(packet, &packet_len);
                bl_radio_tx_prepare(packet, packet_len);
                return; // STOP here
            }
            break;
        case SLOT_TYPE_DOWNLINK:
            if (mac_vars.node_type == BLINK_GATEWAY) {
                if (bl_queue_has_join_packet()) {
                    // send a join response, admitting the node
                    bl_queue_get_join_packet(packet, &packet_len);
                    bl_radio_tx_prepare(packet, packet_len);
                    return; // STOP here
                } else if (bl_queue_peek(packet, &packet_len)) {
                    // free this spot in the queue
                    bl_queue_pop();
                    // send a regular packet
                    bl_radio_tx_prepare(packet, packet_len);
                    return; // STOP here
                }
                break;
            }
            break;
        default:
            break;
    }

    // if arrived here, there's nothing to tx
    set_slot_state(STATE_SLEEP);
    end_slot();
}

static void activity_ti2(void) {
    // ti2: tx actually begins
    // called by: timer isr
    set_slot_state(STATE_TX_DATA);

    // FIXME: replace this call with a direct PPI connection, i.e., TsTxOffset expires -> radio tx
    bl_radio_tx_dispatch();
}

static void activity_tie1(void) {
    // tte1: something went wrong, stayed in tx for too long, abort
    // called by: timer isr
    set_slot_state(STATE_SLEEP);

    end_slot();
}

static void activity_ti3(void) {
    // ti3: all fine, finished tx, cancel error timers and go to sleep
    // called by: radio isr
    set_slot_state(STATE_SLEEP);

    // cancel tte1 timer
    bl_timer_hf_cancel(BLINK_TIMER_DEV, BLINK_TIMER_CHANNEL_2);

    end_slot();
}

// --------------------- rx activities --------------

// just write the placeholders for ri1

static void activity_ri1(void) {
    // ri1: arm rx timers and prepare the radio for rx
    // called by: function new_slot
    set_slot_state(STATE_RX_OFFSET);

    bl_timer_hf_set_oneshot_with_ref_us( // TODO: use PPI instead
        BLINK_TIMER_DEV,
        BLINK_TIMER_CHANNEL_1,
        mac_vars.start_slot_ts,
        slot_durations.rx_offset,
        &activity_ri2
    );

    bl_timer_hf_set_oneshot_with_ref_us(
        BLINK_TIMER_DEV,
        BLINK_TIMER_CHANNEL_2,
        mac_vars.start_slot_ts,
        slot_durations.tx_offset + slot_durations.rx_guard,
        &activity_rie1
    );

    bl_timer_hf_set_oneshot_with_ref_us(
        BLINK_TIMER_DEV,
        BLINK_TIMER_CHANNEL_3,
        mac_vars.start_slot_ts,
        slot_durations.rx_offset + slot_durations.rx_max,
        &activity_rie2
    );
}

static void activity_ri2(void) {
    // ri2: rx actually begins
    // called by: timer isr
    set_slot_state(STATE_RX_DATA_LISTEN);

    bl_radio_rx();
}

static void activity_ri3(uint32_t ts) {
    // ri3: a packet started to arrive
    // called by: radio isr
    set_slot_state(STATE_RX_DATA);

    // cancel timer for rx_guard
    bl_timer_hf_cancel(BLINK_TIMER_DEV, BLINK_TIMER_CHANNEL_2);

    // NOTE: got these parameters by looking at the logic analyzer
    uint32_t tx_delay_radio = 28; // time between START task and ADDRESS event
    uint32_t propagation_time = 6;
    uint32_t rx_delay_radio = 27;
    uint32_t ad_hoc_fix = 21;

    uint32_t expected_ts = mac_vars.start_slot_ts + slot_durations.tx_offset + (tx_delay_radio+propagation_time+rx_delay_radio+ad_hoc_fix);
    int32_t clock_drift = ts - expected_ts;
    uint32_t abs_clock_drift = abs(clock_drift);

    if (abs_clock_drift < 40) {
        // very small corrections can safely be ignored
    } else if (abs_clock_drift < 150) {
        // drift is acceptable
        // adjust the slot reference
        bl_timer_hf_set_oneshot_with_ref_diff_us(
            BLINK_TIMER_DEV,
            BLINK_TIMER_INTER_SLOT_CHANNEL,
            mac_vars.start_slot_ts,
            slot_durations.whole_slot + clock_drift,
            &new_slot
        );
        // DEBUG_GPIO_SET(&pin3); DEBUG_GPIO_CLEAR(&pin3); // show that the slot was adjusted for clock drift
    } else {
        // drift is too high, need to re-sync
        set_join_state(JOIN_STATE_IDLE);
        set_slot_state(STATE_SLEEP);
        end_slot();
    }
}

static void activity_rie1(void) {
    // rie1: didn't receive start of packet before rx_guard, abort
    // called by: timer isr
    set_slot_state(STATE_SLEEP);

    // cancel timer for rx_max (rie2)
    bl_timer_hf_cancel(BLINK_TIMER_DEV, BLINK_TIMER_CHANNEL_3);

    end_slot();
}

static void activity_ri4(uint32_t ts) {
    // ri4: all fine, finished rx, cancel error timers and go to sleep
    // called by: radio isr
    set_slot_state(STATE_SLEEP);

    // cancel timer for rx_max (rie2)
    bl_timer_hf_cancel(BLINK_TIMER_DEV, BLINK_TIMER_CHANNEL_3);

    if (!bl_radio_pending_rx_read()) {
        // no packet received
        end_slot();
        return;
    }

    bl_radio_get_rx_packet(mac_vars.received_packet.packet, &mac_vars.received_packet.packet_len);

    mac_vars.received_packet.finished_ts = ts; // NOTE: save ts now, or only if packet is valid?

    bl_packet_header_t *header = (bl_packet_header_t *)mac_vars.received_packet.packet;

    if (header->version != BLINK_PROTOCOL_VERSION) {
        end_slot();
        return;
    }

    if (header->dst != mac_vars.device_id && header->dst != BLINK_BROADCAST_ADDRESS) {
        end_slot();
        return;
    }

    switch (header->type) {
        case BLINK_PACKET_BEACON:
            // just ignore: beacons are handled by the scan activities
            break;
        case BLINK_PACKET_JOIN_REQUEST:
            if (mac_vars.node_type == BLINK_GATEWAY && header->dst == mac_vars.device_id) {
                // accept any node that wants to join
                bl_queue_set_join_packet(header->src, BLINK_PACKET_JOIN_RESPONSE);
                break;
            }
            break;
        case BLINK_PACKET_JOIN_RESPONSE:
            if (mac_vars.node_type == BLINK_NODE && header->dst == mac_vars.device_id) {
                // handle the join response
                mac_vars.waiting_join_response = false;
                set_join_state(JOIN_STATE_JOINED);
                break;
            }
            break;
        case BLINK_PACKET_DATA:
            // send the packet to the application
            if (mac_vars.app_rx_callback) {
                mac_vars.app_rx_callback(mac_vars.received_packet.packet, mac_vars.received_packet.packet_len);
            }
            break;
        default:
            break;
    }

    end_slot();
}

static void activity_rie2(void) {
    // rie2: something went wrong, stayed in rx for too long, abort
    // called by: timer isr
    set_slot_state(STATE_SLEEP);

    end_slot();
}

// --------------------- scan activities ------------------

static bool select_gateway_and_sync(void) {
    disable_radio_and_intra_slot_timers();

    // select best channel_info
    // uint32_t now_ts = bl_timer_hf_now(BLINK_TIMER_DEV);
    mac_vars.selected_channel_info = bl_scan_select(mac_vars.scan_started_ts, mac_vars.start_slot_ts);
    if (mac_vars.selected_channel_info.timestamp == 0) {
        // no gateway found
        set_slot_state(STATE_SLEEP);
        end_slot();
        return false;
    }

    if (mac_vars.join_state == JOIN_STATE_JOINED) { // this is a handover attempt
        if (mac_vars.selected_channel_info.rssi < mac_vars.synced_gateway_rssi + BLINK_SCAN_HANDOVER_HYSTERESIS) {
            // rssi difference is not enough to justify handover
            set_slot_state(STATE_SLEEP);
            end_slot();
            return false;
        }
    }

    set_slot_state(STATE_SCAN_SYNC);

    if (!bl_scheduler_set_schedule(mac_vars.selected_channel_info.beacon.active_schedule_id)) {
        // schedule not found
        // NOTE: what to do in this case? for now, just silently fail (a new scan will begin again via new_slot)
        set_slot_state(STATE_SLEEP);
        end_slot();
        return false;
    }

    // save the gateway address -- will try to join it in the next shared uplink slot
    mac_vars.synced_gateway = mac_vars.selected_channel_info.beacon.src;

    // the selected gateway may have been scanned a few slot_durations ago, so we need to account for that difference
    // NOTE: this assumes that the slot duration is the same for gateways and nodes
    // uint64_t asn_diff = (mac_vars.start_slot_ts - mac_vars.selected_channel_info.timestamp) / slot_durations.whole_slot;
    uint64_t asn_count_since_beacon = mac_vars.asn - mac_vars.selected_channel_info.captured_asn;
    asn_count_since_beacon += 2; // make sure we sync to a ts in the future

    // advance the asn to match the gateway's
    mac_vars.asn = mac_vars.selected_channel_info.beacon.asn + asn_count_since_beacon;
    mac_vars.asn -= 1; // adjust off by one

    // ---- calculate how much time we should adjust to match the gateway's slot ticking time

    // the time when the gateway started this beacon slot, accounting for TsTxOffset
    uint32_t gateway_ts = mac_vars.selected_channel_info.timestamp - BLINK_TS_TX_OFFSET;
    // advance it by the number of slots we have been scanning
    gateway_ts += (asn_count_since_beacon * slot_durations.whole_slot);

    uint32_t sync_diff = gateway_ts - mac_vars.start_slot_ts;

    sync_diff -= 144; // NOTE: ad-hoc correction based on experiments. to be adjusted as the code or parameters change.

    // set new slot ticking reference, overriding the timer set at new_slot
    bl_timer_hf_set_oneshot_with_ref_us(
        BLINK_TIMER_DEV,
        BLINK_TIMER_INTER_SLOT_CHANNEL, // overrides the currently set timer, which is non-synchronized
        mac_vars.start_slot_ts, // timestamp of the beacon at start_frame (which matches the start of the slot for synced_gateway), corrected by the asn_diff to account for the scan delay
        sync_diff,
        &new_slot
    );

    // update start_slot and sync ts
    mac_vars.start_slot_ts = mac_vars.start_slot_ts + sync_diff;
    mac_vars.synced_ts = mac_vars.start_slot_ts;

    set_slot_state(STATE_SLEEP);

    // synchronization is done!
    end_slot();

    return true;
}

static void activity_scan_new_slot(void) {
    if (mac_vars.state == STATE_SCAN_RX || mac_vars.state == STATE_SCAN_PROCESS_PACKET) {
        // in the middle of receiving a packet
        return;
    }

    // prepare timer for handler of end of this slot, to check if scan timeout has been reached
    bl_timer_hf_set_oneshot_with_ref_us(
        BLINK_TIMER_DEV,
        BLINK_TIMER_CHANNEL_1,
        mac_vars.start_slot_ts,
        slot_durations.whole_slot - slot_durations.end_guard,
        &activity_scan_end_slot
    );

    if (mac_vars.state != STATE_SCAN_LISTEN) {
        // if not in listen state, go to it
        set_slot_state(STATE_SCAN_LISTEN);
        mac_vars.scan_started_ts = mac_vars.start_slot_ts;
#ifdef BLINK_FIXED_CHANNEL
        bl_radio_set_channel(BLINK_FIXED_CHANNEL); // not doing channel hopping for now
#else
        puts("Channel hopping not implemented yet");
#endif
        bl_radio_rx();
    }
}

static void activity_scan_end_slot(void) {
    // called by: timer isr

    // do something here I guess?
}

static void activity_scan_start_frame(uint32_t ts) {
    if (mac_vars.state != STATE_SCAN_LISTEN) {
        // if not in listen state, just return
        return;
    }

    set_slot_state(STATE_SCAN_RX);
    mac_vars.current_scan_item_ts = ts;
    // save this here because there is a chance that activity_scan_end_frame happens in the next slot
}

static void activity_scan_end_frame(uint32_t end_frame_ts) {
    if (mac_vars.state != STATE_SCAN_RX) {
        // this should not happen!
        end_slot();
        return;
    }

    set_slot_state(STATE_SCAN_PROCESS_PACKET);

    uint8_t packet[BLINK_PACKET_MAX_SIZE];
    uint8_t packet_len;
    bl_radio_get_rx_packet(packet, &packet_len);

    // The `do { ... } while (0)` is a trick to allow using `break` to exit the block
    // in order to handle errors and avoid using `goto`.
    // To exit with success, we use `return` after all processing is done.
    do {
        // if not a beacon packet, ignore it and go back to listen
        if (packet[1] != BLINK_PACKET_BEACON) {
            break;
        }

        // now that we know it's a beacon packet, parse and process it
        bl_beacon_packet_header_t *beacon = (bl_beacon_packet_header_t *)packet;

        if (beacon->version != BLINK_PROTOCOL_VERSION) {
            // ignore packet with different protocol version
            break;
        }

        if (beacon->remaining_capacity == 0) {
            // this gateway is full, ignore it
            break;
        }

        // save this scan info
        bl_scan_add(*beacon, bl_radio_rssi(), BLINK_FIXED_CHANNEL, mac_vars.current_scan_item_ts, mac_vars.asn);

        mac_vars.received_packet.finished_ts = end_frame_ts;

        // success
    } while (0);

    bool can_continue = true;
    uint32_t now_ts = bl_timer_hf_now(BLINK_TIMER_DEV);

    // background scanning is limited in that we can't scan across slots
    // - NOTE: can we improve this by adding a bl_scheduler_peek_next_slot(asn+1) function?
    if (mac_vars.is_background_scanning) {
        // if there is still time within this slot, we can continue scanning
        if (now_ts + BLINK_BEACON_TOA_WITH_PADDING > mac_vars.start_slot_ts + slot_durations.whole_slot) {
            can_continue = false;
        }
    }

    if (can_continue) {
        set_slot_state(STATE_SCAN_LISTEN);
        // we cannot call rx immediately, because this runs in isr context
        // and it might interfere with `if (NRF_RADIO->EVENTS_DISABLED)` in RADIO_IRQHandler
        bl_timer_hf_set_oneshot_with_ref_us(
            BLINK_TIMER_DEV,
            BLINK_TIMER_CHANNEL_2,
            now_ts, // arbitrary value, just to give some time for the radio to turn off
            20,
            &bl_radio_rx
        );
    } else {
        // no more time to scan, end the slot
        set_slot_state(STATE_SLEEP);
        end_slot();
    }
}

// --------------------- tx/rx activities ------------

// --------------------- radio ---------------------
inline static bool is_scanning(void) {
    return mac_vars.join_state == JOIN_STATE_SCANNING || (mac_vars.join_state == JOIN_STATE_JOINED && mac_vars.is_background_scanning);
}

static void isr_mac_radio_start_frame(uint32_t ts) {
    DEBUG_GPIO_SET(&pin2);
    if (is_scanning()) {
        activity_scan_start_frame(ts);
        return;
    }

    switch (mac_vars.state) {
        case STATE_RX_DATA_LISTEN:
            activity_ri3(ts);
            break;
        default:
            break;
    }
}

static void isr_mac_radio_end_frame(uint32_t ts) {
    DEBUG_GPIO_CLEAR(&pin2);

    if (is_scanning()) {
        activity_scan_end_frame(ts);
        return;
    }

    switch (mac_vars.state) {
        case STATE_TX_DATA:
            activity_ti3();
            break;
        case STATE_RX_DATA:
            activity_ri4(ts);
            break;
        default:
            break;
    }
}
