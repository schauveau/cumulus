#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
//#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
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

//#include "mqtt_client.h"
//#include "cJSON.h"

#include "lwip/err.h"
#include "lwip/sys.h"

// local includes 
#include "app.h"
#include "resource.h"
#include "project.h"
#include "button_driver.h"
#include "acr.h"
#include "ui_http.h"
#include "ui_mqtt.h"
#include "rgb_led.h"

//#include "ui_telnet.h"

#include "app_events.h"
static const char TAG[] = "app" ;

ESP_EVENT_DEFINE_BASE(APP_EVENT);

#define TZ_FRANCE   "CET-1CEST,M3.2.0/2:00:00,M11.1.0/2:00:00"
#define TZ_DEFAULT  TZ_FRANCE

// Compute an 'almost unique' 16 bit identifier based from the BASE MAC address of the device.
static uint16_t get_device_id()
{
  uint8_t mac[6+2] = {0,0,0,0,0,0};
  esp_read_mac(mac, ESP_MAC_BASE); 
  printf("base mac = %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  uint16_t id = (mac[0]*1)^(mac[1]*13)^(mac[2]*23)^(mac[3]*41)^(mac[4]*55)^(mac[5]*120) ;
  return id;
}


//
// Copy a C-string to a uint8_t[] buffer
//
template <int N>
inline void
assign_buffer(unsigned char (&out)[N], const char *in)
{
  strlcpy((char*)out, in, sizeof(out) );
}


// 1000000 ticks = 1s , 1000 ticks = 1ms , 1 tick = 1us so timer frequency is 1MHz,  
#define TIMER_RESOLUTION (1000*1000) 
// AC frequency is typically 50 or 60 hertz 
#define AC_FREQ 50


static bool app_nvs_ok = false;  // True when app_nvs is operational
static nvs_handle_t app_nvs;

static esp_netif_t *wifi_sta_netif;   // Wifi network interface (STA)
static esp_netif_t *wifi_ap_netif;    // Wifi network interface (AP)

static app_state_t state ;
  
// The current state of the WiFi 
typedef enum {
  APP_WIFI_OK,             // Connected to an access point
  APP_WIFI_ACCESS_POINT,   // Access Point 
  APP_WIFI_FAIL,           // Not connected
  APP_WIFI_CONNECTING,     // Currently trying to connect using SSID & PASSWORD
} app_wifi_state_t ;


static app_wifi_state_t app_wifi_state = APP_WIFI_FAIL ;

// Number of minutes to wait before attempting to reconnect.
// Must be re-set when setting app_wifi_state=APP_WIFI_FAIL.
// Set to -1 to disable auto-reconnect
static int app_wifi_reconnect_delay=0;

// Number of connections remaining connection attempts.
static int app_wifi_retry = 0;


template <int N>
inline void
app_nvs_get_s(const char *key, char_buffer<N>& buffer, const char *default_value)
{  
  size_t size=N;
  if (nvs_get_str(app_nvs, key, buffer.c_str(), &size)!=ESP_OK) {
    strlcpy(buffer.c_str(), default_value, N) ;
  }  
}

static int32_t app_nvs_get_i( const char *key, int32_t default_value)
{  
  int32_t value;
  if (nvs_get_i32(app_nvs, key, &value)==ESP_OK) {
    return value;
  } else {
    return default_value;
  }
}
 
void dump_state(stf::mask_t mask) {
  
  if (mask & stf::frame_size) {
    printf("state.frame_size = %d\n",state.frame_size);
  }

  if (mask & stf::full_power) {
    printf("state.full_power = %d\n",state.full_power);
  }

  if (mask & stf::timezone) {
    printf("state.timezone = \"%s\"\n",state.timezone.c_str());
  }

  if (mask & stf::hostname) {
    printf("state.hostname = \"%s\"\n",state.hostname.c_str());
  }
  
  if (mask & stf::mqtt_uri) {
    printf("state.mqtt_uri = \"%s\"\n",state.mqtt.uri.c_str());
  }
      
  if (mask & stf::wifi_ssid) {
    printf("state.wifi.ssid = \"%s\"\n",state.wifi.ssid.c_str());
  }

  if (mask & stf::wifi_password) {
    printf("state.wifi.password = \"%s\"\n",state.wifi.password.c_str());
  }

  if (mask & stf::ui_password) {
    printf("state.ui.password = \"%s\"\n",state.ui.password.c_str());
  }
}

void load_state(stf::mask_t mask)
{
    
  if (mask & stf::frame_size) {
    state.frame_size = std::max( int32_t(10), app_nvs_get_i("frame_size", CONFIG_ACR_FRAME_SIZE) );
  }

  if (mask & stf::full_power) {
    state.full_power = std::max( int32_t(1),  app_nvs_get_i("full_power", CONFIG_FULL_POWER) ) ;
  }

  if (mask & stf::timezone) {
    app_nvs_get_s("timezone", state.timezone, TZ_DEFAULT);
  }

  if (mask & stf::hostname) {
    uint16_t almost_unique_id = get_device_id();
    constexpr int SZ = 32;
    char default_hostname[SZ];
    snprintf(default_hostname,SZ,"%s%u","cumulus",((unsigned)almost_unique_id)%1000) ;
    app_nvs_get_s("hostname", state.hostname, default_hostname);
  }
  
  if (mask & stf::mqtt_uri) {
    app_nvs_get_s("mqtt_uri", state.mqtt.uri, CONFIG_MQTT_BROKER_URI);
  }
      
  if (mask & stf::wifi_ssid) {
    app_nvs_get_s("wifi_ssid", state.wifi.ssid, "");
  }

  if (mask & stf::wifi_password) {
    app_nvs_get_s("wifi_password", state.wifi.password, "");
  }

  if (mask & stf::ui_password) {
    app_nvs_get_s("ui_password", state.ui.password, "foobar");
  }
      
}

void save_state(stf::mask_t mask)
{
  if (!app_nvs_ok) {
    return;
  }
  
  if (mask & stf::frame_size) {
    nvs_set_i32(app_nvs,"frame_size",state.frame_size);
  }

  if (mask & stf::full_power) {
    nvs_set_i32(app_nvs,"full_power",state.full_power);
  }

  if (mask & stf::timezone) {
    nvs_set_str(app_nvs,"timezone",state.timezone.c_str());
  }

  if (mask & stf::hostname) {
    nvs_set_str(app_nvs,"hostname",state.hostname.c_str());
  }
  
  if (mask & stf::mqtt_uri) {
    nvs_set_str(app_nvs,"mqtt_uri",state.mqtt.uri.c_str());
  }
      
  if (mask & stf::wifi_ssid) {
    nvs_set_str(app_nvs,"wifi_ssid",state.wifi.ssid.c_str());
  }

  if (mask & stf::wifi_password) {
    nvs_set_str(app_nvs,"wifi_password",state.wifi.password.c_str());
  }

  if (mask & stf::ui_password) {
    nvs_set_str(app_nvs,"ui_password",state.ui.password.c_str());
  }

  nvs_commit(app_nvs);
      
}    



// Change the hostname.
//
// The new hostname is saved to nvs and will take effect after the next reboot
//
static void
set_hostname(const char *hostname)
{
  if (hostname==NULL || hostname[0]=='\0') {
    return ; 
  }
  if (state.hostname==hostname) {
    // No changes. Ignore.
    return;
  }
  
  state.hostname = hostname;  
  save_state(stf::hostname);

}


// Set the timezone (POSIX format)
static void
set_timezone(const char *timezone)
{
  if (timezone==NULL || timezone[0]=='\0') {
    return ; 
  }
  if ( state.timezone==timezone ) {
    // No changes. Ignore.
    return;
  }
  state.timezone = timezone;
  save_state(stf::timezone);
  setenv("TZ", state.timezone.c_str(), 1);
  tzset();
}

// Set the timezone (POSIX format)
static void
set_mqtt_uri(const char *uri)
{
  if (uri==NULL) {
    return ; 
  }
  if (state.mqtt.uri == uri) {
    return;
  }
  state.mqtt.uri = uri;
  save_state(stf::mqtt_uri);
}


// Set the timezone (POSIX format)
static void
set_ui_password(const char *password)
{
  if (password==NULL) {
    return ; 
  }
  if (state.ui.password==password){
    return;
  } 
  state.ui.password=password;
  save_state(stf::ui_password);

}


void
set_full_power(int value, bool save)
{
  if (value<=0)
    value = 1;
  
  state.full_power = value;

  if (save) {
    save_state(stf::full_power);
  }
}

static void update_ac_relay() {
  int power=0;
  if (state.mode==AC_MODE_MANUAL)
  {
    power = state.m.power ;
    double ratio = acr_set_target_ratio( double(power) / state.full_power );
    ESP_LOGI(TAG, "manual ratio:%4.1f%% target:%d/%d",
             ratio*100,
             power,
             state.full_power);
  }
  else if (state.mode == AC_MODE_AUTO)
  {

    power = state.a.available_power+state.a.over_power ;
    power = std::max(power, state.a.min_power);
    double ratio = acr_set_target_ratio( double(power) / state.full_power );
    ESP_LOGI(TAG, "auto ratio %4.1f%% target %d/%d avail %d over %d min %d",
             ratio*100,
             power,
             state.full_power,
             state.a.available_power,
             state.a.over_power,
             state.a.min_power);    
  }
}


static void start_wifi_connection()
{
  wifi_config_t wifi_config;
  ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_config) );
  ESP_LOGI(TAG, "Connecting to SSID:%.*s", (int) sizeof wifi_config.sta.ssid, wifi_config.sta.ssid) ;
  app_wifi_state = APP_WIFI_CONNECTING;
  app_wifi_retry = CONFIG_APP_WIFI_MAXIMUM_RETRY;
  esp_wifi_connect();
  led_set_rgb(RGB_ORANGE);
}

