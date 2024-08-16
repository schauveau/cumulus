#include <stdio.h>
#include <string.h>

#include "app_events.h"
#include "app.h"

void
app_post_event(app_event_t event, const void *arg, size_t argsize) {
  esp_event_post(APP_EVENT, event, arg, argsize, portMAX_DELAY);
}

void
app_sync(void)
{
  StaticSemaphore_t sem;
  SemaphoreHandle_t sem_handle = xSemaphoreCreateBinaryStatic(&sem);
  app_event_sync_t arg = {
    .sem = sem_handle
  };
  app_post_event(APP_EVENT_SYNC, &arg, sizeof(arg) );
  xSemaphoreTake(sem_handle, portMAX_DELAY);
}

void app_post_query_state(app_state_t *state_copy, bool async)
{
  app_post_event(APP_EVENT_QUERY_STATE, &state_copy, sizeof(app_state_t*) );
  if (!async) {
    app_sync();
  }
}

void
app_post_reboot(void)
{
  app_post_event(APP_EVENT_REBOOT, NULL, 0);
}

void
app_post_mode(ac_mode_t mode)
{
  app_post_event(APP_EVENT_MODE, &mode, sizeof(mode));
}

void
app_post_frame_size(int value, bool save)
{
  app_event_frame_size_t args = { .value=value , .save=save };
  app_post_event(APP_EVENT_FRAME_SIZE, &args, sizeof(args));
}

void
app_post_auto_available_power(int value)
{
  app_post_event(APP_EVENT_AUTO_AVAILABLE_POWER, &value, sizeof(value));
}

void
app_post_auto_min_power(int value)
{
  app_post_event(APP_EVENT_AUTO_MIN_POWER, &value, sizeof(value));
}

void
app_post_auto_over_power(int value)
{
  app_post_event(APP_EVENT_AUTO_OVER_POWER, &value, sizeof(value));
}


void
app_post_manual_power(int value)
{
  app_post_event(APP_EVENT_MANUAL_POWER, &value, sizeof(value));
}


void
app_post_full_power(int value, bool save)
{
  app_event_full_power_t args = { .value=value , .save=save };
  app_post_event(APP_EVENT_FULL_POWER, &args, sizeof(args));
}

void
app_post_timezone(const char *timezone)
{
  app_post_event(APP_EVENT_TIMEZONE, timezone, strlen(timezone)+1);
}

void
app_post_hostname(const char *hostname)
{
  app_post_event(APP_EVENT_HOSTNAME, hostname, strlen(hostname)+1);
}

void
app_post_ui_password(const char *password)
{
  app_post_event(APP_EVENT_UI_PASSWORD, password, strlen(password)+1);
}

void
app_post_mqtt_uri(const char *uri)
{
  app_post_event(APP_EVENT_MQTT_URI, uri, strlen(uri)+1);
}

void app_post_button_press(char id, int duration_ms)
{
  app_event_button_t args = { .id=id, .duration_ms=duration_ms } ;
  app_post_event(APP_EVENT_BUTTON_PRESS, &args, sizeof(args));
}

void app_post_button_release(char id, int duration_ms)
{
  app_event_button_t args = { .id=id, .duration_ms=duration_ms } ;
  app_post_event(APP_EVENT_BUTTON_RELEASE, &args, sizeof(args));
}

void app_post_wifi_cred(const char *ssid, const char *password)
{
  app_wifi_cred_t cred={} ;
  cred.ssid = ssid; 
  cred.password = password; 
  app_post_event(APP_EVENT_WIFI_CRED, &cred, sizeof(cred));
}
