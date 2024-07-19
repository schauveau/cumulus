#include <stdio.h>

#include "app_events.h"
#include "app.h"

void
app_post_event(app_event_t event, void *arg, size_t argsize) {
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

void
app_post_set_mode_manual(void)
{
  app_event_set_mode_manual_t arg = {};
  app_post_event(APP_EVENT_SET_MODE_MANUAL, &arg, sizeof(arg));
}

void
app_post_set_mode_auto(void) {
  app_event_set_mode_auto_t arg = {};
  app_post_event(APP_EVENT_SET_MODE_AUTO, &arg, sizeof(arg));
}

void
app_post_set_available_power(double value)
{
  app_event_set_available_power_t arg = {
    .value = value
  } ;
  app_post_event(APP_EVENT_SET_AVAILABLE_POWER, &arg, sizeof(arg));
}


