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

// For APP_EVENT_EVERY_MINUTE and APP_EVENT_EVERY_MINUTE, the argument is
// a 'struct tm' populated with the local time. 
//
typedef struct {
} app_event_minute_tic_t ;

typedef struct {
} app_event_hour_tic_t ;


