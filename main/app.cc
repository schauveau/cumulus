#include "app_events.h"


ESP_EVENT_DEFINE_BASE(APP_EVENT);


static void app_event_sync(app_event_sync_t *arg)
{
  xSemaphoreGive(arg->sem);
}

static void app_event_set_mode_auto(app_event_set_mode_auto_t *arg)
{
  // TODO
}


static void app_event_set_mode_manual(app_event_set_mode_manual_t *arg)
{
  // TODO
}


static void app_event_minute_tic(app_event_minute_tic_t *arg)
{
  // TODO
}

static void app_event_hour_tic(app_event_hour_tic_t *arg)
{
  // TODO
}


static void app_event_handler(void* dummy,
                              esp_event_base_t event_base,
                              int32_t event_id,
                              void* data)
{  
  switch(event_id) {
    case APP_EVENT_SYNC:
      app_event_sync( (app_event_sync_t*) data ) ;
      break;
    case APP_EVENT_SET_MODE_AUTO:
      app_event_set_mode_auto( (app_event_set_mode_auto_t*) data ) ;
      break;
    case APP_EVENT_SET_MODE_MANUAL:
      app_event_set_mode_manual( (app_event_set_mode_manual_t*) data ) ;
      break;
    case APP_EVENT_MINUTE_TIC:
      app_event_minute_tic( (app_event_minute_tic_t*) data ) ;
      break;
    case APP_EVENT_HOUR_TIC:
      app_event_hour_tic( (app_event_hour_tic_t*) data ) ;
      break;
     
  }
}
  

void app_init(void)
{
  esp_event_handler_instance_t instance_app_id;

  ESP_ERROR_CHECK(esp_event_handler_instance_register(APP_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      &app_event_handler,
                                                      NULL,
                                                      &instance_app_id));

}

