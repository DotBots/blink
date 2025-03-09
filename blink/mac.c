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

#include "blink.h"
#include "mac.h"
#include "queue.h"
#include "scan.h"
#include "scheduler.h"
#include "association.h"
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

    // transmitter
    STATE_TX_OFFSET = 21,
    STATE_TX_DATA = 22,

    // receiver
    STATE_RX_OFFSET = 31,
    STATE_RX_DATA_LISTEN = 32,
    STATE_RX_DATA = 33,

} bl_mac_state_t;

typedef struct {
    uint8_t channel;
    int8_t rssi;
    uint32_t ts;
    uint64_t asn;
    bool to_me;
    uint8_t packet[BLINK_PACKET_MAX_SIZE];
    uint8_t packet_len;
} bl_received_packet_t;

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
    uint32_t current_scan_item_ts; ///< Timestamp of the current scan item

    bool is_bg_scanning; ///< Whether the node is scanning for gateways in the background

    uint64_t synced_gateway; ///< ID of the gateway the node is synchronized with
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
    db_gpio_init(&pin0, DB_GPIO_OUT);
    db_gpio_init(&pin1, DB_GPIO_OUT);
    db_gpio_init(&pin2, DB_GPIO_OUT);
    db_gpio_init(&pin3, DB_GPIO_OUT);
#endif

    // initialize the high frequency timer
    bl_timer_hf_init(BLINK_TIMER_DEV);

    // initialize the radio
    bl_radio_init(&isr_mac_radio_start_frame, &isr_mac_radio_end_frame, DB_RADIO_BLE_2MBit);

    // node stuff
    mac_vars.node_type = node_type;
    mac_vars.device_id = db_device_id();

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
            // DEBUG_GPIO_CLEAR(&pin3);
            break;
        default:
            break;
    }
}

// --------------------- start/end synced slots -----------

