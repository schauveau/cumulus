
#include "app_events.h"
#include "app.h"

void
app_sync(void)
{
  StaticSemaphore_t sem;
  SemaphoreHandle_t sem_handle = xSemaphoreCreateBinaryStatic(&sem);
  app_event_sync_t arg = {
    .sem = sem_handle
  };  
  esp_event_post(APP_EVENT, APP_EVENT_SYNC, &arg, sizeof(arg), portMAX_DELAY);
  xSemaphoreTake(sem_handle, portMAX_DELAY);
}

void
app_async_set_mode_manual(void)
{
  app_event_set_mode_manual_t arg = {};
  esp_event_post(APP_EVENT, APP_EVENT_SET_MODE_MANUAL, &arg, sizeof(arg), portMAX_DELAY);
}

void
app_async_set_mode_auto(void) {
  app_event_set_mode_auto_t arg = {};
  esp_event_post(APP_EVENT, APP_EVENT_SET_MODE_AUTO, &arg, sizeof(arg), portMAX_DELAY);
}

void
app_async_minute_tic(void) {
  app_event_minute_tic_t arg = {};
  esp_event_post(APP_EVENT, APP_EVENT_MINUTE_TIC, &arg, sizeof(arg), portMAX_DELAY);
}

void
app_async_hour_tic(void) {
  app_event_hour_tic_t arg = {};
  esp_event_post(APP_EVENT, APP_EVENT_HOUR_TIC, &arg, sizeof(arg), portMAX_DELAY);
}


