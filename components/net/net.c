#include "net.h"

esp_err_t net_init(void)
{
    // TODO(me):
    // 1. esp_netif_init(), esp_event_loop_create_default(),
    //    esp_netif_create_default_wifi_sta(), esp_wifi_init().
    // 2. Check NVS for stored WiFi credentials.
    //    - found: esp_wifi_start()/esp_wifi_connect(), move to
    //      NET_STATE_WIFI_CONNECTING.
    //    - not found: move straight to NET_STATE_OFFLINE.
    // 3. Start (or let a dedicated task run) the FSM table in
    //    docs/DESIGN.md section 8.2:
    //    - WIFI_CONNECTING -> SNTP_SYNCING on connect, -> OFFLINE after 15s.
    //    - SNTP_SYNCING: use esp_sntp / esp_netif_sntp, on success call
    //      timekeeping_set_time() then move to SYNCED; after 3 failures
    //      move to OFFLINE.
    //    - SYNCED: re-sync every hour; on WiFi disconnect event go back to
    //      WIFI_CONNECTING.
    //    - OFFLINE: retry WIFI_CONNECTING every 5 minutes.
    // 4. Keep the current state in a variable net_get_state() can read
    //    (guard it if the FSM runs on its own task).
    return ESP_OK;
}

net_state_t net_get_state(void)
{
    // TODO(me): return the FSM's current state.
    return NET_STATE_OFFLINE;
}

esp_err_t net_start_provisioning(void)
{
    // TODO(me):
    // 1. Move the FSM to NET_STATE_PROVISIONING.
    // 2. Start network_provisioning in SoftAP mode (espressif/network_provisioning).
    //    If using security v1, CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_1=y
    //    must be set in sdkconfig.defaults (docs/DESIGN.md section 8.2).
    // 3. On NETWORK_PROV_CRED_RECV / end event, save credentials to NVS and
    //    move to NET_STATE_WIFI_CONNECTING.
    return ESP_OK;
}
