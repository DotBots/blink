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
#include <stdbool.h>
#include <string.h>

#include "blink.h"
#include "mac.h"
#include "queue.h"
#include "scan.h"
#include "scheduler.h"
#include "association.h"
#include "bl_radio.h"
#include "bl_timer_hf.h"
#include "packet.h"
#include "bl_device.h"
#if defined(NRF5340_XXAA) && defined(NRF_NETWORK)
#include "ipc.h"
#endif

//=========================== debug ============================================

#ifndef DEBUG // FIXME: remove before merge. Just to make VS Code enable code behind `#ifdef DEBUG`
#define DEBUG
#endif

#ifdef DEBUG
#include "bl_gpio.h" // for debugging
// pins connected to logic analyzer, variable names reflect the channel number
bl_gpio_t pin0 = { .port = 1, .pin = 2 };
bl_gpio_t pin1 = { .port = 1, .pin = 3 };
bl_gpio_t pin2 = { .port = 1, .pin = 4 };
bl_gpio_t pin3 = { .port = 1, .pin = 5 };
#define DEBUG_GPIO_TOGGLE(pin) bl_gpio_toggle(pin)
#define DEBUG_GPIO_SET(pin) bl_gpio_set(pin)
#define DEBUG_GPIO_CLEAR(pin) bl_gpio_clear(pin)
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

    // transmitter
    STATE_TX_OFFSET = 21,
    STATE_TX_DATA = 22,

    // receiver
    STATE_RX_OFFSET = 31,
    STATE_RX_DATA_LISTEN = 32,
    STATE_RX_DATA = 33,

} bl_mac_state_t;

