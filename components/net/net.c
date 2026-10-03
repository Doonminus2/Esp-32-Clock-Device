/*
 * net.c — WiFi + SNTP + ตั้งค่า WiFi ผ่านมือถือ (SoftAP provisioning)
 *
 * Net FSM (DESIGN.md 8.2) รันใน task "net" ของตัวเอง
 * การต่อ WiFi ช้าหรือค้างแค่ไหน task ui ก็ยังวาดนาฬิกาต่อได้
 *
 *   IDLE ─┬─ มีชื่อ/รหัส WiFi ──► WIFI_CONNECTING ─ ได้ IP ─► SNTP_SYNCING ─ ได้เวลา ─► SYNCED
 *         └─ ไม่มี ─────────────► OFFLINE ◄── เกิน 15 วิ ──┘        │ พลาด 3 ครั้ง        │ ครบ 1 ชม. → SNTP
 *                                  │ ▲                            ▼                       │ WiFi หลุด → CONNECTING
 *                                  │ └────────────────────────────┘
 *                                  └─ ครบ 5 นาที → WIFI_CONNECTING
 *   (ทุก state) ── ui ขอ provisioning ──► PROVISIONING ─ ได้ชื่อ/รหัสแล้ว ─► WIFI_CONNECTING
 *
 * event จากระบบ WiFi มาถึง "คนละ task" กับ FSM เราจึงสื่อสารผ่าน Event Group:
 * กระดานที่มีไฟ (bit) หลายดวง handler เปิดไฟ ส่วน FSM นั่งรอไฟดวงที่สนใจ
 */
#include "net.h"

#include <stdbool.h>
#include <sys/time.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_softap.h"
#include "timekeeping.h"

static const char *TAG = "net";

// ---------------------------------------------------------------------------
// ค่าตั้ง
// ---------------------------------------------------------------------------

#define NET_CONNECT_TIMEOUT_MS  15000             // ต่อ WiFi ไม่ได้ใน 15 วิ → OFFLINE
#define NET_OFFLINE_RETRY_MS    (5 * 60 * 1000)   // OFFLINE แล้วลองใหม่ทุก 5 นาที
#define NET_RESYNC_MS           (60 * 60 * 1000)  // SYNCED แล้วขอเวลาใหม่ทุก 1 ชม.
#define NET_SNTP_WAIT_MS        10000             // รอคำตอบ NTP ครั้งละ 10 วิ
#define NET_SNTP_MAX_TRIES      3
#define NET_SNTP_SERVER         "pool.ntp.org"

#define NET_PROV_SERVICE_NAME   "CLOCK-SETUP"     // ชื่อ WiFi ที่นาฬิกาปล่อยตอนตั้งค่า
#define NET_PROV_POP            "clock1234"       // รหัส PoP ที่ต้องพิมพ์ในแอปมือถือ

#define NET_TASK_STACK          4096
#define NET_TASK_PRIO           4

// "ไฟ" แต่ละดวงใน Event Group
#define BIT_GOT_IP              BIT0   // ได้ IP แล้ว (ต่อ WiFi สำเร็จ)
#define BIT_DISCONNECTED        BIT1   // WiFi หลุด
#define BIT_PROV_REQUEST        BIT2   // ui ขอเข้าโหมดตั้งค่า WiFi
#define BIT_PROV_DONE           BIT3   // provisioning จบแล้ว

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------

// volatile: ui task อ่าน ส่วน net task เขียน
// enum 32 บิตบน ESP32 อ่าน/เขียนได้ในคำสั่งเดียว จึงไม่ต้องใช้ mutex
static volatile net_state_t s_state = NET_STATE_IDLE;
static volatile bool s_prov_pending = false; // ui ขอแล้ว แต่ task ยังไม่ได้เริ่ม
static EventGroupHandle_t s_events = NULL;
static bool s_sntp_started = false;

static const char *const STATE_NAMES[] = {
    "IDLE", "WIFI_CONNECTING", "SNTP_SYNCING", "SYNCED", "OFFLINE", "PROVISIONING",
};

static void set_state(net_state_t next)
{
    if (next != s_state) {
        ESP_LOGI(TAG, "%s -> %s", STATE_NAMES[s_state], STATE_NAMES[next]);
        s_state = next;
    }
}

