#pragma once

#include "esp_err.h"

/** Net FSM states per docs/DESIGN.md section 8.2. Runs in its own task. */
typedef enum {
    NET_STATE_IDLE,
    NET_STATE_WIFI_CONNECTING,
    NET_STATE_SNTP_SYNCING,
    NET_STATE_SYNCED,
    NET_STATE_OFFLINE,
    NET_STATE_PROVISIONING,
} net_state_t;

/**
 * Starts the net task. If WiFi credentials exist in NVS it moves to
 * WIFI_CONNECTING, otherwise straight to OFFLINE (docs/DESIGN.md section
 * 8.2, NET_IDLE transitions). On a successful SNTP sync it calls
 * timekeeping_set_time() to write the result through to the RTC.
 */
esp_err_t net_init(void);

/**
 * Current Net FSM state. ui polls this for the ONLINE/OFFLINE status
 * display (docs/DESIGN.md section 5.8): NET_STATE_SYNCED = "ONLINE",
 * anything else = "OFFLINE".
 */
net_state_t net_get_state(void);

/**
 * Switches into SoftAP provisioning (docs/DESIGN.md section 8.2
 * PROVISIONING state), triggered by INPUT_EVENT_BOOT_HOLD. Returns to
 * WIFI_CONNECTING once credentials are saved.
 */
esp_err_t net_start_provisioning(void);
