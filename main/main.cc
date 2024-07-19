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


#define STATIC_ARRAY_SIZE(X)  (sizeof(X)/sizeof(X[0]))

static const char *TAG = "main";


static bool         app_nvs_ok = false ;
static nvs_handle_t app_nvs;

char app_hostname[32]; // The hostname (saved in nvs)
int32_t app_full_power;  // The estimated AC power when 100% ON (saved in nvs)
int32_t app_frame_size; // The default frame size (saved in nvs). 

double app_auto_available_power = 0; // in AUTO mode, this is how much power is currently available
double app_auto_over_power = 40 ;    // in AUTO mode, overshoot power by that amount.
double app_auto_min_power  = 0 ;     // in AUTO mode, do not produce less than this value.

double app_manual_power    = 0 ;     // in MANUAL mode, this is the target power



// The GPIO for the RGB led 
#define RGB_LED_GPIO  CONFIG_RGB_GPIO

// 1000000 ticks = 1s , 1000 ticks = 1ms , 1 tick = 1us so timer frequency is 1MHz,  
#define TIMER_RESOLUTION (1000*1000) 
// AC frequency is typically 50 or 60 hertz 
#define AC_FREQ 50

typedef enum {
  AC_MODE_AUTO,    // Use information provided by energy_meter (via MQTT) to adjust the production_target
  AC_MODE_MANUAL,  // Set to a fixed ratio  
} ac_mode_t;

static ac_mode_t ac_mode = AC_MODE_AUTO ; 

const button_info_t project_buttons[BUTTON_COUNT] = {
  {
    // The primary button 
    .id = 'A',
    .gpio = CONFIG_BUTTON_A_GPIO,
    .active_level = 0
  },
  {
    // The secondary button (can be the Boot button on the board)
    .id = 'B',
    .gpio = CONFIG_BUTTON_B_GPIO,
    .active_level = 0
  },
};


///////////////////////////////////////////////////////////////////

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}


