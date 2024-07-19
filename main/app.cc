#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wps.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include <esp_http_server.h>
#include "esp_mac.h"

#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"

#include "led_strip.h"

#include "mqtt_client.h"
#include "cJSON.h"

#include "lwip/err.h"
#include "lwip/sys.h"

// local includes 
#include "app.h"
#include "resource.h"
#include "wifi_manager.h"
#include "project.h"
#include "button_driver.h"
#include "acr.h"
#include "telnet.h"
#include "http_ui.h"
#include "rgb_led.h"

#include "app_events.h"
static const char TAG[] = "app" ;

ESP_EVENT_DEFINE_BASE(APP_EVENT);

#define STATIC_ARRAY_SIZE(X)  (sizeof(X)/sizeof(X[0]))

// The GPIO for the RGB led 
#define RGB_LED_GPIO  CONFIG_RGB_GPIO

// 1000000 ticks = 1s , 1000 ticks = 1ms , 1 tick = 1us so timer frequency is 1MHz,  
#define TIMER_RESOLUTION (1000*1000) 
// AC frequency is typically 50 or 60 hertz 
#define AC_FREQ 50


const button_info_t project_buttons[BUTTON_COUNT] = {
  {
    // The primary button 
    .id = 'A',
    .gpio = (gpio_num_t) CONFIG_BUTTON_A_GPIO,
    .active_level = 0
  },
  {
    // The secondary button (can be the Boot button on the board)
    .id = 'B',
    .gpio = (gpio_num_t) CONFIG_BUTTON_B_GPIO,
    .active_level = 0
  },
};


typedef enum {
  AC_MODE_AUTO,    // Use information provided by energy_meter (via MQTT) to adjust the production_target
  AC_MODE_MANUAL,  // Set to a fixed ratio  
} ac_mode_t;

static ac_mode_t ac_mode = AC_MODE_AUTO ; 

static bool         app_nvs_ok = false ;
static nvs_handle_t app_nvs;

double app_auto_available_power = 0; // in AUTO mode, this is how much power is currently available
double app_auto_over_power = 40 ;    // in AUTO mode, overshoot power by that amount.
double app_auto_min_power  = 0 ;     // in AUTO mode, do not produce less than this value.

double app_manual_power    = 0 ;     // in MANUAL mode, this is the target power

char    app_hostname[32]; // The hostname (saved in nvs)
int32_t app_full_power;   // The estimated AC power when 100% ON (saved in nvs)
int32_t app_frame_size;   // The default frame size (saved in nvs). 

// The current state of the WiFi 
typedef enum {
  APP_WIFI_OK,             // Connected to an access point
  APP_WIFI_FAIL,           // Not connected
  APP_WIFI_CONNECTING,     // Currently trying to connect using SSID & PASSWORD
  APP_WIFI_WPS,            // Currently trying to connect using WPS
} app_wifi_state_t ;


static app_wifi_state_t app_wifi_state = APP_WIFI_FAIL ;

// Number of minutes to wait before attempting to reconnect.
// Must be re-set when setting app_wifi_state=APP_WIFI_FAIL.
// Set to -1 to disable auto-reconnect
static int              app_wifi_reconnect_delay=0;

// Values for app_wifi_reconnect_delay
#define RECONNECT_DELAY_DISCONNECT      1 
#define RECONNECT_DELAY_FAIL_CONNECTION 1

// Number of connections remaining connection attempts.
static int              app_wifi_retry = 0;

//
// WPS
//

static int wps_index = 0; // current index in wps_cred
static int wps_count = 0; // number of credentials stored in wps_cred (0..MAX_WPS_AP_CRED)
static wifi_config_t wps_cred[MAX_WPS_AP_CRED];

//
//
//
enum {
  CMD_SET_HOSTNAME,
  CMD_SET_FULL_POWER,
  CMD_SET_DEFAULT_FRAME_SIZE,
  CMD_TIMEZONE,
  CMD_HSV,
  CMD_RGB,
};


static void on_wifi_connected() ;
static void on_wifi_disconnected() ;
static void on_wifi_fail_connection() ;