static void network_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
  if (event_base==WIFI_EVENT)
  {
    switch(event_id) {
      case WIFI_EVENT_AP_STACONNECTED:
      {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
      }
      break;

      case WIFI_EVENT_AP_STADISCONNECTED:
      {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d, reason=%d",
                 MAC2STR(event->mac), event->aid, event->reason);
      }
      break;
      
      case WIFI_EVENT_STA_START:
      case WIFI_EVENT_STA_STOP:
      case WIFI_EVENT_STA_CONNECTED:
        // Note: The actual 'CONNECTION' event is handled below by IP_EVENT_STA_GOT_IP.
        break;

      case WIFI_EVENT_STA_DISCONNECTED:
        ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");
        switch(app_wifi_state) {
          case APP_WIFI_OK:
            app_wifi_state = APP_WIFI_FAIL;
            app_wifi_reconnect_delay = 1; 
            ESP_LOGI(TAG, "Disconnected from WiFi. Retry delay set to %d minutes", app_wifi_reconnect_delay);
            led_set_rgb(RGB_PURPLE);
            break;        
          case APP_WIFI_CONNECTING:
            if (app_wifi_retry>0) {
              app_wifi_retry--;
              ESP_LOGI(TAG, "Retry connecting");
              esp_wifi_connect();
            } else {
              app_wifi_state = APP_WIFI_FAIL;
              app_wifi_reconnect_delay = 3;  
              ESP_LOGI(TAG, "Failed to connect to WiFi. Retry delay set to %d minutes", app_wifi_reconnect_delay);
              led_set_rgb(RGB_PURPLE);
            }
            break;
          default:
            ESP_LOGI(TAG, "Unexpected WIFI_EVENT_STA_DISCONNECTED");
            break;
        }
        break;

      default:
        ESP_LOGI(TAG, "Other WIFI event: id=%d", ((int)event_id) );
        break;
    }
  }
  else if(event_base==IP_EVENT)
  {
    switch (event_id) {
      case IP_EVENT_STA_GOT_IP:
      {     
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_config_t wifi_config;
        ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_config) );
        ESP_LOGI(TAG, "Connected to WiFi SSID:%s Password:%s",
                 state.wifi.ssid.c_str(),
                 state.wifi.password.c_str());
        led_set_rgb( RGB_GREEN );
      }
      break ;
      default:
        ESP_LOGI(TAG, "Other IP event: id=%d",  ((int)event_id) );
    }
  }
  
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

