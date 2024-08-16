#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <esp_http_server.h>
#include "esp_tls_crypto.h"
#include <esp_wifi.h>

#include <esp_log.h>
#include "esp_check.h"
#include "cJSON.h"

#include "ui_http.h"
#include "resource.h"

static const char TAG[] = "ui_http";

static const char auth_username[] = "admin"; // For now, the username is hardcoded
static app_password_t auth_password; 

#define HTTPD_401  "401 UNAUTHORIZED"           /*!< HTTP Response 401 */

static bool check_auth_basic(httpd_req_t *req)
{
  // The Authorization header shall contains "Basic DIGEST" where DIGEST
  // is a base64 encoding of "username:password" so 3 bytes to encode 2 characters
  constexpr int digest_maxlen = 10 + 2*(sizeof(auth_username)+sizeof(auth_password)+1) ;
  
  char auth[digest_maxlen]; 
  if ( httpd_req_get_hdr_value_str(req, "Authorization", auth, sizeof(auth)) != ESP_OK) {
    return false;
  }
  ESP_LOGI(TAG, "Found   => 'Authorization: %s'", auth);

  char user_pass[sizeof(auth_username)+1+sizeof(auth_password)+1];
  snprintf(user_pass, sizeof(user_pass), "%s:%s", auth_username, auth_password.c_str() );

  char digest[digest_maxlen]={0};
  size_t n;
  strcpy(digest,"Basic ");
  esp_crypto_base64_encode((uint8_t*)digest+6, sizeof(digest)-6-1, &n, (const unsigned char *)user_pass, strlen(user_pass));
  
  ESP_LOGI(TAG, "Expect  => 'Authorization: %s'", digest);

  if (strcmp(auth, digest)!=0) {
    ESP_LOGI(TAG, "Basic Auth Failure");
    return false;
  }
  
  ESP_LOGI(TAG, "Basic Auth Success");
  return true;
}


static esp_err_t failed_auth_basic_response(httpd_req_t *req, const char *type, const char *response)
{
  constexpr bool keep_alive=false; // Don't bother to keep alive. We do not care about speed.
  httpd_resp_set_status(req, HTTPD_401);
  httpd_resp_set_type(req, type);
  if (keep_alive)
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
  httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Cumulus\"");  
  httpd_resp_send(req, response, response ? HTTPD_RESP_USE_STRLEN : 0 );
  return keep_alive ? ESP_OK : ESP_FAIL; 
}


#define URI_JSON_PREFIX "/json"
#define URI_DATA_PREFIX "/data/"
#define URI_PAGE_PREFIX "/page/"
#define URI_UPLOAD_PREFIX "/upload"

//
// Provide access to an embedded static resource 
//
static esp_err_t uri_resource_handler(httpd_req_t *req)
{
  ESP_LOGI(TAG, "request uri  %s", req->uri);

  if (!check_auth_basic(req)) {
    return failed_auth_basic_response(req,"text/html",NULL);
  }
  
  const char *name = req->user_ctx ? ((const char *)req->user_ctx) : req->uri;
  
  const resource_t *res = resource_get(name);
  if (res) {
    httpd_resp_set_type(req, res->type);            
    httpd_resp_send(req, res->data, res->size); 
    return ESP_OK;
  }
  
  httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Unknown resource");
  return ESP_FAIL;
}


//
// The /upload/ uri is used to overwrite an ALREADY EXISTING resource. 
// The change is only temporary.
//
// The main purpose is to avoid flashing the device during the development
// of the web interface. 
//
// Example:
//    curl -X POST --data-binary @path/to/index.html  http://username:password@mydevice/upload/index.html
//
//
static esp_err_t uri_upload_handler(httpd_req_t *req)
{
  if (!check_auth_basic(req)) {
    return failed_auth_basic_response(req,"text/plain","failed");
  }

  const char * name = req->uri+strlen("/upload"); 
  ESP_LOGI(TAG, "Uploading resource '%s'", name);

  const resource_t *resource = resource_get(name) ;
  
  if ( !resource ) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown resource");
    return ESP_FAIL;
  }

  int size = req->content_len ;

  if ( size == 0 ) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No content");
    return ESP_FAIL;
  }

  // Do not allow very large uploads
  if ( size > 10*1024 ) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Upload is too large");
    return ESP_FAIL;
  }

  char *data = (char*)malloc(size + (resource->is_str?1:0)) ;
  if (!data) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    return ESP_FAIL;
  }

  int pos = 0; 
  int remaining = size; 
  while (remaining > 0) {
    /* Read the data for the request */
    int n = httpd_req_recv(req, data+pos, remaining) ;
    if (n <= 0) {
      if (n == HTTPD_SOCK_ERR_TIMEOUT) {
        /* Retry receiving if timeout occurred */
        continue;
      }
      return ESP_FAIL;
    }
    remaining -= n;
    pos += n;
  }

  if (resource->is_str) {
    data[size]='\0';
    if (strlen(data) != size) {
      free(data);
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unexpected null byte in string resource");
      return ESP_FAIL;
    }
  }

  if (resource->is_str) {
    resource_update_string(name,data,size);
  } else {
    resource_update_binary(name,data,size);
  }
  
  ESP_LOGI(TAG, "Received %d bytes", (int) req->content_len);

  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

  