static void app_telnet_cmd_callback(telnet_server_t *server, const telnet_command_t *cmd, int narg, const telnet_argval_t *arg)
{
  switch(cmd->id) {
    case CMD_SET_HOSTNAME:
      set_hostname(arg[0].s);
      telnet_printf(server,"Hostname set to %s\n",app_hostname);
      break;
    case CMD_SET_FULL_POWER: 
      set_full_power(arg[0].i);
      telnet_printf(server,"AC Max Power set to %d\n",app_full_power);
      break;
    case CMD_SET_DEFAULT_FRAME_SIZE:
      set_default_frame_size(arg[0].i);
      telnet_printf(server,"Default Frame Size set to %d\n",app_frame_size);
      break;
    case CMD_TIMEZONE:
    {
      time_t now;
      struct tm timeinfo;
      char output[64];
      telnet_printf(server,"Current timezone: %s\n",arg[0].s);
      time(&now);
      localtime_r(&now, &timeinfo);
      strftime(output, sizeof(output), "%c", &timeinfo);
      telnet_printf(server,"Local time is %s\n",output);
      
      if (narg==1) {
        telnet_printf(server,"New timezone: %s\n",arg[0].s);
        setenv("TZ",arg[0].s,1);
        tzset();
        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(output, sizeof(output), "%c", &timeinfo);
        telnet_printf(server,"Local time is %s\n",output);
      }
    }
    break;
    case CMD_HSV:
    {
      int h = arg[0].i;
      int s = arg[1].i;
      int v = arg[2].i;      
      telnet_printf(server,"Set H=%d S=%d V=%d\n",h,s,v);
      led_set_hsv(h,s,v);
    }
    break;
    case CMD_RGB:  
    {
      int r = arg[0].i & 0xFF ;
      int g = arg[1].i & 0xFF ;
      int b = arg[2].i & 0xFF ;
      telnet_printf(server,"Set R=%d G=%d B=%d\n",r,g,b);
      led_set_rgb((r<<16)|(g<<8)|(b));
    }
    break;
  }
}

static telnet_command_t app_telnet_commands[] = {
  TELNET_DEFAULT_COMMANDS ,
  {
    .id = CMD_SET_HOSTNAME,
    .name="set-hostname" ,
    .args="w",
    .callback=app_telnet_cmd_callback,
    .help={
      "Set the hostname",
      "HOSTNAME",
      "The provided hostname is trucated to 31 characters"
    }
  },
  {
    .id = CMD_SET_FULL_POWER,
    .name="set-full-power" ,
    .args="i",
    .callback=app_telnet_cmd_callback,
    .help={
      "Set the estimated power when running at full capacity",
      "VALUE",
      "The provided value must be greater than 0"
    }
  },
  {
    .id = CMD_SET_DEFAULT_FRAME_SIZE,
    .name="set-default-frame-size" ,
    .args="i",
    .callback=app_telnet_cmd_callback,
    .help={
      "Set the default-frame-size",
      "VALUE",
    }
  },
  {
    .id = CMD_TIMEZONE,
    .name="timezone" ,
    .args="w",
    .callback=app_telnet_cmd_callback,
    
    .help={
      "Get or set the timezone",
      "[TIMEZONE]",
      "The TIMEZONE specification should follow the POSIX format.\n"
      "See 'man tzset' for more info\n"
    }
  },
  {
    .id = CMD_HSV,
    .name="hsv" ,
    .args="iii",
    .callback=app_telnet_cmd_callback,
    .help={
      "Set the led color using HSV",
      "HUE SATURATION VALUE",
      "HUE 0..360\n"
      "SATURATION 0..255\n"
      "VALUE 0..255\n"
    }
  },
  {
    .id = CMD_RGB,
    .name="rgb" ,
    .args="iii",
    .callback=app_telnet_cmd_callback,
    .help={
      "Set the led color using RGB",
      "RGB",
      "\n"
    }
  },  
} ;


static telnet_server_t app_telnet_server = {
  .port = 23, 
  .commands = app_telnet_commands,
  .command_count = STATIC_ARRAY_SIZE(app_telnet_commands),
} ;

static int button_a_count = 0 ;
static int button_b_count = 0 ;