//
// Called every time the hour changes (so roughly at 00:00, 01:00, ... , 23:00 )
// but also at startup and when the timezone is changed (e.g. at Summer/Winter Time).
//
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
    {
      app_event_sync_t *arg = (app_event_sync_t*) data ;
      xSemaphoreGive(arg->sem);
    }
    break;
    
    case APP_EVENT_MODE:
    {
      ac_mode_t new_mode = * (ac_mode_t*) data ;
      if (new_mode!=state.mode) {
        switch(new_mode) {
          case AC_MODE_AUTO:
          case AC_MODE_MANUAL:
            state.mode = new_mode;
            update_ac_relay();
            break;
        }
      }
    }
    break;
    
    case APP_EVENT_QUERY_STATE:
     {
       app_state_t *dest = * (app_state_t **) data ;
       if (dest) {
         *dest = state ;
       }
     }
     break;      

    case APP_EVENT_REBOOT:   
      esp_restart();
      break;

    case APP_EVENT_AUTO_AVAILABLE_POWER: 
      {
        state.a.available_power = *(int*)data ; 
        if (state.mode==AC_MODE_AUTO) {
          update_ac_relay();
        }
      }
      break;

    case APP_EVENT_AUTO_MIN_POWER:
      {
        state.a.min_power = *(int*)data ; 
        if (state.mode==AC_MODE_AUTO) {
          update_ac_relay();
        }
      }
      break;

    case APP_EVENT_AUTO_OVER_POWER:
      {
        state.a.over_power = *(int*)data ; 
        if (state.mode==AC_MODE_AUTO) {
          update_ac_relay();
        }
      }
      break;

    case APP_EVENT_MANUAL_POWER:
      {
        state.m.power = *(int*)data ; 
        if (state.mode==AC_MODE_AUTO) {
          update_ac_relay();
        }
      }
      break;

    
     case APP_EVENT_WIFI_CRED:
      {
        app_wifi_cred_t *cred = (app_wifi_cred_t *)data ;
        state.wifi.ssid     = cred->ssid;
        state.wifi.password = cred->password;
        save_state(stf::wifi_ssid | stf::wifi_password);
      }
      break;
      
    case APP_EVENT_FULL_POWER:
      {
        app_event_full_power_t *ev = (app_event_full_power_t *) data;
        state.full_power = *(int*)data ;
        if (state.full_power <= 0 ) {
          state.full_power = 1 ;
        }
        if (state.mode==AC_MODE_AUTO) {
          update_ac_relay();
        }
        if (ev->save) {
          save_state(stf::full_power);
        }
      }
      break;


    case APP_EVENT_FRAME_SIZE:
      {
        app_event_frame_size_t *ev = (app_event_frame_size_t *) data;
        int new_frame_size = acr_set_frame_size(ev->value) ;
        if ( new_frame_size != state.frame_size ) {
          state.frame_size = new_frame_size;
        }
        
        if (ev->save) {
          save_state(stf::frame_size); 
        }
      }
      break;
      
    case APP_EVENT_BUTTON_PRESS:
      {
        app_event_button_t * press = (app_event_button_t*)data;
        ESP_LOGI(TAG, "Button Press id=%c duration=%d",press->id, press->duration_ms);
      }
      break;
      
    case APP_EVENT_BUTTON_RELEASE:
      {
        app_event_button_t * press = (app_event_button_t*)data;
        ESP_LOGI(TAG, "Button Release id=%c duration=%d",press->id, press->duration_ms);

        if (press->id=='A' || press->id=='B') {
          if (press->duration_ms>2000) {
            // TODO: Clear wifi credential and other settings and then reboot
            // start_wifi_connection();
            
          }
        }
      }
      break;
      
    case APP_EVENT_HOSTNAME:
      set_hostname( (char*)data ) ;
      break;
      
    case APP_EVENT_MQTT_URI:
      set_mqtt_uri( (char*)data ) ;
      break;
      
    case APP_EVENT_TIMEZONE:
      set_timezone( (char*)data ) ;
      break;
      
    case APP_EVENT_UI_PASSWORD:
      set_ui_password( (char*)data ) ;
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

