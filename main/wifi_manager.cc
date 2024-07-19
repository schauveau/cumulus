#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_wps.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "wifi_manager.h"

static const char TAG[] = "wifi-manager" ;


#define wifi_ssid CONFIG_ESP_WIFI_SSID
#define wifi_password CONFIG_ESP_WIFI_PASSWORD

#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define PROJECT_H2E_IDENTIFIER ""
#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define PROJECT_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define PROJECT_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1


/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
static int s_wifi_retry_num = 0;

///////////////////// WPS


static esp_wps_config_t wps_config = WPS_CONFIG_INIT_DEFAULT(WPS_TYPE_PBC);                                               
static int wps_index = 0; // current index in wps_cred
static int wps_count = 0; // number of credentials stored in wps_cred (0..MAX_WPS_AP_CRED)
static wifi_config_t wps_cred[MAX_WPS_AP_CRED];

// start or restart WPS 
static void wps_start() {
  wps_index=0;
  wps_count=0;
  ESP_ERROR_CHECK(esp_wifi_wps_enable(&wps_config));
  ESP_ERROR_CHECK(esp_wifi_wps_start(0));
}

//
// If a SSID is configured then try to connect to it.  
// Otherwise, start WPS
//
void wifi_connect_or_start_wps(bool force_wps)
{

  wifi_config_t wifi_config;

  if (!force_wps) {
    if ( esp_wifi_get_config(WIFI_IF_STA, &wifi_config) == ESP_OK) {
      if ( wifi_config.sta.ssid[0] != 0 ) {      
        ESP_LOGI(TAG, "Connecting to SSID:%s", wifi_config.sta.ssid) ;
        esp_wifi_connect();
        return;
      }
    }
  }

  // Otherwise, attempt to get a connection with WPS
  ESP_LOGI(TAG, "WPS started") ;
  wps_start(); 
  
}

///////////////////////

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
  // assert(event_base==WIFI_EVENT);
  switch(event_id) {

    case WIFI_EVENT_STA_START:
      ESP_LOGI(TAG, "Wifi Station Started");
      // esp_wifi_connect();
      break;

    case WIFI_EVENT_STA_STOP:
      ESP_LOGI(TAG, "Wifi Station Stopped");
      break;

    case WIFI_EVENT_STA_CONNECTED:
      ESP_LOGI(TAG, "Connected to the AP");
      break;

    case WIFI_EVENT_STA_DISCONNECTED:
      ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");
      if (s_wifi_retry_num < 10) {
        esp_wifi_connect();
        s_wifi_retry_num++;
        ESP_LOGI(TAG, "retry to connect to the AP");
      } else if (wps_index < wps_count) {
        //  Try the next WPS credential if any. 
        ESP_LOGI(TAG, "Connecting to SSID: %s, Passphrase: %s",
                 wps_cred[wps_index].sta.ssid, wps_cred[wps_index].sta.password);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wps_cred[wps_index++]) );
        esp_wifi_connect();
        s_wifi_retry_num = 0;
      } else {
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
      }
      ESP_LOGI(TAG,"connect to the AP fail");
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
          wps_index = 0;
          ESP_LOGI(TAG, "Connecting to SSID: %s, Passphrase: %s",
                   wps_cred[wps_index].sta.ssid,
                   wps_cred[wps_index].sta.password);
          ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wps_cred[wps_index]) );
        } else {
          // No event means that only one AP credential was received from WPS and
          // that esp_wifi_set_config() was already called by WPS module for backward
          // compatibility.
          // So retrieve that config in wps_cred[0] to print the credentials
          wps_count = 1;
          wps_index = 0;
          ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wps_cred[wps_index]) );
          ESP_LOGI(TAG, " Connecting to SSID: %s, Passphrase: %s",
                   wps_cred[wps_index].sta.ssid, wps_cred[wps_index].sta.password);
        }
        
        ESP_ERROR_CHECK(esp_wifi_wps_disable());

        esp_wifi_connect();
      }
      break;

    case WIFI_EVENT_STA_WPS_ER_FAILED:
      ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_FAILED");
      ESP_ERROR_CHECK(esp_wifi_wps_disable());
      wps_start();
      break;

    case WIFI_EVENT_STA_WPS_ER_TIMEOUT:
      ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_TIMEOUT");
      ESP_ERROR_CHECK(esp_wifi_wps_disable());
      wps_start();
      break;

    case WIFI_EVENT_STA_WPS_ER_PIN:
      ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_PIN");
      /* display the PIN code */
      wifi_event_sta_wps_er_pin_t* event = (wifi_event_sta_wps_er_pin_t*) event_data;
      ESP_LOGI(TAG, "WPS_PIN = %c%c%c%c%c%c%c%c",
               event->pin_code[0],event->pin_code[1],event->pin_code[2],event->pin_code[3],
               event->pin_code[4],event->pin_code[5],event->pin_code[6],event->pin_code[7] );
      break;
      
    default:
      ESP_LOGI(TAG, "Other WIFI event: id=%d", ((int)event_id) );
      break;
  }
  
}



static void ip_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data)
{
  // assert(event_base==IP_EVENT);
  switch (event_id) {
    case IP_EVENT_STA_GOT_IP:
      ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
      ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
      s_wifi_retry_num = 0;
      xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
      break ;
    default:
      ESP_LOGI(TAG, "Other IP event: id=%d",  ((int)event_id) );
  }
}

/////////////////////

// Set wifi credentials
//   ssid maximum length is 31  
//   password maximum length is 63  
void wifi_set_credentials(const char *ssid, const char *password)
{
  wifi_config_t wifi_config = {
        .sta = {
          .ssid = wifi_ssid,
          .password = wifi_password,
          /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
           * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
           * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
           * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
           */
          .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
          .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
          .sae_h2e_identifier = PROJECT_H2E_IDENTIFIER,
        },
  };
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
}

bool wifi_start(bool wps, const char *hostname)
{
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_t *netif = esp_netif_create_default_wifi_sta();

    esp_netif_set_hostname(netif,hostname); // TODO: make it configurable.
      
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &ip_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );

    if (false) {
      wifi_config_t wifi_config = {
        .sta = {
            .ssid = wifi_ssid,
            .password = wifi_password,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = PROJECT_H2E_IDENTIFIER,
        },
      };
      ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    }
    
    ESP_ERROR_CHECK(esp_wifi_start() );

    wifi_config_t wifi_config;
    ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_config) );

#if 1
    wifi_connect_or_start_wps(wps) ;
#else    
    ESP_LOGI(TAG, "Current Wifi Config is SSID:%s password:%s",
             wifi_config.sta.ssid,
             wifi_config.sta.password);        
    if (wps) {
      wps_start(); 
    } else {
      esp_wifi_connect();
    }
#endif
    ESP_LOGI(TAG, "wifi_init_sta finished.");

    // TODO: Move everything below to main? 
    
    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by wifi_event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
      wifi_config_t wifi_config;
      ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_config) );
      ESP_LOGI(TAG, "Connected to ap SSID:%s password:%s",
               wifi_config.sta.ssid,
               wifi_config.sta.password);
      return true;
    } else if (bits & WIFI_FAIL_BIT) {
      wifi_config_t wifi_config ;
      ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_config) );
      ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
               wifi_config.sta.ssid,
               wifi_config.sta.password);
    } else {
      ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    return false; 
}