void project_button_handler(const button_info_t *button, int duration_ms)
{
  switch (button->id) {
    case 'A':
      button_a_count = button_a_count+1 ;
      ESP_LOGW(TAG, "Button A duration=%d", duration_ms);
      break;
    case 'B':
      button_b_count = button_b_count+1  ; 
      ESP_LOGW(TAG, "Button B duration=%d", duration_ms);
      break;
    default:
      ESP_EARLY_LOGW(TAG, "Unknown button id=%d pin=%d duration=%d", button->id, button->gpio, duration_ms);
      break;
  }
}

// Obtain a string value from NVS to a buffer.
// 
void app_nvs_get_str( const char *key, char *buffer, size_t bufsize, const char *default_value)
{
  size_t len = bufsize;
  if (app_nvs_ok) { 
    if (nvs_get_str(app_nvs, key, buffer, &len)==ESP_OK) {
      return;
    }
  }
  // Fallback to the default value
  strncpy(buffer, default_value, bufsize-1) ;
  buffer[bufsize-1]='\0';
}

uint32_t app_nvs_get_u32( const char *key, uint32_t default_value)
{  
  if (app_nvs_ok) {
    uint32_t value;
    if (nvs_get_u32(app_nvs, key, &value)==ESP_OK) {
      return value;
    }
  }
  return default_value;  
}

int32_t app_nvs_get_i32( const char *key, int32_t default_value)
{  
  if (app_nvs_ok) {
    int32_t value;
    if (nvs_get_i32(app_nvs, key, &value)==ESP_OK) {
      return value;
    }
  }
  return default_value;
}


// Change the hostname.
//
// The new hostname is saved to nvs and will take effect after the next reboot
//
void
set_hostname(const char *hostname)
{
  if (hostname==NULL || hostname[0]=='\0') {
    return ; 
  }
    
  // copy to app_hostname 
  strncpy(app_hostname, hostname, sizeof(app_hostname)-1 );
  app_hostname[sizeof(app_hostname)-1] = '\0';

  // and also save in nvs.
  if (app_nvs_ok) {
    nvs_set_str(app_nvs,"hostname",app_hostname);
    nvs_commit(app_nvs);
  }
}

void
set_full_power(int value)
{
  if (value<=0)
    value = 1;
  
  app_full_power = value;
  
  if (app_nvs_ok) {
    nvs_set_i32(app_nvs,"full_power",app_full_power);
    nvs_commit(app_nvs);
  }
}

void
set_default_frame_size(int value)
{
  if (value<=0)
    value = 1;
  
  app_frame_size = value;
  
  if (app_nvs_ok) {
    nvs_set_i32(app_nvs,"frame_size",app_frame_size);
    nvs_commit(app_nvs);
  }
}

static double compute_auto_min_power() {
  return app_auto_min_power ; // TODO:
}

static void dump_state() {

  const char *mode_name = "???" ;

  if (ac_mode==AC_MODE_AUTO)   mode_name="auto" ;
  if (ac_mode==AC_MODE_MANUAL) mode_name="manual" ;

#if 0
  time_t now;
  time(&now);
  ESP_LOGW(TAG, "################ TIME %s ", ctime(&now));

  char strftime_buf[64];
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
  ESP_LOGW(TAG, "################ TIME %s ", strftime_buf);
#endif
  
  ESP_LOGW(TAG, "################ mode = %s (%d)", mode_name, ac_mode);
  ESP_LOGW(TAG, "################ frame_size       = %" PRId32 " W", app_frame_size);
  ESP_LOGW(TAG, "################ full_power       = %" PRId32 " W", app_full_power);
  ESP_LOGW(TAG, "################ manual_power     = %.1f W", app_manual_power);
  ESP_LOGW(TAG, "################ auto_min_power   = %.1f W", app_auto_min_power);
  ESP_LOGW(TAG, "################ auto_over_power  = %.1f W", app_auto_over_power);
  ESP_LOGW(TAG, "################ auto_available_power  = %.1f W", app_auto_available_power);

  double x;
  x = acr_get_target_ratio() ;
  ESP_LOGW(TAG, "################ target         = %.2f W (%.2f) ", app_full_power*x , x ) ;
  x = acr_get_achievable_ratio() ;
  ESP_LOGW(TAG, "################ achievable     = %.2f W (%.2f) ", app_full_power*x , x ) ;
  x = acr_get_last_achieved_ratio() ;
  ESP_LOGW(TAG, "################ last achieved  = %.2f W (%.2f) ", app_full_power*x , x ) ;  
}


