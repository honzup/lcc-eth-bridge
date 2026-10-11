#pragma once
#include <stdint.h>
#include <stdbool.h>

// Start the LCC bridge: TWAI @125 kbit/s + GridConnect TCP server on :12021.
void bridge_start(void);

// ---- Observability (read-only; added to trace multi-frame config-write drops) ----

// Frames dropped by feed_byte() when the net->CAN queue (s_tx_q) was full.
uint32_t bridge_tx_drops(void);

// net->CAN queue occupancy: *now = current depth, *max = peak depth seen
// since boot, *size = queue capacity. Any pointer may be NULL to skip it.
void bridge_tx_q_stats(uint32_t *now, uint32_t *max, uint32_t *size);

// CAN->net queue: *drops = frames the RX ISR couldn't enqueue (queue full),
// *now/*max/*size mirror bridge_tx_q_stats() for s_rx_q. Any pointer may be
// NULL to skip it.
void bridge_rx_q_stats(uint32_t *drops, uint32_t *now, uint32_t *max, uint32_t *size);

// TWAI node health, from twai_node_get_info(). state mirrors
// twai_error_state_t (0=ACTIVE 1=WARNING 2=PASSIVE 3=BUS_OFF) without
// pulling TWAI headers into every includer of bridge.h.
typedef struct {
    int      state;
    uint16_t tx_error_count;
    uint16_t rx_error_count;
    uint32_t bus_err_num;   // IDF's cumulative count: real bus errors AND lost
                             // arbitration (both bump it). Kept for reference.
    uint32_t arb_lost;      // lost arbitration – normal CAN, not an error
    uint32_t err_bit, err_form, err_stuff, err_ack;   // real bus errors by type
} bridge_can_health_t;

// Returns false (h left untouched) if the node isn't up yet or the read failed.
bool bridge_can_health(bridge_can_health_t *h);
