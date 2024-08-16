#include <math.h>
#include "freertos/FreeRTOS.h"
#include "esp_event.h"
#include "esp_log.h"

#include "mqtt_client.h"
#include "cJSON.h"

// Local includes
#include "app.h"
#include "ui_mqtt.h"

static const char TAG[] = "ui_mqtt";

#define MATCH_TOPIC(EVENT,TOPIC) ((strlen(TOPIC)==EVENT->topic_len) && memcmp(EVENT->topic,TOPIC,EVENT->topic_len)==0)

#define  TOPIC_ENERGY_METER "zigbee2mqtt/energy_meter"

#define QOS_0 0
#define QOS_1 1
#define QOS_2 2

static char *topic_set;  // "hostname"
static char *topic_reboot;  // "hostname/reboot"

static void process_energy_meter_msg(cJSON *root, esp_mqtt_client_handle_t client) {
    
  double power_a = cJSON_GetNumberValue( cJSON_GetObjectItemCaseSensitive(root,"power_a") ) ;
  double power_b = cJSON_GetNumberValue( cJSON_GetObjectItemCaseSensitive(root,"power_b") ) ;

  if ( isnan(power_a) ) {
    ESP_LOGW(TAG, "Missing or malformed 'power_a'");
    return;
  }

  if ( isnan(power_b) ) {
    ESP_LOGW(TAG, "Missing or malformed 'power_b'");
    return;
  }

  int available_power = power_b-power_a ;
  app_post_auto_available_power(available_power); 
  
  ESP_LOGI(TAG, "Energy meter: available_power = %d", available_power);

  // My energy has a tendancy to reset its update_frequency to 10s about once a day.   
  int update_frequency = cJSON_GetNumberValue( cJSON_GetObjectItemCaseSensitive(root,"update_frequency") ) ;  
  if (update_frequency==10) {
    ESP_LOGW(TAG, "Reset Energy Meter update frequency to 3");
    esp_mqtt_client_publish(client, TOPIC_ENERGY_METER "/set/update_frequency" , "3", 0, QOS_0, false);
  }
  
}

static void process_set_msg(cJSON *root)
{
    
    double frame_size   = cJSON_GetNumberValue( cJSON_GetObjectItemCaseSensitive(root,"frame_size") ) ;
    if ( !isnan(frame_size) ) {
      app_post_frame_size(frame_size);
    }

    double full_power   = cJSON_GetNumberValue( cJSON_GetObjectItemCaseSensitive(root,"full_power") ) ;
    if ( !isnan(full_power) ) {
      app_post_full_power(full_power);
    }

    double manual_power = cJSON_GetNumberValue( cJSON_GetObjectItemCaseSensitive(root,"manual_power") ) ;
    if ( !isnan(manual_power) ) {
      app_post_manual_power(manual_power);
    }
    
    double auto_min_power  = cJSON_GetNumberValue( cJSON_GetObjectItemCaseSensitive(root,"auto_min_power") ) ;
    if ( !isnan(auto_min_power) ) {
      app_post_auto_min_power(auto_min_power);
    }

    double auto_over_power = cJSON_GetNumberValue( cJSON_GetObjectItemCaseSensitive(root,"auto_over_power") ) ;
    if ( !isnan(auto_over_power) ) {
      app_post_auto_over_power(auto_over_power);
    }

    const char * mode   = cJSON_GetStringValue( cJSON_GetObjectItemCaseSensitive(root,"mode") ) ;
    if (mode) {
      if (!strcmp(mode,"auto"))
        app_post_mode(AC_MODE_AUTO);
      else if (!strcmp(mode,"manual"))
        app_post_mode(AC_MODE_MANUAL);
    }
    
}

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}



/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t) event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
    {
      ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
      //msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 0);
      //ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
      const char * topic;
      
      topic = TOPIC_ENERGY_METER ;
      msg_id = esp_mqtt_client_subscribe(client, topic, 1);
      ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d  topic='%s'", msg_id, topic);
      
      topic = topic_set ;
      msg_id = esp_mqtt_client_subscribe(client, topic, 1);
      ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d  topic='%s'", msg_id, topic);
      
      topic = topic_reboot;          
      msg_id = esp_mqtt_client_subscribe(client, topic, 1);
      ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d  topic='%s'", msg_id, topic);
    }      
    //msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
    //ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
    break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
      ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
      //msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
      //ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
      break;
    case MQTT_EVENT_UNSUBSCRIBED:
      ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
      break;
    case MQTT_EVENT_PUBLISHED:
      ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
      break;

    case MQTT_EVENT_DATA:
      //ESP_LOGI(TAG, "MQTT_EVENT_DATA");
      if (MATCH_TOPIC(event,TOPIC_ENERGY_METER)) {
        cJSON *root = cJSON_Parse(event->data);
        process_energy_meter_msg(root, client) ;
        cJSON_Delete(root);
      } else if (MATCH_TOPIC(event,topic_set)) {
        cJSON *root = cJSON_Parse(event->data);
        process_set_msg(root) ;
        cJSON_Delete(root);
      } else if (MATCH_TOPIC(event,topic_reboot)) {
        app_post_reboot();
      }
      break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "MQTT other event %d", event->event_id);
        break;
    }
}


void ui_mqtt_start(const char *uri, const char *hostname)
{
  asprintf(&topic_set,"%s/set",hostname);
  asprintf(&topic_reboot,"%s/reboot",hostname);
  
  // Warning: LWIP DNS is only using mDNS to resolve hostnames that end with '.local'
  //          so that won't work if your '.local' hostnames are manually configured.
  //           ==> So do not use .local for your local domain.
  //           ==> alternatively, disable LWIP_DNS_SUPPORT_MDNS_QUERIES in config
  //               to fall back to IPV4 name resolution.
  
  esp_mqtt_client_config_t mqtt_cfg = {
    .broker = {
      .address = {
        .uri = uri, 
      }
    }
  };
  ESP_LOGI(TAG, "MQTT URI: %s", mqtt_cfg.broker.address.uri);
  
  esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
  /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
  esp_mqtt_client_register_event(client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
  esp_mqtt_client_start(client);
}