static void update_ac_relay() {
  double target_power=0 ; 

  if (ac_mode==AC_MODE_MANUAL)
  {
    target_power = app_manual_power ;
  }
  else if (ac_mode == AC_MODE_AUTO)
  {
    target_power = app_auto_available_power;  

    // We want to overshoot by that much 
    target_power += app_auto_over_power;
      
    // and we require at least app_min_power
    double min_power = compute_auto_min_power();
    if ( target_power < min_power )
      target_power = min_power;
  }
  acr_set_target_ratio( target_power / app_full_power );

}

static void process_energy_meter_msg(cJSON *root) {

  dump_state() ;
    
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

  double available_power = power_b-power_a ;
  app_post_set_available_power(available_power); 
  
  ESP_LOGW(TAG, "==> power_a=%.1f power_b=%.1f", power_a, power_b);
  ESP_LOGW(TAG, "==> available_power = %.1f", available_power);

  update_ac_relay();
  
}

static void process_cumulus_msg(cJSON *root) {
    
    double full_power   = cJSON_GetNumberValue( cJSON_GetObjectItemCaseSensitive(root,"full_power") ) ;
    double frame_size   = cJSON_GetNumberValue( cJSON_GetObjectItemCaseSensitive(root,"frame_size") ) ;
    const char * mode   = cJSON_GetStringValue( cJSON_GetObjectItemCaseSensitive(root,"mode") ) ;
    double manual_power = cJSON_GetNumberValue( cJSON_GetObjectItemCaseSensitive(root,"manual_power") ) ;
    double auto_min_power  = cJSON_GetNumberValue( cJSON_GetObjectItemCaseSensitive(root,"auto_min_power") ) ;
    double auto_over_power = cJSON_GetNumberValue( cJSON_GetObjectItemCaseSensitive(root,"auto_over_power") ) ;

    if (mode) {
      if (!strcmp(mode,"auto"))
        ac_mode = AC_MODE_AUTO;
      else if (!strcmp(mode,"manual"))
        ac_mode = AC_MODE_MANUAL;
    }
    
    if ( !isnan(frame_size) ) {
      frame_size = acr_set_frame_size(frame_size) ;
    }

    if ( !isnan(full_power) ) {      
      app_full_power = full_power;
      if (app_full_power < 1) {
        app_full_power = 1 ;
      }
    }

    if ( !isnan(manual_power) ) {
      app_manual_power = manual_power;
    }
    
    if ( !isnan(auto_min_power) ) {
      app_auto_min_power = auto_min_power;
    }

    if ( !isnan(auto_over_power) ) {
      app_auto_over_power = auto_over_power;
    }

    update_ac_relay() ;
    dump_state() ;
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
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        //msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 0);
        //ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        {
          const char * topic;

          topic = "zigbee2mqtt/energy_meter" ;
          msg_id = esp_mqtt_client_subscribe(client, topic, 1);
          ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d  topic='%s'", msg_id, topic);

          topic = "cumulus" ;          
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
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);

