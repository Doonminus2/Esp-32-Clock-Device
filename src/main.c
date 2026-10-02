#include <stdio.h>
#include "max7219.h"
#include "ds1302.h"
#include "iot_button.h"
#include "button_gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"  
#include "esp_log.h"

void app_main(void)
{
   while(1) {
      printf("libs OK\n");
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}   