/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * Zigbee switch driver example
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "button_driver.h"
#include "esp_event.h"

static const char TAG[] = "button";

ESP_EVENT_DEFINE_BASE(APP_EVENT);

//
// A queue of 'const button_info_t*'
//
static QueueHandle_t buttons_gpio_queue = NULL;


static void button_driver_enable_interrupts()
{
    for (int i = 0; i < BUTTON_COUNT; ++i) {
      gpio_intr_enable( project_buttons[i].gpio );
    }
}
static void button_driver_disable_interrupts()
{
    for (int i = 0; i < BUTTON_COUNT; ++i) {
      gpio_intr_disable( project_buttons[i].gpio );
    }
}

static void IRAM_ATTR button_driver_isr_handler(void *arg)
{
    button_driver_disable_interrupts();
    xQueueSendFromISR(buttons_gpio_queue, &arg, NULL);
}

static void button_driver_task(void *arg)
{

  while(true) {
    const int delay_ms = 100 ;
    const button_info_t *button = NULL;

    button_driver_enable_interrupts();

    // ESP_LOGI(TAG, "at xQueueReceive");

    if ( !xQueueReceive(buttons_gpio_queue, &button, portMAX_DELAY) )
      continue;

    // button_driver_disable_interrupts(); // Already done in gpio_isr_handler so probably not needed.
    
    int count=0;
    while ( gpio_get_level(button->gpio) == button->active_level ) {
      vTaskDelay( delay_ms / portTICK_PERIOD_MS);
      count++;
    }
    
    // The press was so short that we could not even detect it.
    // This is probably noise so ignore it.
    // Humm... I am not so sure. The reason could also be that
    // the task was slow to react (sleep or heavy load)
    if (count==0) {
      ESP_LOGI(TAG, "Button: Ignore very short press");
      continue;
    }

    // TODO: count*delay_ms may not be an accurate method to measure
    //       the time. Should use a realtime clock instead.  
    
    project_button_handler(button, count*delay_ms);

  }
}


bool button_driver_init()
{
    for (int i = 0; i < BUTTON_COUNT; ++i) {
      const button_info_t * button = &project_buttons[i] ;
      gpio_set_direction(button->gpio, GPIO_MODE_INPUT);
      gpio_set_pull_mode(button->gpio, button->active_level==1 ? GPIO_PULLDOWN_ONLY : GPIO_PULLUP_ONLY );
      gpio_set_intr_type(button->gpio, button->active_level==1 ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL );
    }

    // Create a queue to handle gpio event from isr 
    buttons_gpio_queue = xQueueCreate(10, sizeof(const button_info_t*));
    if ( buttons_gpio_queue == 0) {
        ESP_LOGE(TAG, "Button Queue could not be created");
        return false;
    }

    xTaskCreate(button_driver_task, "button_driver", 4096, NULL, 10, NULL);

    // Start gpio isr service
    //
    // TODO:
    //   - Handle errors.
    //   - the GPIO ISR service can be shared with other part of the code
    //     so gpio_install_isr_service() should probably be done elsewhere.
    //         ==> or allow returns ESP_ERR_INVALID_STATE
    //              
    
    gpio_install_isr_service(0);

    // Install gpio interrupt handlers
    // TODO:
    //  -  Handle errors
    for (int i = 0; i < BUTTON_COUNT; ++i) {
      gpio_isr_handler_add( project_buttons[i].gpio,
                            button_driver_isr_handler,
                            (void *) &project_buttons[i] );
    }
    
    return true;
}