// รอไฟดวงใดดวงหนึ่งใน `bits` (ไม่ดับไฟให้) หรือจนหมดเวลา
static EventBits_t wait_bits(EventBits_t bits, uint32_t timeout_ms)
{
    return xEventGroupWaitBits(s_events, bits, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
}

// ---------------------------------------------------------------------------
// event handler (รันใน task ของ event loop ไม่ใช่ net task)
// ---------------------------------------------------------------------------

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, BIT_GOT_IP);
        xEventGroupSetBits(s_events, BIT_DISCONNECTED);
        // ระหว่างช่วง 15 วิของ WIFI_CONNECTING ให้ลองต่อซ้ำเรื่อยๆ
        if (s_state == NET_STATE_WIFI_CONNECTING) {
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_events, BIT_GOT_IP);
    } else if (base == NETWORK_PROV_EVENT) {
        if (id == NETWORK_PROV_WIFI_CRED_RECV) {
            wifi_sta_config_t *sta = (wifi_sta_config_t *)data;
            ESP_LOGI(TAG, "provisioning: got WiFi name \"%s\", trying it", (const char *)sta->ssid);
        } else if (id == NETWORK_PROV_WIFI_CRED_FAIL) {
            ESP_LOGW(TAG, "provisioning: could not join, check the password in the app");
        } else if (id == NETWORK_PROV_WIFI_CRED_SUCCESS) {
            ESP_LOGI(TAG, "provisioning: joined WiFi, credentials saved");
        } else if (id == NETWORK_PROV_END) {
            xEventGroupSetBits(s_events, BIT_PROV_DONE);
        }
    }
}

// ---------------------------------------------------------------------------
// ตัวช่วย
// ---------------------------------------------------------------------------

// มีชื่อ WiFi เก็บไว้ใน NVS หรือยัง (esp_wifi_init โหลดค่ามาจาก NVS ให้แล้ว)
static bool has_credentials(void)
{
    wifi_config_t cfg;
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK) {
        return false;
    }
    return cfg.sta.ssid[0] != '\0';
}

static void go_offline(void)
{
    set_state(NET_STATE_OFFLINE); // เปลี่ยน state ก่อน handler จะได้ไม่ต่อใหม่เอง
    esp_wifi_disconnect();
}

// SNTP ตั้ง system clock ให้แล้ว → เขียนต่อลง RTC ที่ขอบวินาที
// (timekeeping_set_time ตัดเศษวินาทีทิ้ง จึงรอให้เศษเกือบเป็น 0 ก่อน)
static void save_time_to_rtc(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    vTaskDelay(pdMS_TO_TICKS(1000 - tv.tv_usec / 1000));

    struct tm now;
    timekeeping_now(&now);
    timekeeping_set_time(&now);
    ESP_LOGI(TAG, "SNTP time %02d:%02d:%02d written to RTC", now.tm_hour, now.tm_min, now.tm_sec);
}

// ---------------------------------------------------------------------------
// แต่ละ state ของ FSM: ทำงานของ state นั้น แล้วเลือก state ถัดไป
// ทุกตัวรอ BIT_PROV_REQUEST ด้วย เพื่อให้ปุ่มตั้งค่า WiFi ตัดเข้ามาได้ทันที
// ---------------------------------------------------------------------------

static void run_connecting(void)
{
    xEventGroupClearBits(s_events, BIT_GOT_IP | BIT_DISCONNECTED);
    esp_wifi_connect();

    EventBits_t bits = wait_bits(BIT_GOT_IP | BIT_PROV_REQUEST, NET_CONNECT_TIMEOUT_MS);
    if (bits & BIT_PROV_REQUEST) {
        return;
    }
    if (bits & BIT_GOT_IP) {
        set_state(NET_STATE_SNTP_SYNCING);
    } else {
        ESP_LOGW(TAG, "no WiFi after %d s", NET_CONNECT_TIMEOUT_MS / 1000);
        go_offline();
    }
}

static void run_sntp(void)
{
    if (!s_sntp_started) {
        esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(NET_SNTP_SERVER);
        esp_netif_sntp_init(&cfg);  // เริ่มถามเวลาทันที (cfg.start = true)
        s_sntp_started = true;
    } else {
        esp_netif_sntp_start();     // ถามใหม่
    }

    for (int attempt = 1; attempt <= NET_SNTP_MAX_TRIES; attempt++) {
        if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(NET_SNTP_WAIT_MS)) == ESP_OK) {
            save_time_to_rtc();     // system clock ถูกตั้งแล้วโดย SNTP
            set_state(NET_STATE_SYNCED);
            return;
        }
        ESP_LOGW(TAG, "SNTP attempt %d/%d timed out", attempt, NET_SNTP_MAX_TRIES);

        EventBits_t bits = xEventGroupGetBits(s_events);
        if (bits & BIT_PROV_REQUEST) {
            return;
        }
        if (!(bits & BIT_GOT_IP)) {  // WiFi หลุดระหว่างรอ
            set_state(NET_STATE_WIFI_CONNECTING);
            return;
        }
        esp_netif_sntp_start();
    }
    go_offline();
}

static void run_synced(void)
{
    EventBits_t bits = wait_bits(BIT_DISCONNECTED | BIT_PROV_REQUEST, NET_RESYNC_MS);
    if (bits & BIT_PROV_REQUEST) {
        return;
    }
    if (bits & BIT_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi lost");
        set_state(NET_STATE_WIFI_CONNECTING);
    } else {
        set_state(NET_STATE_SNTP_SYNCING); // ครบ 1 ชม.
    }
}