static void new_slot_synced(void) {
    mac_vars.start_slot_ts = bl_timer_hf_now(BLINK_TIMER_DEV);
    DEBUG_GPIO_SET(&pin0); DEBUG_GPIO_CLEAR(&pin0); // debug: show that a new slot started

    // too long without receiving a packet? disconnect
    if (mac_vars.node_type == BLINK_GATEWAY) {
        bl_assoc_clear_old_nodes(mac_vars.asn);
    } else if (mac_vars.node_type == BLINK_NODE && bl_assoc_node_is_joined()) {
        if ((mac_vars.asn - mac_vars.received_packet.asn) > bl_scheduler_get_active_schedule_slot_count() * BLINK_MAX_SLOTFRAMES_NO_RX_LEAVE) {
            mac_vars.blink_event_callback(BLINK_DISCONNECTED, (bl_event_data_t){ 0 });
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
    DEBUG_GPIO_SET(&pin0); // debug: show that a new scan started
    mac_vars.is_scanning = true;

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
        bl_assoc_set_state(JOIN_STATE_SYNCED);
        bl_queue_set_join_request(mac_vars.synced_gateway);
    } else {
        // no gateway found, back to scanning
        start_scan();
    }
}

// --------------------- start/end background scan --------

static void start_background_scan(void) {
    mac_vars.scan_started_ts = mac_vars.start_slot_ts; // reuse the slot start time as reference
    DEBUG_GPIO_SET(&pin0); // debug: show that a new scan started
    mac_vars.is_bg_scanning = true;

    // end_scan will be called when the scan is over
    bl_timer_hf_set_oneshot_with_ref_us(
        BLINK_TIMER_DEV,
        BLINK_TIMER_CHANNEL_1, // remember that the inter-slot timer is already being used for the slot
        mac_vars.scan_started_ts, // in this case, we use the slot start time as reference because we are synced
        BLINK_TS_TX_OFFSET + BLINK_PACKET_TOA_WITH_PADDING, // scan for some time during this slot
        &end_background_scan
    );

    set_slot_state(STATE_RX_DATA_LISTEN);
    bl_radio_disable();
#ifdef BLINK_FIXED_SCAN_CHANNEL
    bl_radio_set_channel(BLINK_FIXED_SCAN_CHANNEL); // not doing channel hopping for now
#else
    puts("Channel hopping not implemented yet for scanning");
#endif
    bl_radio_rx();
}

static void end_background_scan(void) {
    mac_vars.is_bg_scanning = false;
    DEBUG_GPIO_CLEAR(&pin0); // debug: show that the scan is over
    set_slot_state(STATE_SLEEP);
    disable_radio_and_intra_slot_timers();

    if (select_gateway_and_sync()) {
        // found a gateway and synchronized to it
        bl_assoc_set_state(JOIN_STATE_SYNCED);
        bl_queue_set_join_request(mac_vars.synced_gateway);
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
#ifdef BLINK_FIXED_CHANNEL
    bl_radio_set_channel(BLINK_FIXED_CHANNEL); // not doing channel hopping for now
#else
    bl_radio_set_channel(mac_vars.current_slot_info.channel);
#endif
    bl_radio_rx();
}

static void activity_ri3(uint32_t ts) {
    // ri3: a packet started to arrive
    // called by: radio isr
    set_slot_state(STATE_RX_DATA);

    // cancel timer for rx_guard
    bl_timer_hf_cancel(BLINK_TIMER_DEV, BLINK_TIMER_CHANNEL_2);

    if (bl_get_node_type() == BLINK_GATEWAY) {
        // the gateway is the time reference, so no need to check for clock drift
        return;
    }

    uint32_t time_cpu_periph = 47; // got this value by looking at the logic analyzer

    uint32_t expected_ts = mac_vars.start_slot_ts + slot_durations.tx_offset + time_cpu_periph;
    int32_t clock_drift = ts - expected_ts;
    uint32_t abs_clock_drift = abs(clock_drift);

    if (abs_clock_drift < 5) {
        // very small corrections can safely be ignored
    } else if (abs_clock_drift < 80) {
        // drift is acceptable
        // adjust the slot reference
        bl_timer_hf_adjust_periodic_us(
            BLINK_TIMER_DEV,
            BLINK_TIMER_INTER_SLOT_CHANNEL,
            clock_drift
        );
        DEBUG_GPIO_SET(&pin3); DEBUG_GPIO_CLEAR(&pin3); // show that the slot was adjusted for clock drift
    } else {
        // drift is too high, need to re-sync
        bl_assoc_set_state(JOIN_STATE_IDLE);
        set_slot_state(STATE_SLEEP);
        end_slot();
        start_scan();
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

    bl_packet_header_t *header = (bl_packet_header_t *)mac_vars.received_packet.packet;

    if (header->version != BLINK_PROTOCOL_VERSION) {
        end_slot();
        return;
    }

    if (header->dst != mac_vars.device_id && header->dst != BLINK_BROADCAST_ADDRESS && header->type != BLINK_PACKET_BEACON) {
        end_slot();
        return;
    }

    // now that we know it's a blink packet, either for me, or broadcast, or a beacon, store some info about it
    mac_vars.received_packet.channel = mac_vars.current_slot_info.channel;
    mac_vars.received_packet.rssi = bl_radio_rssi();
    mac_vars.received_packet.ts = ts;
    mac_vars.received_packet.asn = mac_vars.asn;

    if (mac_vars.node_type == BLINK_GATEWAY && header->dst == mac_vars.device_id) {
        bl_assoc_save_received_from_node(header->src, mac_vars.asn);
    }

    switch (header->type) {
        case BLINK_PACKET_BEACON:
        case BLINK_PACKET_JOIN_REQUEST:
        case BLINK_PACKET_JOIN_RESPONSE:
            bl_assoc_handle_packet(mac_vars.received_packet.packet, mac_vars.received_packet.packet_len);
            break;
        case BLINK_PACKET_DATA:
            // send the packet to the application
            if (mac_vars.blink_event_callback) {
                bl_event_data_t event_data = {
                    .data.new_packet = {
                        .packet = mac_vars.received_packet.packet,
                        .length = mac_vars.received_packet.packet_len
                    }
                };
                mac_vars.blink_event_callback(BLINK_NEW_PACKET, event_data);
            }
            break;
        case BLINK_PACKET_KEEPALIVE:
            // at this point can just ignore keepalives, since node info was already saved in the association module
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

    bl_channel_info_t selected_gateway = { 0 };
    if (!bl_scan_select(&selected_gateway, mac_vars.scan_started_ts, now_ts)) {
        // no gateway found
        return false;
    }

    if (!bl_scheduler_set_schedule(selected_gateway.beacon.active_schedule_id)) {
        // schedule not found, a new scan will begin again via new_scan
        return false;
    }

    mac_vars.synced_gateway = selected_gateway.beacon.src;

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

    uint64_t time_cpu_and_toa = 398; // measured using the logic analyzer

    bl_timer_hf_set_oneshot_us(
        BLINK_TIMER_DEV,
        BLINK_TIMER_CHANNEL_1,
        ((slot_durations.whole_slot - time_into_gateway_slot) + time_to_skip_one_slot) - time_cpu_and_toa,
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
    if (end_frame_ts + BLINK_BEACON_TOA_WITH_PADDING < mac_vars.scan_started_ts + BLINK_SCAN_MAX_DURATION) {
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
    }
}

// --------------------- tx/rx activities ------------

// --------------------- radio ---------------------
static void isr_mac_radio_start_frame(uint32_t ts) {
    DEBUG_GPIO_SET(&pin2);
    if (mac_vars.is_scanning) {
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

    if (mac_vars.is_scanning) {
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