typedef struct {
    bl_node_type_t node_type; //< whether the node is a gateway or a dotbot
    uint64_t device_id; ///< Device ID

    bl_mac_state_t state; ///< State within the slot
    uint32_t start_slot_ts; ///< Timestamp of the start of the slot
    uint64_t asn; ///< Absolute slot number
    bl_slot_info_t current_slot_info; ///< Information about the current slot

    bl_event_cb_t blink_event_callback; ///< Function pointer, stores the application callback

    bl_received_packet_t received_packet; ///< Last received packet

    bool is_scanning; ///< Whether the node is scanning for gateways
    uint32_t scan_started_ts; ///< Timestamp of the start of the scan
    uint32_t scan_expected_end_ts; ///< Timestamp of the expected end of the scan
    uint32_t current_scan_item_ts; ///< Timestamp of the current scan item

    bool is_bg_scanning; ///< Whether the node is scanning for gateways in the background
    bool bg_scan_sleep_next_slot; ///< Whether the next slot is a sleep slot

    uint64_t synced_gateway; ///< ID of the gateway the node is synchronized with
    uint32_t synced_ts; ///< Timestamp of the last synchronization
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

static void new_slot_synced(void);
static void end_slot(void);
static void disable_radio_and_intra_slot_timers(void);

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

static void fix_drift(uint32_t ts);

static void start_scan(void);
static void end_scan(void);
static void activity_scan_start_frame(uint32_t ts);
static void activity_scan_end_frame(uint32_t ts);
static bool select_gateway_and_sync(void);

static void start_background_scan(void);
static void end_background_scan(void);

static void isr_mac_radio_start_frame(uint32_t ts);
static void isr_mac_radio_end_frame(uint32_t ts);

//=========================== public ===========================================

void bl_mac_init(bl_node_type_t node_type, bl_event_cb_t event_callback) {
#ifdef DEBUG
    bl_gpio_init(&pin0, BL_GPIO_OUT);
    bl_gpio_init(&pin1, BL_GPIO_OUT);
    bl_gpio_init(&pin2, BL_GPIO_OUT);
    bl_gpio_init(&pin3, BL_GPIO_OUT);
#endif

    // initialize the high frequency timer
    bl_timer_hf_init(BLINK_TIMER_DEV);

    // initialize the radio
    bl_radio_init(&isr_mac_radio_start_frame, &isr_mac_radio_end_frame, BL_RADIO_BLE_2MBit);

    // node stuff
    mac_vars.node_type = node_type;
    mac_vars.device_id = bl_device_id();

    // synchronization stuff
    mac_vars.asn = 0;

    // application callback
    mac_vars.blink_event_callback = event_callback;

    // begin the slot
    set_slot_state(STATE_SLEEP);

    if (mac_vars.node_type == BLINK_GATEWAY) {
        mac_vars.start_slot_ts = bl_timer_hf_now(BLINK_TIMER_DEV);
        bl_assoc_set_state(JOIN_STATE_JOINED);
        bl_timer_hf_set_periodic_us(
            BLINK_TIMER_DEV,
            BLINK_TIMER_INTER_SLOT_CHANNEL,
            slot_durations.whole_slot,
            &new_slot_synced
        );
    } else {
        start_scan();
    }
}

uint64_t bl_mac_get_asn(void) {
    return mac_vars.asn;
}

uint64_t bl_mac_get_synced_ts(void) {
    return mac_vars.synced_ts;
}

uint64_t bl_mac_get_synced_gateway(void) {
    return mac_vars.synced_gateway;
}

//=========================== private ==========================================

static void set_slot_state(bl_mac_state_t state) {
    mac_vars.state = state;

    switch (state) {
        case STATE_RX_DATA_LISTEN:
            // DEBUG_GPIO_SET(&pin3);
        case STATE_TX_DATA:
        case STATE_RX_DATA:
            DEBUG_GPIO_SET(&pin1);
            break;
        case STATE_SLEEP:
            DEBUG_GPIO_CLEAR(&pin1);
            DEBUG_GPIO_CLEAR(&pin2); // pin2 might be SET in case a packet had just started to arrive, so set it low again
            break;
        default:
            break;
    }
}

// --------------------- start/end synced slots -----------

static void new_slot_synced(void) {
    mac_vars.start_slot_ts = bl_timer_hf_now(BLINK_TIMER_DEV);
    DEBUG_GPIO_SET(&pin0); DEBUG_GPIO_CLEAR(&pin0); // debug: show that a new slot started

    // perform timeout checks
    if (mac_vars.node_type == BLINK_GATEWAY) {
        // too long without receiving a packet from certain nodes? disconnect them
        bl_assoc_gateway_clear_old_nodes(mac_vars.asn);
    } else if (mac_vars.node_type == BLINK_NODE) {
        if (bl_assoc_node_gateway_is_lost(mac_vars.asn)) {
            // too long without receiving a packet from gateway? disconnect and back to scanning
            bl_assoc_node_handle_disconnect();
            set_slot_state(STATE_SLEEP);
            end_slot();
            start_scan();
            return;
        } else if (bl_assoc_node_too_long_waiting_for_join_response()) {
            // too long without receiving a join response? notify the association module which will backfoff
            bl_assoc_node_handle_failed_join();
        } else if (bl_assoc_node_too_long_synced_without_joining()) {
            // too long synced without being able to join? back to scanning
            bl_assoc_node_handle_give_up_joining();
            bl_assoc_set_state(JOIN_STATE_IDLE);
            set_slot_state(STATE_SLEEP);
            end_slot();
            start_scan();
            return;
        }
    }

    mac_vars.current_slot_info = bl_scheduler_tick(mac_vars.asn++);

    if (mac_vars.current_slot_info.radio_action == BLINK_RADIO_ACTION_TX) {
        activity_ti1();
    } else if (mac_vars.current_slot_info.radio_action == BLINK_RADIO_ACTION_RX) {
        activity_ri1();
    } else if (mac_vars.current_slot_info.radio_action == BLINK_RADIO_ACTION_SLEEP) {
        // check if we should use this slot for background scan
        if (mac_vars.node_type == BLINK_GATEWAY || !BLINK_ENABLE_BACKGROUND_SCAN) {
            set_slot_state(STATE_SLEEP);
            end_slot();
        } else { // node with background scan enabled
            start_background_scan();
        }
    }
}

static void end_slot(void) {
    disable_radio_and_intra_slot_timers();
}

static void disable_radio_and_intra_slot_timers(void) {
    bl_radio_disable();

    // NOTE: clean all timers
    bl_timer_hf_cancel(BLINK_TIMER_DEV, BLINK_TIMER_CHANNEL_1);
    bl_timer_hf_cancel(BLINK_TIMER_DEV, BLINK_TIMER_CHANNEL_2);
    bl_timer_hf_cancel(BLINK_TIMER_DEV, BLINK_TIMER_CHANNEL_3);
}

// --------------------- start/end scan -------------------

static void start_scan(void) {
    mac_vars.scan_started_ts = bl_timer_hf_now(BLINK_TIMER_DEV);
    mac_vars.scan_expected_end_ts = mac_vars.scan_started_ts + BLINK_SCAN_MAX_DURATION;
    DEBUG_GPIO_SET(&pin0); // debug: show that a new scan started
    mac_vars.is_scanning = true;
    bl_assoc_set_state(JOIN_STATE_SCANNING);

    // end_scan will be called when the scan is over
    bl_timer_hf_set_oneshot_with_ref_us(
        BLINK_TIMER_DEV,
        BLINK_TIMER_INTER_SLOT_CHANNEL,
        mac_vars.scan_started_ts,
        BLINK_SCAN_MAX_DURATION, // scan during a certain amount of slots
        &end_scan
    );

    // mac_vars.assoc_info = bl_assoc_get_info(); // NOTE: why this?

    set_slot_state(STATE_RX_DATA_LISTEN);
    bl_radio_disable();
#ifdef BLINK_FIXED_SCAN_CHANNEL
    bl_radio_set_channel(BLINK_FIXED_SCAN_CHANNEL); // not doing channel hopping for now
#else
    puts("Channel hopping not implemented yet for scanning");
#endif
    bl_radio_rx();
}

static void end_scan(void) {
    mac_vars.is_scanning = false;
    DEBUG_GPIO_CLEAR(&pin0); // debug: show that the scan is over
    set_slot_state(STATE_SLEEP);
    disable_radio_and_intra_slot_timers();

    if (select_gateway_and_sync()) {
        // found a gateway and synchronized to it
        bl_assoc_node_handle_synced();
    } else {
        // no gateway found, back to scanning
        start_scan();
    }
}

// --------------------- start/end background scan --------

static void start_background_scan(void) {
    // 1. prepare timestamps and and arm timer
    if (!mac_vars.is_bg_scanning) {
        mac_vars.scan_started_ts = mac_vars.start_slot_ts; // reuse the slot start time as reference
        mac_vars.scan_expected_end_ts = mac_vars.scan_started_ts + BLINK_BG_SCAN_DURATION;
    }

    // end_scan will be called when the scan is over
    bl_timer_hf_set_oneshot_with_ref_us(
        BLINK_TIMER_DEV,
        BLINK_TIMER_CHANNEL_1, // remember that the inter-slot timer is already being used for the slot
        mac_vars.start_slot_ts, // in this case, we use the slot start time as reference because we are synced
        BLINK_BG_SCAN_DURATION, // scan for some time during this slot
        &end_background_scan
    );

    // 2. turn on the radio, in case it was off (bg scan might be already running since the last slot)
    if (!mac_vars.is_bg_scanning) {
        set_slot_state(STATE_RX_DATA_LISTEN);
        bl_radio_disable();
#ifdef BLINK_FIXED_SCAN_CHANNEL
        bl_radio_set_channel(BLINK_FIXED_SCAN_CHANNEL); // not doing channel hopping for now
#else
        puts("Channel hopping not implemented yet for scanning");
#endif
        bl_radio_rx();
    }
    mac_vars.is_bg_scanning = true;
}

static void end_background_scan(void) {
    cell_t next_slot = bl_scheduler_node_peek_slot(mac_vars.asn); // remember: the asn was already incremented at new_slot_synced
    mac_vars.bg_scan_sleep_next_slot = next_slot.type == SLOT_TYPE_UPLINK && next_slot.assigned_node_id != bl_device_id();

    if (!mac_vars.bg_scan_sleep_next_slot) {
        // if next slot is not sleep, stop the background scan and check if there is an alternative gateway to join
        mac_vars.is_bg_scanning = false;
        set_slot_state(STATE_SLEEP);
        disable_radio_and_intra_slot_timers();

        if (select_gateway_and_sync()) {
            // found a gateway and synchronized to it
            bl_assoc_node_handle_synced();
        }
    }
}

// --------------------- tx activities --------------------

static void activity_ti1(void) {
    // ti1: arm tx timers and prepare the radio for tx
    // called by: function new_slot_synced
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

    // FIXME: check if there is a packet to send before arming the timers
    uint8_t packet[BLINK_PACKET_MAX_SIZE];
    uint8_t packet_len = bl_queue_next_packet(mac_vars.current_slot_info.type, packet);
    if (packet_len > 0) {
        bl_radio_disable();
        bl_radio_set_channel(mac_vars.current_slot_info.channel);
        bl_radio_tx_prepare(packet, packet_len);
    } else {
        // nothing to tx
        set_slot_state(STATE_SLEEP);
        end_slot();
    }
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
    // called by: function new_slot_synced
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

    bl_radio_disable();
    bl_radio_set_channel(mac_vars.current_slot_info.channel);
    bl_radio_rx();
}

static void activity_ri3(uint32_t ts) {
    // ri3: a packet started to arrive
    // called by: radio isr
    set_slot_state(STATE_RX_DATA);

    // cancel timer for rx_guard
    bl_timer_hf_cancel(BLINK_TIMER_DEV, BLINK_TIMER_CHANNEL_2);

    mac_vars.received_packet.start_ts = ts;
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

    bl_packet_header_t *header = (bl_packet_header_t *)mac_vars.received_packet.packet;

    if (header->version != BLINK_PROTOCOL_VERSION) {
        end_slot();
        return;
    }

    if (mac_vars.node_type == BLINK_NODE && bl_assoc_is_joined() && header->src == mac_vars.synced_gateway) {
        // only fix drift if the packet comes from the gateway we are synced to
        // NOTE: this should ideally be done at ri3 (when the packet starts), but we don't have the id there.
        //       could use use the physical BLE address for that?
        fix_drift(mac_vars.received_packet.start_ts);
    }

    // now that we know it's a blink packet, store some info about it
    mac_vars.received_packet.channel = mac_vars.current_slot_info.channel;
    mac_vars.received_packet.rssi = bl_radio_rssi();
    mac_vars.received_packet.end_ts = ts;
    mac_vars.received_packet.asn = mac_vars.asn;

    bl_handle_packet(mac_vars.received_packet.packet, mac_vars.received_packet.packet_len);

    end_slot();
}

static void activity_rie2(void) {
    // rie2: something went wrong, stayed in rx for too long, abort
    // called by: timer isr
    set_slot_state(STATE_SLEEP);

    end_slot();
}

static void fix_drift(uint32_t ts) {
    uint32_t time_cpu_periph = 47; // got this value by looking at the logic analyzer

    uint32_t expected_ts = mac_vars.start_slot_ts + slot_durations.tx_offset + time_cpu_periph;
    int32_t clock_drift = ts - expected_ts;
    uint32_t abs_clock_drift = abs(clock_drift);

    if (abs_clock_drift < 100) {
        // drift is acceptable
        // adjust the slot reference
        bl_timer_hf_adjust_periodic_us(
            BLINK_TIMER_DEV,
            BLINK_TIMER_INTER_SLOT_CHANNEL,
            clock_drift
        );
    } else {
        // drift is too high, need to re-sync
        bl_event_data_t event_data = { .data.gateway_info.gateway_id = mac_vars.synced_gateway, .tag = BLINK_OUT_OF_SYNC };
        mac_vars.blink_event_callback(BLINK_DISCONNECTED, event_data);
        bl_assoc_set_state(JOIN_STATE_IDLE);
        set_slot_state(STATE_SLEEP);
        end_slot();
        start_scan();
    }
}

// --------------------- scan activities ------------------

static void activity_scan_dispatch_new_schedule(void) {
    bl_timer_hf_set_periodic_us(
        BLINK_TIMER_DEV,
        BLINK_TIMER_INTER_SLOT_CHANNEL,
        slot_durations.whole_slot,
        &new_slot_synced
    );
}

static bool select_gateway_and_sync(void) {
    uint32_t now_ts = bl_timer_hf_now(BLINK_TIMER_DEV);
    bool is_handover = false;

    bl_channel_info_t selected_gateway = { 0 };
    if (!bl_scan_select(&selected_gateway, mac_vars.scan_started_ts, now_ts)) {
        // no gateway found
        return false;
    }

    if (bl_assoc_is_joined()) {
        // this is a handover attempt
        if (selected_gateway.beacon.src == mac_vars.synced_gateway) {
            // should not happen, but just in case: already synced to this gateway, ignore it
            return false;
        }
        if (mac_vars.synced_ts - selected_gateway.timestamp < BLINK_HANDOVER_MIN_INTERVAL) {
            // just recently performed a synchronization, will not try again so soon
            return false;
        }
        if (selected_gateway.rssi > mac_vars.received_packet.rssi + BLINK_HANDOVER_RSSI_HYSTERESIS) {
            // the new gateway is not strong enough, ignore it
            return false;
        }
        is_handover = true;
    }

    if (!bl_scheduler_set_schedule(selected_gateway.beacon.active_schedule_id)) {
        // schedule not found, a new scan will begin again via new_scan
        return false;
    }

    if (is_handover) {
        DEBUG_GPIO_SET(&pin3); DEBUG_GPIO_CLEAR(&pin3); // pin3 DEBUG
        // a handover is going to happen, notify application about network disconnection
        bl_event_data_t event_data = { .data.gateway_info.gateway_id = mac_vars.synced_gateway, .tag = BLINK_HANDOVER };
        mac_vars.blink_event_callback(BLINK_DISCONNECTED, event_data);
        // NOTE: should we `bl_assoc_set_state(JOIN_STATE_IDLE);` here?
        // during handover, we don't want the inter slot timer to tick again before we finish sync, so just set if far away in the future
        bl_timer_hf_set_periodic_us(
            BLINK_TIMER_DEV,
            BLINK_TIMER_INTER_SLOT_CHANNEL,
            slot_durations.whole_slot << 4, // 16 slots in the future
            &new_slot_synced
        );
    }

    mac_vars.synced_gateway = selected_gateway.beacon.src;
    mac_vars.synced_ts = now_ts;

    // the selected gateway may have been scanned a few slot_durations ago, so we need to account for that difference
    // NOTE: this assumes that the slot duration is the same for gateways and nodes
    uint32_t time_since_beacon = now_ts - selected_gateway.timestamp;
    uint64_t asn_count_since_beacon = (time_since_beacon / slot_durations.whole_slot) + 1; // +1 because we are inside the current slot
    uint64_t time_into_gateway_slot = time_since_beacon % slot_durations.whole_slot;

    uint64_t time_to_skip_one_slot = 0;
    if (time_into_gateway_slot > slot_durations.whole_slot / 2) {
        // too close to the next slot, skip this one
        asn_count_since_beacon++;
        time_to_skip_one_slot = slot_durations.whole_slot;
    }

    uint64_t time_cpu_and_toa = 398; // magic number: measured using the logic analyzer
    if (is_handover) {
        time_cpu_and_toa += 116; // magic number: measured using the logic analyzer (why??)
    }

    uint32_t time_dispatch_new_schedule = ((slot_durations.whole_slot - time_into_gateway_slot) + time_to_skip_one_slot) - time_cpu_and_toa;
    bl_timer_hf_set_oneshot_us(
        BLINK_TIMER_DEV,
        BLINK_TIMER_CHANNEL_1,
        time_dispatch_new_schedule,
        &activity_scan_dispatch_new_schedule
    );

    // set the asn to match the gateway's
    mac_vars.asn = selected_gateway.beacon.asn + asn_count_since_beacon;

    return true;
}

static void activity_scan_start_frame(uint32_t ts) {
    set_slot_state(STATE_RX_DATA);
    mac_vars.current_scan_item_ts = ts;

    // NOTE: should probably set an error timer here, in case the end event doesn't happen
}

static void activity_scan_end_frame(uint32_t end_frame_ts) {
    uint8_t packet[BLINK_PACKET_MAX_SIZE];
    uint8_t packet_len;
    bl_radio_get_rx_packet(packet, &packet_len);

    bl_assoc_handle_beacon(packet, packet_len, BLINK_FIXED_SCAN_CHANNEL, mac_vars.current_scan_item_ts);

    // if there is still enough time before end of scan, re-enable the radio
    bool still_time_for_rx_scan = mac_vars.is_scanning && (end_frame_ts + BLINK_BEACON_TOA_WITH_PADDING < mac_vars.scan_expected_end_ts);
    bool still_time_for_rx_bg_scan = mac_vars.is_bg_scanning && mac_vars.bg_scan_sleep_next_slot;
    if (still_time_for_rx_scan || still_time_for_rx_bg_scan) {
        // re-enable the radio, if there still time to scan more (conditions for normal / bg scan)
        set_slot_state(STATE_RX_DATA_LISTEN);
        // we cannot call rx immediately, because this runs in isr context/
        // and it might interfere with `if (NRF_RADIO->EVENTS_DISABLED)` in RADIO_IRQHandler
        bl_timer_hf_set_oneshot_with_ref_us(
            BLINK_TIMER_DEV,
            BLINK_TIMER_CHANNEL_2,
            end_frame_ts,
            20, // arbitrary value, just to give some time for the radio to turn off
            &bl_radio_rx
        );
    } else {
        set_slot_state(STATE_SLEEP);
    }
}

// --------------------- tx/rx activities ------------

// --------------------- radio ---------------------
static void isr_mac_radio_start_frame(uint32_t ts) {
    DEBUG_GPIO_SET(&pin2);
    if (mac_vars.is_scanning || mac_vars.is_bg_scanning) {
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

    if (mac_vars.is_scanning || mac_vars.is_bg_scanning) {
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