static void
heap_debug(void)
{
  printf("free heap size: %lu\n", esp_get_free_heap_size());
  printf("minimum free heap size: %lu\n", esp_get_minimum_free_heap_size());
}

static void
setup_nvs()
{
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
 
  if (nvs_open("nvs", NVS_READWRITE, &app_nvs)==ESP_OK ) {
    app_nvs_ok = true;
  }
}

static void
setup_wifi()
{
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  
  if ( !state.wifi.ssid.empty() )
  {
    wifi_config_t wifi_config = {} ;
    assign_buffer( wifi_config.sta.ssid, state.wifi.ssid.c_str() ) ;
    assign_buffer( wifi_config.sta.password, state.wifi.password.c_str() ) ;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    
    // Note: The wifi connection will be started during the first minute tic
    // so immediately in the event loop.
    app_wifi_state = APP_WIFI_FAIL; 
    app_wifi_reconnect_delay = 0; 
  }
  else
  {
    // No known SSID so create an Access Point instead 
    wifi_config_t wifi_config = {
      .ap = {
        .password = "",
        .ssid_len = 0, // will use strlen
        .channel = 1,
        .authmode = WIFI_AUTH_OPEN,
        .max_connection = 10,
        .pmf_cfg = {
          .required = true,
        },
      },
    };
    
    sprintf( (char*)wifi_config.ap.ssid, "cumulus%u",get_device_id() );

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    app_wifi_state = APP_WIFI_ACCESS_POINT ; 
  }
  
  ESP_ERROR_CHECK(esp_wifi_start());

  
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
  
  setup_nvs();

  load_state(stf::all_saved);
  dump_state(stf::all_saved);
  
  ESP_LOGI(TAG, "Hostname %s", state.hostname.c_str());

  acr_set_target_ratio(0);  
  acr_set_frame_size( state.frame_size );  
  acr_start(AC_FREQ, CONFIG_RELAY_GPIO); 

  // Setup the timezone
  // See man tzset for the POSIX timezone format
  setenv("TZ", state.timezone.c_str(), 1);
  tzset();
  
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
                                                      &network_event_handler,
                                                      NULL,
                                                      &instance_any_id));

  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                      IP_EVENT_STA_GOT_IP,
                                                      &network_event_handler,
                                                      NULL,
                                                      &instance_got_ip));

  
  ESP_ERROR_CHECK(esp_netif_init());

  
  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  esp_netif_sntp_init(&config);

  

  //
  // We are not yet connected to WiFi but we can already start the various UI. 
  //
  //ui_telnet_start();
  ui_http_start(state.ui.password) ;
  ui_mqtt_start(state.mqtt.uri.c_str(), state.hostname.c_str() );
  
  //
  // Setup the network interfaces for the WiFi
  //
  
  wifi_sta_netif = esp_netif_create_default_wifi_sta();
  wifi_ap_netif  = esp_netif_create_default_wifi_ap();
  esp_netif_set_hostname(wifi_sta_netif, state.hostname.c_str()); 
  esp_netif_set_hostname(wifi_ap_netif, state.hostname.c_str()); 

  setup_wifi();

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

    if(false) heap_debug();
  }

}