static void
json_add_state_items(cJSON *output, stf::mask_t mask, app_state_t &state, bool update=true)
{
  if (update)
    app_post_query_state(&state);

  if (mask & stf::mode) 
    {
      const char *mode_name;
      switch(state.mode) {
        case AC_MODE_AUTO:   mode_name="auto"; break ;
        case AC_MODE_MANUAL: mode_name="manual"; break;
        default: mode_name="unknown" ; break; 
      }
      cJSON_AddItemToObject(output, "mode", cJSON_CreateStringReference(mode_name) );
    }
    
  if (mask & stf::frame_size) {
    cJSON_AddItemToObject(output, "frame_size", cJSON_CreateNumber(state.frame_size) );
  }

  if (mask & stf::full_power) {
    cJSON_AddItemToObject(output, "full_power", cJSON_CreateNumber(state.full_power) );
  }

  if (mask & stf::timezone)
  {
    cJSON_AddItemToObject(output, "timezone", cJSON_CreateStringReference(state.timezone.c_str()) );
    // Also generate the localtime 
    time_t now;
    struct tm timeinfo;
    char buffer[64];
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(buffer, sizeof(buffer), "%F %T UTC%z", &timeinfo);
    cJSON_AddItemToObject(output, "localtime", cJSON_CreateString(buffer) );
  }

  if (mask & stf::hostname) {
    cJSON_AddItemToObject(output, "hostname", cJSON_CreateStringReference(state.hostname.c_str()) );
  }
  
  if (mask & stf::mqtt_uri) {
    cJSON_AddItemToObject(output, "mqtt_uri", cJSON_CreateStringReference(state.mqtt.uri.c_str()) );
  }
      
  if (mask & stf::wifi_ssid ) {
    cJSON_AddItemToObject(output, "wifi_ssid", cJSON_CreateStringReference(state.wifi.ssid.c_str()) );
  }

  if (mask & stf::wifi_password) {
    cJSON_AddItemToObject(output, "wifi_password", cJSON_CreateStringReference(state.wifi.password.c_str()) );
  }

  if (mask & stf::ui_password) {
    cJSON_AddItemToObject(output, "ui_password", cJSON_CreateStringReference(state.ui.password.c_str()) );
  }
      
  if (mask & stf::auto_available_power) {
    cJSON_AddItemToObject(output, "auto_available_power", cJSON_CreateNumber(state.a.available_power) );
  }

  if (mask & stf::auto_over_power) {
    cJSON_AddItemToObject(output, "auto_over_power", cJSON_CreateNumber(state.a.over_power) );
  }

  if (mask & stf::auto_min_power) {
    cJSON_AddItemToObject(output, "auto_min_power", cJSON_CreateNumber(state.a.min_power) );
  }

  if (mask & stf::manual_power) {
    cJSON_AddItemToObject(output, "manual_power", cJSON_CreateNumber(state.m.power) );
  } 
  
}

static void
json_add_error(cJSON *output, const char *format, ...)
{
  char msg[100];
  va_list args;
  
  va_start(args, format);
  vsnprintf(msg, sizeof(msg), format, args);
  va_end(args);
  msg[sizeof(msg)-1] = '\0'; 
  
  cJSON_AddItemToObject(output, "error", cJSON_CreateString(msg) );
}

static bool
json_get_string(cJSON *input, cJSON *output, const char *name, const char **value)
{  
  cJSON *item = cJSON_GetObjectItemCaseSensitive(input,name) ; 
  if ( !item ) {
    json_add_error(output, "Missing field %s", name);
    return false;
  }
  if (!cJSON_IsString(item) ) {
    json_add_error(output, "String expected for field %s", name);
    return false; 
  }

  *value = item->valuestring;
  return true; 
}

// Reminder: json numbers are all 'double' but cJSON also provide
//           a int conversion in 'cJSON::valueint'
static bool
json_get_int(cJSON *input, cJSON *output, const char *name, int *value)
{  
  cJSON *item = cJSON_GetObjectItemCaseSensitive(input,name) ; 
  if ( !item ) {
    json_add_error(output, "Missing field %s", name);
    return false;
  }
  if (!cJSON_IsNumber(item) ) {
    json_add_error(output, "Number expected for field %s", name);
    return false; 
  }

  *value = item->valueint;
  
  return true; 
}