#define IF_TOPIC(STRING) if ( (event->topic_len==sizeof(STRING)-1) && memcmp(event->topic,STRING,event->topic_len)==0 )
        //if ( buffer_contains_text(event->topic,event->topic_len, "zigbee2mqtt/energy_meter") ) {
        IF_TOPIC("zigbee2mqtt/energy_meter") {
          cJSON *root = cJSON_Parse(event->data);
          process_energy_meter_msg(root) ;
          cJSON_Delete(root);
        } else IF_TOPIC("cumulus") {
          cJSON *root = cJSON_Parse(event->data);
          process_cumulus_msg(root) ;
          cJSON_Delete(root);
        }else {
          ESP_LOGW(TAG, "Unknown topic");
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


static void ui_mqtt_start(void)
{
  
  // Warning: LWIP DNS is only using mDNS to resolve hostnames that end with '.local'
  //          so that won't work if your '.local' hostnames are manually configured.
  //           ==> So do not use .local for your local domain.
  //           ==> alternatively, disable LWIP_DNS_SUPPORT_MDNS_QUERIES in config
  //               to fall back to IPV4 name resolution.
  
  esp_mqtt_client_config_t mqtt_cfg = {
    .broker = {
      .address = {
        .uri = "mqtt://cumulus:esp32c6@mqtt.private:1883", // TODO: add config
      }
    }
  };
  ESP_LOGI(TAG, "MQTT URI: %s ", mqtt_cfg.broker.address.uri);
  
  esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
  /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
  esp_mqtt_client_register_event(client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
  esp_mqtt_client_start(client);
}


static void start_wifi_connection()
{
  wifi_config_t wifi_config;
  ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_config) );
  if ( wifi_config.sta.ssid[0] == '\0' ) {
    ESP_LOGI(TAG, "Start WPS");
    esp_wps_config_t wps_config = WPS_CONFIG_INIT_DEFAULT(WPS_TYPE_PBC);
    wps_index=0;
    wps_count=0;
    app_wifi_state = APP_WIFI_WPS;
    app_wifi_retry = CONFIG_APP_WIFI_MAXIMUM_RETRY;
    ESP_ERROR_CHECK(esp_wifi_wps_enable(&wps_config));
    ESP_ERROR_CHECK(esp_wifi_wps_start(0));
    led_set_rgb(RGB_BLUE);
  } else {    
    ESP_LOGI(TAG, "Connecting to SSID:%.*s", (int) sizeof wifi_config.sta.ssid, wifi_config.sta.ssid) ;
    app_wifi_state = APP_WIFI_CONNECTING;
    app_wifi_retry = CONFIG_APP_WIFI_MAXIMUM_RETRY;
    esp_wifi_connect();
    led_set_rgb(RGB_ORANGE);
 }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
  // assert(event_base==WIFI_EVENT);
  switch(event_id) {

    case WIFI_EVENT_STA_START:
      ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
      break;

    case WIFI_EVENT_STA_STOP:
      ESP_LOGI(TAG, "WIFI_EVENT_STA_STOP");
      break;

    case WIFI_EVENT_STA_CONNECTED:
      ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED");
      break;

    case WIFI_EVENT_STA_DISCONNECTED:
      ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");
      switch(app_wifi_state) {
        case APP_WIFI_OK:
          on_wifi_disconnected();
          break;        
        case APP_WIFI_CONNECTING:
        case APP_WIFI_WPS:
          if (app_wifi_retry>0) {
            app_wifi_retry--;
            ESP_LOGI(TAG, "Retry connecting");
            esp_wifi_connect();
          } else if (app_wifi_state==APP_WIFI_WPS && wps_index < wps_count) {
            //  Try the next WPS credential if any. 
            ESP_LOGI(TAG, "WPS Connecting to SSID: %s, Passphrase: %s",
                     wps_cred[wps_index].sta.ssid, wps_cred[wps_index].sta.password);
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wps_cred[wps_index++]) );
            esp_wifi_connect();           
          } else {
            on_wifi_fail_connection();
          }
          break;
        default:
          ESP_LOGI(TAG, "Unexpected WIFI_EVENT_STA_DISCONNECTED");
          break;
      }
      
      break;

    case WIFI_EVENT_STA_WPS_ER_SUCCESS:
      ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_SUCCESS");
      {
        //
        // If only one AP credential is received from WPS, there will be no event data and
        // esp_wifi_set_config() is already called by WPS modules for backward compatibility
        // with legacy apps. So directly attempt connection here.
        //
        wifi_event_sta_wps_er_success_t *evt = (wifi_event_sta_wps_er_success_t *)event_data;
        if (evt) {
          wps_count = evt->ap_cred_cnt;
          for (int i = 0; i < wps_count; i++) {
            memcpy(wps_cred[i].sta.ssid,
                   evt->ap_cred[i].ssid,
                   sizeof(evt->ap_cred[i].ssid));
            memcpy(wps_cred[i].sta.password,
                   evt->ap_cred[i].passphrase,
                   sizeof(evt->ap_cred[i].passphrase));
          }
          ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wps_cred[0]) );
        } else {
          // No event means that only one AP credential was received from WPS and
          // that esp_wifi_set_config() was already called by WPS module for backward
          // compatibility.
          // So retrieve that config in wps_cred[0] to print the credentials
          wps_count = 1;
          ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wps_cred[0]) );
        }
        
        wps_index = 0;
        ESP_LOGI(TAG, "WPS: Connecting to SSID: %.*s, Passphrase: %.*s",
                   (int) sizeof wps_cred[wps_index].sta.ssid,  wps_cred[wps_index].sta.ssid,
                   (int) sizeof wps_cred[wps_index].sta.password, wps_cred[wps_index].sta.password);
          
        ESP_ERROR_CHECK(esp_wifi_wps_disable());

        esp_wifi_connect();
      }
      break;

    case WIFI_EVENT_STA_WPS_ER_FAILED:
      ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_FAILED");
      ESP_ERROR_CHECK(esp_wifi_wps_disable());
      on_wifi_fail_connection();
      break;

    case WIFI_EVENT_STA_WPS_ER_TIMEOUT:
      ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_TIMEOUT");
      ESP_ERROR_CHECK(esp_wifi_wps_disable());
      on_wifi_fail_connection();
      break;
      
    default:
      ESP_LOGI(TAG, "Other WIFI event: id=%d", ((int)event_id) );
      break;
  }
  
}