static void dump_state() {

  const char *mode_name = "???" ;

  if (ac_mode==AC_MODE_AUTO)   mode_name="auto" ;
  if (ac_mode==AC_MODE_MANUAL) mode_name="manual" ;

  time_t now;
  time(&now);
  ESP_LOGW(TAG, "################ TIME %s ", ctime(&now));

  char strftime_buf[64];
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
  ESP_LOGW(TAG, "################ TIME %s ", strftime_buf);
  
  ESP_LOGW(TAG, "################ mode = %s (%d)", mode_name, ac_mode);
  ESP_LOGW(TAG, "################ frame_size       = %"PRId32" W", app_frame_size);
  ESP_LOGW(TAG, "################ full_power       = %"PRId32" W", app_full_power);
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

static double compute_auto_min_power() {
  return app_auto_min_power ; // TODO:
}

// Apply 
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

  app_auto_available_power = power_b-power_a ;
  
  ESP_LOGW(TAG, "==> power_a=%.1f power_b=%.1f", power_a, power_b);
  ESP_LOGW(TAG, "==> available_power = %.1f", app_auto_available_power);

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
    esp_mqtt_event_handle_t event = event_data;
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

static void mqtt_app_start(void)
{
  
  // Warning: LWIP DNS is only using mDNS to resolve hostnames that end with '.local'
  //          so that won't work if your '.local' hostnames are manually configured.
  //           ==> So do not use .local for your local domain.
  //           ==> alternatively, disable LWIP_DNS_SUPPORT_MDNS_QUERIES in config
  //               to fall back to IPV4 name resolution.
  
  esp_mqtt_client_config_t mqtt_cfg = {
    .broker.address.uri = "mqtt://cumulus:esp32c6@mqtt.private:1883", // TODO: add config  
    // .broker.address.uri = "mqtt://cumulus:esp32c6@zig.schauveau.local:1883", // TODO: add config  
    // .broker.address.uri = "mqtt://cumulus:esp32c6@192.168.1.23:1883", // TODO: add config  
  };
  ESP_LOGI(TAG, "MQTT URI: %s ", mqtt_cfg.broker.address.uri);
  
  esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
  /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
  esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
  esp_mqtt_client_start(client);
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

enum {
  CMD_SET_HOSTNAME,
  CMD_SET_FULL_POWER,
  CMD_SET_DEFAULT_FRAME_SIZE,
  CMD_TIMEZONE,
  CMD_HSV,
  CMD_RGB,
};


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
      break;
    case CMD_HSV:  
      int h = arg[0].i;
      int s = arg[1].i;
      int v = arg[2].i;      
      telnet_printf(server,"Set H=%d S=%d V=%d\n",h,s,v);
      led_set_hsv(h,s,v);
      break;
    case CMD_RGB:  
      int r = arg[0].i & 0xFF ;
      int g = arg[1].i & 0xFF ;
      int b = arg[2].i & 0xFF ;
      telnet_printf(server,"Set R=%d G=%d B=%d\n",r,g,b);
      led_set_rgb((r<<16)|(g<<8)|(b));
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

static volatile int button_a_count = 0 ;
static volatile int button_b_count = 0 ;

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

void project_button_handler(const button_info_t *button, int duration_ms)
{
  switch (button->id) {
    case 'A':
      button_a_count++ ; 
      ESP_LOGW(TAG, "Button A duration=%d", duration_ms);
      break;
    case 'B':
      button_b_count++ ; 
      ESP_LOGW(TAG, "Button B duration=%d", duration_ms);
      break;
    default:
      ESP_EARLY_LOGW(TAG, "Unknown button id=%d pin=%d duration=%d", button->id, button->gpio, duration_ms);
      break;
  }
}

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
  
  gpio_reset_pin(CONFIG_RELAY_GPIO);
  gpio_set_direction(CONFIG_RELAY_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_level(CONFIG_RELAY_GPIO, 0);

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

  ///  Dump all NVS entries
#if 1
  printf("NVS ENTRIES:\n");
  nvs_iterator_t it = NULL;
  esp_err_t res = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY, &it);
  while(res == ESP_OK) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info); // Can omit error check if parameters are guaranteed to be non-NULL
    printf("namespace '%s', key '%s', type '%d' \n", info.namespace_name, info.key, info.type);
    res = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
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
  
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  esp_netif_sntp_init(&config);

  // 
  // See man tzset for the timezone format.
  // 
  setenv("TZ", "Europe/Paris", 1);
  //setenv("TZ", "GMT+02:00:00", 1);
  tzset();

  ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
  
  telnet_start_server(&app_telnet_server) ;  
  start_http_ui() ;
  
  bool connected = wifi_start(wps, app_hostname) ;
  
  if (connected) {
    led_set_rgb(wps ? RGB_RED : RGB_GREEN );
    mqtt_app_start(); 
  } else {
    led_set_rgb(RGB_PURPLE); 
  }
  
  // Trigger events at regular intervals in the default event loop.
  //
  // The idea is that the default event loop task shall perform
  // much of the work to avoid creating local 
  //  

  
  // time_t last_minute_tic=0;  
  // time_t last_hour_tic=0;

  int    last_hour=0;
  int    last_minute=0;

  while(true) {
    time_t t = time(NULL);
    struct tm local_time = {0};
    vTaskDelay( 10 * 1000 / portTICK_PERIOD_MS);
    localtime_r(&t, &local_time);

    // Trigger app_async_minute_tic at each minute change
    if (local_time.tm_min != last_minute ) {
      last_minute = local_time.tm_min ;
      app_async_minute_tic() ; 
    }
    
    // Trigger app_async_minute_tic at each hour change
    if (last_hour != local_time.tm_hour) {
      last_hour = local_time.tm_hour;
      app_async_hour_tic() ; 
   }
      
  }

}