// Get an optional boolean field 
static bool
json_get_opt_bool(cJSON *input,  const char *name, bool default_value=false)
{  
  cJSON *item = cJSON_GetObjectItemCaseSensitive(input,name) ; 
  if ( cJSON_IsTrue(item) ) {
    return true;
  } else if ( cJSON_IsFalse(item) ) {
    return false;
  } else {
    return default_value;
  }
}

static bool process_json_get_state(cJSON *input, cJSON *output, app_state_t &state)
{
  json_add_state_items(output, stf::all, state) ;
  return true ;
}

static bool process_json_set_full_power(cJSON *input, cJSON *output, app_state_t &state)
{
  int value; 
  if (!json_get_int(input, output, "value", &value))
    return false;
  bool save = json_get_opt_bool(input, "save", false);
  app_post_full_power(value, save);  
  json_add_state_items(output, stf::full_power, state) ;
  return true ;
}

static bool process_json_set_frame_size(cJSON *input, cJSON *output, app_state_t &state)
{
  int value; 
  if (!json_get_int(input, output, "value", &value))
    return false; 
  bool save = json_get_opt_bool(input, "save", false);
  app_post_frame_size(value, save);  
  json_add_state_items(output, stf::frame_size, state) ;
  return true ;
}

static bool process_json_set_wifi_cred(cJSON *input, cJSON *output, app_state_t &state)
{

  const char * ssid ; 
  if (!json_get_string(input, output, "wifi_ssid", &ssid))
    return false; 

  const char * password ; 
  if (!json_get_string(input, output, "wifi_password", &password))
    return false; 

  app_post_wifi_cred(ssid, password) ;

  json_add_state_items(output, stf::wifi_ssid | stf::wifi_password, state) ;
  
  return true ;
}

static bool process_json_reboot(cJSON *input, cJSON *output, app_state_t &state)
{  
  app_post_reboot() ;
  return true ;
}


static bool process_json_set_time(cJSON *input, cJSON *output, app_state_t &state)
{  
  const char *timezone ; 
  if (!json_get_string(input, output, "timezone", &timezone))
    return false; 
  app_post_timezone(timezone) ;
  json_add_state_items(output, stf::timezone, state) ;
  return true ;
}

static bool process_json_set_hostname(cJSON *input, cJSON *output, app_state_t &state)
{  
  const char *hostname ; 
  if (!json_get_string(input, output, "hostname", &hostname))
    return false; 
  app_post_hostname(hostname) ;
  json_add_state_items(output, stf::hostname, state) ;
  return true ;
}


static bool process_json_set_mqtt(cJSON *input, cJSON *output, app_state_t &state)
{  
  const char *mqtt_uri ; 
  if (!json_get_string(input, output, "mqtt_uri", &mqtt_uri))
    return false; 
  app_post_mqtt_uri(mqtt_uri) ;
  json_add_state_items(output, stf::mqtt_uri, state) ;
  return true ;
}



static bool process_json_set_ui(cJSON *input, cJSON *output, app_state_t &state)
{  
  const char *password ; 
  if (!json_get_string(input, output, "ui_password", &password))
    return false; 
  app_post_ui_password(password) ;
  json_add_state_items(output, stf::ui_password, state) ;
  return true ;
}


static bool process_json_set_auto_mode(cJSON *input, cJSON *output, app_state_t &state)
{  
  int auto_over_power=0; 
  if (!json_get_int(input, output, "auto_over_power", &auto_over_power))
    return false; 
  int auto_min_power=0; 
  if (!json_get_int(input, output, "auto_min_power", &auto_min_power))
    return false;

  app_post_auto_min_power(auto_min_power);
  app_post_auto_over_power(auto_over_power);

  json_add_state_items(output,
                       stf::auto_over_power | stf::auto_min_power,
                       state) ;
  return true ;
}

static bool process_json_set_manual_mode(cJSON *input, cJSON *output, app_state_t &state)
{  
  int manual_power=0; 
  if (!json_get_int(input, output, "manual_power", &manual_power))
    return false; 
  
  app_post_manual_power(manual_power);

  json_add_state_items(output,
                       stf::manual_power,
                       state) ;
  return true ;
}