static void on_wifi_connected()
{
  app_wifi_state = APP_WIFI_OK;

  wifi_config_t wifi_config;
  ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_config) );
  ESP_LOGI(TAG, "Connected to WiFi SSID:%.*s password:%.*s",
           (int) sizeof wifi_config.sta.ssid, wifi_config.sta.ssid,
           (int) sizeof wifi_config.sta.password, wifi_config.sta.password);
  
  led_set_rgb( RGB_GREEN );
  // mqtt_app_start(); 
}


// Executed when the WiFi connection failed
static void on_wifi_disconnected()
{
  app_wifi_state = APP_WIFI_FAIL;
  app_wifi_reconnect_delay = 1; 
  ESP_LOGI(TAG, "Disconnected from WiFi. Retry delay set to %d minutes", app_wifi_reconnect_delay);
  led_set_rgb(RGB_PURPLE);
}

// Executed when the WiFi connection failed
static void on_wifi_fail_connection()
{
  app_wifi_state = APP_WIFI_FAIL;
  app_wifi_reconnect_delay = 3;  
  ESP_LOGI(TAG, "Failed to connect to WiFi. Retry delay set to %d minutes", app_wifi_reconnect_delay);
  led_set_rgb(RGB_PURPLE);
}

static void ip_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data)
{
  // assert(event_base==IP_EVENT);
  switch (event_id) {
    case IP_EVENT_STA_GOT_IP:
    {
      ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
      ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
      on_wifi_connected();
    }
    break ;
    default:
      ESP_LOGI(TAG, "Other IP event: id=%d",  ((int)event_id) );
  }
}

/////////////////////

// Set wifi credentials
//   ssid maximum length is 31  
//   password maximum length is 63  
static void set_wifi_credentials(const char *ssid, const char *password)
{
  wifi_config_t wifi_config = {} ;
  strncpy((char*)wifi_config.sta.ssid,     ssid,     sizeof(wifi_config.sta.ssid)-1) ;
  strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password)-1) ;
  // wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK; 
  // wifi_config.sta.sae_pwe_h2e = ESP_WIFI_SAE_MODE;
  // wifi_config.sta.sae_h2e_identifier = PROJECT_H2E_IDENTIFIER;
  
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
}


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

static void app_event_set_available_power(app_event_set_available_power_t *arg)
{
  ESP_LOGI(TAG, "in app_event_set_available_power(%.1f)", (float) arg->value );
  app_auto_available_power = arg->value;
  update_ac_relay();
}

