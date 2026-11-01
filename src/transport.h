/*
 * Bluetooth transport: Classic Bluetooth (BR/EDR) HID mouse on the original
 * ESP32 (transport_classic.c). Kept as an interface so main.c stays small.
 */
#pragma once

#include <stdbool.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LINK_IDLE = 0,       /* trigger inactive: hidden, not connectable           */
    LINK_PAIRING,        /* discoverable, waiting for a (new) phone             */
    LINK_SEARCHING,      /* paired phone known, trying to (re)connect           */
    LINK_CONNECTED,      /* connected to the phone                              */
} link_state_t;

/* Bring up the stack. Called once. */
void transport_init(const btt_config_t *cfg);

/* Enable/disable the link. When disabled the device disconnects and hides. */
void transport_set_enabled(bool enabled);

/* Allow or refuse pairing with NEW phones (paired phones always reconnect). */
void transport_set_pairable(bool pairable);

/* Call once per second from the main loop (reconnect timers, etc.). */
void transport_tick_1s(void);

link_state_t transport_state(void);

/* True if at least one phone is paired (bonded). */
bool transport_has_bonds(void);

/* Forget every paired phone. */
void transport_forget_all(void);

/* Short label of the transport, for the logs. */
const char *transport_name(void);

#ifdef __cplusplus
}
#endif
