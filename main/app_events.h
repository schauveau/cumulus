#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(APP_EVENT);

typedef enum {
  APP_EVENT_SYNC,
  APP_EVENT_SET_MODE_MANUAL,
  APP_EVENT_SET_MODE_AUTO,
  APP_EVENT_SET_AVAILABLE_POWER,
  APP_EVENT_MINUTE_TIC, // Occur roughly once per minute 
  APP_EVENT_HOUR_TIC,   // Occur every hour usually at the '0' minute mark but can also happen after a timezone or time update if the 'hour' changed.
} app_event_t;

typedef struct {
  SemaphoreHandle_t sem;
} app_event_sync_t ;

typedef struct {
} app_event_set_mode_auto_t ;

typedef struct {
} app_event_set_mode_manual_t ;

typedef struct {
  double value; 
} app_event_set_available_power_t;


void app_post_event(app_event_t event, void *arg, size_t argsize) ;