// Called once every minute (approximately) 
static void app_event_minute_tic()
{
  
  ESP_LOGI(TAG, "in app_event_minute_tic");

  // Automatic WiFi reconnect
  if ( app_wifi_state == APP_WIFI_FAIL ) {
    ESP_LOGI(TAG, "Wifi state is FAIL. Reconnect delay is %d",app_wifi_reconnect_delay);
    if (app_wifi_reconnect_delay>0) {
      app_wifi_reconnect_delay--;
    } else if (app_wifi_reconnect_delay==0) {
      start_wifi_connection();
    }
  }
}

// Called every time the hour changes (so roughly at 00:00, 01:00, ... , 23:00 )
// and also once at startup and when the timezone is changed (e.g. at Summer Time).
static void app_event_hour_tic()
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
      app_event_set_mode_auto((app_event_set_mode_auto_t*) data) ;
      break;
    case APP_EVENT_SET_MODE_MANUAL:
      app_event_set_mode_manual((app_event_set_mode_manual_t*) data) ;
      break;
    case APP_EVENT_SET_AVAILABLE_POWER:
      app_event_set_available_power((app_event_set_available_power_t*)data);
      break;
    case APP_EVENT_MINUTE_TIC:
      app_event_minute_tic() ;
      break;
    case APP_EVENT_HOUR_TIC:
      app_event_hour_tic() ;
      break;
    default:
      ESP_LOGE(TAG, "Unknown APP EVENT %d",(int) event_id);
      break;
  }
}


#ifdef __cplusplus
extern "C" 
#endif
void app_main(void)
{
  
  ESP_LOGI(TAG, "[APP] Startup..");
  ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
  ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());
  esp_log_level_set("*", ESP_LOG_INFO);
  //esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
  //esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
  //esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
  //esp_log_level_set("transport", ESP_LOG_VERBOSE);
  //esp_log_level_set("outbox", ESP_LOG_VERBOSE);

  
  ///////////// Make sure that the RELAY pin is OFF during startup
  
  gpio_reset_pin((gpio_num_t)CONFIG_RELAY_GPIO);
  gpio_set_direction((gpio_num_t)CONFIG_RELAY_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)CONFIG_RELAY_GPIO, 0);

  /////////////

  resource_init();
  
  /////////////
    
  led_init();
  led_set_rgb(RGB_GREEN);

  //////////// Start monitoring the buttons 
  
  if ( ! button_driver_init() ) {
    ESP_LOGE(TAG, "Failed to initialize button driver");
  }
  
  //Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  if (false) {
    // Pause for 5s with a blue led. 
    // Enable WPS if the button is pressed.  
    bool wps = false ;
    led_set_rgb(RGB_BLUE);
    for (int k=0; k<10 ; k++) {
      vTaskDelay( 500 / portTICK_PERIOD_MS);
      if (button_a_count>0 || button_b_count>0) {
        wps = true ;
        led_set_rgb(RGB_WHITE);
      }
    }
    led_set_rgb(wps ? RGB_RED : RGB_GREEN );
  }

#if 0
  nvs_stats_t nvs_stats;
  nvs_get_stats(NULL, &nvs_stats);
  printf("NVS STATS:\n");
  printf("  used_entries=%zu\n",nvs_stats.used_entries) ;
  printf("  free_entries=%zu\n",nvs_stats.free_entries) ;
  printf("  available_entries=%zu\n",nvs_stats.available_entries) ;
  printf("  total_entries=%zu\n",nvs_stats.total_entries) ;
  printf("  namespace_count=%zu\n",nvs_stats.namespace_count) ;