static bool process_json_request(httpd_req_t *req, cJSON *input, cJSON *output, app_state_t &state)
{
  
  if ( !cJSON_IsObject(input)) {
    json_add_error(output,"Bad json");
    return false; 
  }

  const char *action ;
  if (!json_get_string(input, output, "action", &action)) {
    json_add_error(output,"Missing action item");
    return false; 
  }

  if (strcmp(action,"reboot")==0) {
    return process_json_reboot(input,output,state);
  } else if (strcmp(action,"get-state")==0) {
    return process_json_get_state(input,output,state);
  } else if (strcmp(action,"set-full-power")==0) {
    return process_json_set_full_power(input,output,state);
  } else if (strcmp(action,"set-frame-size")==0) {
    return process_json_set_frame_size(input,output,state);
  } else if (strcmp(action,"set-wifi-cred")==0) {
    return process_json_set_wifi_cred(input,output,state);
  } else if (strcmp(action,"set-time")==0) {
    return process_json_set_time(input,output,state);
  } else if (strcmp(action,"set-hostname")==0) {
    return process_json_set_hostname(input,output,state);
  } else if (strcmp(action,"auto-mode")==0) {
    return process_json_set_auto_mode(input,output,state);
  } else if (strcmp(action,"manual-mode")==0) {
    return process_json_set_manual_mode(input,output,state);
  } else if (strcmp(action,"mqtt")==0) {
    return process_json_set_mqtt(input,output,state);
  } else if (strcmp(action,"ui")==0) {
    return process_json_set_ui(input,output,state);
  } else {
    json_add_error(output,"Unsupported action");
    return false; 
  }

  return true;
}


//
// Example:
//    curl --header "Content-Type: application/json" --request POST --data '{"action":"get-state"}'  "http://xxxxx/json" 
//
static esp_err_t uri_json_handler(httpd_req_t *req)
{
  const char * prefix = (const char *)req->user_ctx;
  (void) prefix;
  
  ESP_LOGI(TAG, "Processing json request");

  if (!check_auth_basic(req)) {
    return failed_auth_basic_response(req,"application/json",
                                      "{\"error\":\"Failed authentication\"}");
  }

  if ( req->content_len == 0 ) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No content");
    return ESP_FAIL;
  }

  // JSON message shall be small
  if ( req->content_len > 1024 ) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON is too large");
    return ESP_FAIL;
  }
  
  char *data = (char*)malloc(req->content_len+1) ;
  if (!data) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    return ESP_FAIL;
  }
  
  int pos = 0; 
  int remaining = req->content_len; 
  while (remaining > 0) {
    /* Read the data for the request */
    int n = httpd_req_recv(req, data+pos, remaining) ;
    if (n <= 0) {
      if (n == HTTPD_SOCK_ERR_TIMEOUT) {
        /* Retry receiving if timeout occurred */
        continue;
      }
      return ESP_FAIL;
    }
    remaining -= n;
  }

  ESP_LOGI(TAG, "Received %d bytes", (int) req->content_len);
  data[req->content_len]=0;
  ESP_LOGI(TAG, " <<= %s", data);

  bool formatted = true;  
  httpd_resp_set_type(req, "application/json");
  cJSON *input  = cJSON_ParseWithLength(data, req->content_len) ;
  cJSON *output = cJSON_CreateObject();
  app_state_t state; // not initialized yet. Must remain alive until cJSON_Print below
  process_json_request(req,input,output,state) ;
  cJSON_Delete(input);

  char *out = formatted ? cJSON_Print(output) : cJSON_PrintUnformatted(output) ;
  httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
  ESP_LOGI(TAG, " =>> %s", out);
  free(out); 
  cJSON_Delete(output);
  return ESP_OK;
  
}

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.server_port = 80;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
      
    const httpd_uri_t all_uris[] = {
      {
        .uri       = "/json",
        .method    = HTTP_POST,
        .handler   = uri_json_handler,
      },
      {
        .uri       = "/upload/*",
        .method    = HTTP_POST,
        .handler   = uri_upload_handler,
      },
      {
        .uri       = "/data/*",
        .method    = HTTP_GET,
        .handler   = uri_resource_handler,
      },
      {
        .uri       = "/favicon.ico",
        .method    = HTTP_GET,
        .handler   = uri_resource_handler,
        .user_ctx  = (void*)"/data/icon.png"
      },
      {
        .uri       = "/page/*",
        .method    = HTTP_GET,
        .handler   = uri_resource_handler,
      },
      {
        .uri       = "/index.html",
        .method    = HTTP_GET,
        .handler   = uri_resource_handler,
      },
      {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = uri_resource_handler,
        .user_ctx  = (void*)"/index.html"       
      }
    };

      // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        for ( auto &uri : all_uris ) { 
          httpd_register_uri_handler(server, &uri);
        }
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_webserver(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}

//
// Must be called after before connecting to Wifi
// but after esp_netif_init() and esp_event_loop_create_default()
//
void ui_http_start(const app_password_t &password)
{
    static httpd_handle_t server = NULL;

    auth_password = password;

    ESP_LOGI(TAG, "UI password is %s", auth_password.c_str());

    // Stop server when WiFi is disconnected
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));

    // Start server when WiFi is connected and get an IP address
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));

    /* Start the server for the first time */
    server = start_webserver();
}
