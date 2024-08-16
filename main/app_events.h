#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(APP_EVENT);

typedef enum {
  APP_EVENT_SYNC,                 // Wait for all previous events to be completed
  APP_EVENT_QUERY_STATE,          // Get a copy of the current state of the application
  APP_EVENT_REBOOT,               // Reboot the device.
  APP_EVENT_MODE,                 // Set the AC mode
  APP_EVENT_MANUAL_POWER,         // Set the target power (Manual mode)
  APP_EVENT_MODE_AUTO,            // Switch to Auto mode 
  APP_EVENT_AUTO_AVAILABLE_POWER, // Set the available power (Auto mode)
  APP_EVENT_AUTO_OVER_POWER,      // Set by how much to overshoot the power (Auto mode)
  APP_EVENT_AUTO_MIN_POWER,       // Set the minimum target power (Auto mode)
  APP_EVENT_FULL_POWER,           // Define the maximum possible power (when the AC relay is on at 100%)
  APP_EVENT_FRAME_SIZE,           // Define the number of cycles in each frame (i.e. 1/100 seconds for AC 50Hz)
  APP_EVENT_WIFI_CRED,            // Set WiFi credentials
  APP_EVENT_HOSTNAME,             // Set Hostname
  APP_EVENT_TIMEZONE,             // Set the Timezone (POSIX)
  APP_EVENT_MQTT_URI,             // Set the MQTT Broker URI
  APP_EVENT_UI_PASSWORD,          // Set the User Interface password (http, ...) 

  APP_EVENT_BUTTON_PRESS,         // A button is currently being pressed.
  APP_EVENT_BUTTON_RELEASE,       // A button was released.

  // The events below are internal and should not be triggered by the UI
  APP_EVENT_MINUTE_TIC,           // Triggered roughly once per minute 
  APP_EVENT_HOUR_TIC,             // Triggered every hour usually at the '0' minute mark but will also happen
                                  // at startup and after a timezone or time update if the 'hour' changed.
} app_event_t;


typedef struct {
  SemaphoreHandle_t sem;
} app_event_sync_t ;

typedef struct {
  char id;
  int  duration_ms; 
} app_event_button_t ;

typedef struct {
  int  value;
  bool save;
} app_event_frame_size_t ;

typedef struct {
  int  value;
  bool save;
} app_event_full_power_t ;


void app_post_event(app_event_t event, const void *arg, size_t argsize);