#endif
 
  if (nvs_open("nvs", NVS_READWRITE, &app_nvs)==ESP_OK ) {
    app_nvs_ok = true;
  }

  // Get the BASE MAC address of the device.
  uint8_t mac[6+2] = {0,0,0,0,0,0};
  esp_read_mac(mac, ESP_MAC_BASE); 
  printf("base mac = %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);

  // Transform the base mac into a almost unique 16bit id.
  uint16_t almost_unique_id =
    (mac[0]*1) ^
    (mac[1]*13) ^
    (mac[2]*23) ^
    (mac[3]*41) ^
    (mac[4]*55) ^
    (mac[5]*120) ;

  // Create a default (almost) unique hostname
  char default_hostname[32];
  snprintf(default_hostname,32,"%s%u","cumulus",(unsigned)almost_unique_id) ;
  default_hostname[31] = '\0'; 
    
  app_nvs_get_str("hostname", app_hostname, sizeof(app_hostname), default_hostname);
  printf("hostname = %s\n", app_hostname);

  app_full_power = app_nvs_get_i32("full_power", CONFIG_FULL_POWER);
  if ( app_full_power==0 )
    app_full_power = CONFIG_FULL_POWER ;
  printf("full_power = %" PRId32 "\n", app_full_power);

  app_frame_size = app_nvs_get_i32("frame_size", CONFIG_ACR_FRAME_SIZE);
  printf("frame_size = %" PRId32 "\n", app_frame_size);
  //////////// start the AC relay service

  acr_set_target_ratio(0);  
  acr_set_frame_size(app_frame_size);  
  acr_start(AC_FREQ, CONFIG_RELAY_GPIO); 

  // Create the main event loop and register events. 
  
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  esp_event_handler_instance_t instance_app_id;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(APP_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      &app_event_handler,
                                                      NULL,
                                                      &instance_app_id));

  esp_event_handler_instance_t instance_any_id;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      &wifi_event_handler,
                                                      NULL,
                                                      &instance_any_id));

  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                      IP_EVENT_STA_GOT_IP,
                                                      &ip_event_handler,
                                                      NULL,
                                                      &instance_got_ip));


  
  ESP_ERROR_CHECK(esp_netif_init());

  
  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  esp_netif_sntp_init(&config);
    
  // 
  // See man tzset for the timezone format.
  //
 #define TZ_PARIS  "CET-1CEST,M3.2.0/2:00:00,M11.1.0/2:00:00" 
  setenv("TZ", TZ_PARIS, 1);
  tzset();

  //
  // We are not yet connected to WiFi but we can start the various UI. 
  //
  telnet_start_server(&app_telnet_server) ;  
  start_http_ui() ;
  ui_mqtt_start();
  
  //
  // Setup the WiFi network interface
  //
  
  esp_netif_t *netif = esp_netif_create_default_wifi_sta();

  esp_netif_set_hostname(netif, app_hostname); // TODO: make it configurable.
  
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
  ESP_ERROR_CHECK(esp_wifi_start());

  // Set the default SSID and PASSWORD if nothing is set. 
  wifi_config_t wifi_config = { };
  ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_config) );
  if (wifi_config.sta.ssid[0] == 0) {
    set_wifi_credentials(CONFIG_APP_WIFI_SSID, CONFIG_APP_WIFI_PASSWORD) ; 
  }
  
  // Note: The first wifi connection will be started during the first minute tic.
  app_wifi_state = APP_WIFI_FAIL;
  app_wifi_reconnect_delay = 0; 


  // TODO: We could use a dedicated event loop for app events instead of the default event loop.
  //       That could reduce latency but that would also require additionnal events to synchronize
  //       both loops.
  
  //
  // Trigger events at regular intervals in the default event loop.
  //
  // Note: The clock will start at EPOCH (Jan 1st 1970, 00:00 UTC+00:00)
  //       until the NTP service sets a proper time.
  int last_hour=-1;          
  int last_minute=-1;
  struct tm local_time;
  while(true) {
    time_t t = time(NULL);
    localtime_r(&t, &local_time);

    ESP_LOGI(TAG, "localtime %02d:%02d", local_time.tm_hour, local_time.tm_min);

    // Trigger app_event_minute_tic at each minute change
    if (local_time.tm_min != last_minute ) {
      last_minute = local_time.tm_min ;
      app_post_event(APP_EVENT_MINUTE_TIC, NULL, 0);
    }
    
    // Trigger app_event_hour_tic at each hour change
    if (last_hour != local_time.tm_hour) {
      last_hour = local_time.tm_hour;
      app_post_event(APP_EVENT_HOUR_TIC, NULL, 0);
    }

    vTaskDelay( 10 * 1000 / portTICK_PERIOD_MS);
  }

}