static void run_offline(void)
{
    EventBits_t bits = wait_bits(BIT_PROV_REQUEST, NET_OFFLINE_RETRY_MS);
    if (bits & BIT_PROV_REQUEST) {
        return;
    }
    if (has_credentials()) {
        set_state(NET_STATE_WIFI_CONNECTING);
    }
    // ไม่มีชื่อ WiFi → อยู่ OFFLINE ต่อ (วนกลับมารอใหม่อีก 5 นาที)
}

static void run_provisioning(void)
{
    xEventGroupClearBits(s_events, BIT_PROV_DONE);
    esp_wifi_stop(); // ให้ manager เป็นคนเปิด WiFi ใหม่ในโหมด AP+STA

    network_prov_mgr_config_t cfg = {
        .scheme = network_prov_scheme_softap,
        .scheme_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE,
    };
    esp_err_t err = network_prov_mgr_init(cfg);
    if (err == ESP_OK) {
        // security 1 = เข้ารหัส + ต้องใส่ PoP (ต้องเปิดใน sdkconfig ดู sdkconfig.defaults)
        err = network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1, NET_PROV_POP,
                                                  NET_PROV_SERVICE_NAME, NULL);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "provisioning failed to start: %s", esp_err_to_name(err));
        network_prov_mgr_deinit();
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
        s_prov_pending = false;
        go_offline();
        return;
    }

    ESP_LOGI(TAG, "SETUP: on your phone open the \"ESP SoftAP Provisioning\" app,");
    ESP_LOGI(TAG, "       join WiFi \"%s\", PoP = %s", NET_PROV_SERVICE_NAME, NET_PROV_POP);

    // รอจนกว่าผู้ใช้ส่งชื่อ/รหัส WiFi มาและต่อสำเร็จ (manager ปิดตัวเอง ~30 วิหลังสำเร็จ)
    xEventGroupWaitBits(s_events, BIT_PROV_DONE, pdTRUE, pdFALSE, portMAX_DELAY);
    network_prov_mgr_deinit();

    // กลับเป็น STA ธรรมดา แล้วให้ FSM ต่อใหม่ตามขั้นตอนปกติ
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));
    set_state(NET_STATE_WIFI_CONNECTING);
    s_prov_pending = false;
}

// ---------------------------------------------------------------------------
// task
// ---------------------------------------------------------------------------

static void net_task(void *arg)
{
    set_state(has_credentials() ? NET_STATE_WIFI_CONNECTING : NET_STATE_OFFLINE);

    while (1) {
        // คำขอตั้งค่า WiFi มาก่อนทุกอย่าง
        if (xEventGroupGetBits(s_events) & BIT_PROV_REQUEST) {
            xEventGroupClearBits(s_events, BIT_PROV_REQUEST);
            set_state(NET_STATE_PROVISIONING);
        }

        switch (s_state) {
        case NET_STATE_WIFI_CONNECTING: run_connecting();   break;
        case NET_STATE_SNTP_SYNCING:    run_sntp();         break;
        case NET_STATE_SYNCED:          run_synced();       break;
        case NET_STATE_OFFLINE:         run_offline();      break;
        case NET_STATE_PROVISIONING:    run_provisioning(); break;
        default:                        go_offline();       break;
        }
    }
}

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

esp_err_t net_init(void)
{
    s_events = xEventGroupCreate();
    if (s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // ชั้น network ของ ESP-IDF: netif = "การ์ดแลน" เสมือน, event loop = ไปรษณีย์ของ event
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    esp_netif_create_default_wifi_sta(); // นาฬิกาไปต่อเราเตอร์
    esp_netif_create_default_wifi_ap();  // นาฬิกาปล่อย WiFi เอง (ใช้ตอน SETUP)

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_cfg), TAG, "esp_wifi_init");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                                   on_event, NULL), TAG, "handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                   on_event, NULL), TAG, "handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID,
                                                   on_event, NULL), TAG, "handler");

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set_mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start");

    if (xTaskCreate(net_task, "net", NET_TASK_STACK, NULL, NET_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

net_state_t net_get_state(void)
{
    // ระหว่างที่ ui ขอไปแล้วแต่ net task ยังไม่ทันเริ่ม ให้ตอบ PROVISIONING ไปก่อน
    return s_prov_pending ? NET_STATE_PROVISIONING : s_state;
}

esp_err_t net_start_provisioning(void)
{
    if (s_events == NULL) {
        return ESP_ERR_INVALID_STATE; // net_init() ไม่ผ่าน
    }
    s_prov_pending = true;
    xEventGroupSetBits(s_events, BIT_PROV_REQUEST);
    return ESP_OK;
}