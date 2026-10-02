#include "esp_err.h"
#include "esp_log.h"

#include "app_rtc.h"
#include "buzzer.h"
#include "display.h"
#include "input.h"
#include "net.h"
#include "settings.h"
#include "timekeeping.h"
#include "ui.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "display_init: %s", esp_err_to_name(display_init()));
    ESP_LOGI(TAG, "app_rtc_init: %s", esp_err_to_name(app_rtc_init()));
    ESP_LOGI(TAG, "timekeeping_init: %s", esp_err_to_name(timekeeping_init()));
    ESP_LOGI(TAG, "input_init: %s", esp_err_to_name(input_init()));
    ESP_LOGI(TAG, "buzzer_init: %s", esp_err_to_name(buzzer_init()));
    ESP_LOGI(TAG, "settings_init: %s", esp_err_to_name(settings_init()));
    ESP_LOGI(TAG, "net_init: %s", esp_err_to_name(net_init()));
    ESP_LOGI(TAG, "ui_init: %s", esp_err_to_name(ui_init()));
}
