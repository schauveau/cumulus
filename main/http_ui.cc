
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

//#include "esp_netif.h"
//#include "esp_tls_crypto.h"
#include <esp_http_server.h>
//#include "esp_event.h"
//#include "esp_netif.h"
//#include "esp_tls.h"

// for HTTP Basic Auth
#include "esp_tls_crypto.h"

// for WIFI_EVENT
#include <esp_wifi.h>

#include <esp_log.h>
#include "esp_check.h"

#include "resource.h"

#define EXAMPLE_HTTP_QUERY_KEY_MAX_LEN  (64)

static const char TAG[] = "http_ui";

///////// Basic Auth //////////////////////////////////////

typedef struct {
    char    *username;
    char    *password;
} basic_auth_info_t;

#define HTTPD_401      "401 UNAUTHORIZED"           /*!< HTTP Response 401 */

static char *http_auth_basic(const char *username, const char *password)
{
    size_t out;
    char *user_info = NULL;
    char *digest = NULL;
    size_t n = 0;
    int rc = asprintf(&user_info, "%s:%s", username, password);
    if (rc < 0) {
        ESP_LOGE(TAG, "asprintf() returned: %d", rc);
        return NULL;
    }

    if (!user_info) {
        ESP_LOGE(TAG, "No enough memory for user information");
        return NULL;
    }
    esp_crypto_base64_encode(NULL, 0, &n, (const unsigned char *)user_info, strlen(user_info));

    /* 6: The length of the "Basic " string
     * n: Number of bytes for a base64 encode format
     * 1: Number of bytes for a reserved which be used to fill zero
    */
    digest = (char*)calloc(1, 6 + n + 1);
    if (digest) {
        strcpy(digest, "Basic ");
        esp_crypto_base64_encode((unsigned char *)digest + 6, n, &out, (const unsigned char *)user_info, strlen(user_info));
    }
    free(user_info);
    return digest;
}

/* An HTTP GET handler */
static esp_err_t uri_basic_auth_get_handler(httpd_req_t *req)
{
    char *buf = NULL;
    size_t buf_len = 0;
    basic_auth_info_t *basic_auth_info = (basic_auth_info_t *)req->user_ctx;

    buf_len = httpd_req_get_hdr_value_len(req, "Authorization") + 1;
    if (buf_len > 1) {
        buf = (char*)calloc(1, buf_len);
        if (!buf) {
            ESP_LOGE(TAG, "No enough memory for basic authorization");
            return ESP_ERR_NO_MEM;
        }

        if (httpd_req_get_hdr_value_str(req, "Authorization", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Authorization: %s", buf);
        } else {
            ESP_LOGE(TAG, "No auth value received");
        }

        char *auth_credentials = http_auth_basic(basic_auth_info->username, basic_auth_info->password);
        if (!auth_credentials) {
            ESP_LOGE(TAG, "No enough memory for basic authorization credentials");
            free(buf);
            return ESP_ERR_NO_MEM;
        }

        if (strncmp(auth_credentials, buf, buf_len)) {
            ESP_LOGE(TAG, "Not authenticated");
            httpd_resp_set_status(req, HTTPD_401);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Connection", "keep-alive");
            httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Hello\"");
            httpd_resp_send(req, NULL, 0);
        } else {
            ESP_LOGI(TAG, "Authenticated!");
            char *basic_auth_resp = NULL;
            httpd_resp_set_status(req, HTTPD_200);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Connection", "keep-alive");
            int rc = asprintf(&basic_auth_resp, "{\"authenticated\": true,\"user\": \"%s\"}", basic_auth_info->username);
            if (rc < 0) {
                ESP_LOGE(TAG, "asprintf() returned: %d", rc);
                free(auth_credentials);
                return ESP_FAIL;
            }
            if (!basic_auth_resp) {
                ESP_LOGE(TAG, "No enough memory for basic authorization response");
                free(auth_credentials);
                free(buf);
                return ESP_ERR_NO_MEM;
            }
            httpd_resp_send(req, basic_auth_resp, strlen(basic_auth_resp));
            free(basic_auth_resp);
        }
        free(auth_credentials);
        free(buf);
    } else {
        ESP_LOGE(TAG, "No auth header received");
        httpd_resp_set_status(req, HTTPD_401);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Connection", "keep-alive");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Hello\"");
        httpd_resp_send(req, NULL, 0);
    }

    return ESP_OK;
}

static httpd_uri_t uri_basic_auth = {
    .uri       = "/basic_auth",
    .method    = HTTP_GET,
    .handler   = uri_basic_auth_get_handler,
};

static void httpd_register_basic_auth(httpd_handle_t server)
{
  basic_auth_info_t *basic_auth_info = (basic_auth_info_t*) calloc(1, sizeof(basic_auth_info_t));
  if (basic_auth_info) {
    basic_auth_info->username = "admin";
    basic_auth_info->password = "foobar";
    
    uri_basic_auth.user_ctx = basic_auth_info;
    httpd_register_uri_handler(server, &uri_basic_auth);
  }
}


///////////////////////////////////////////////

static char *find_header(httpd_req_t *req, const char *header)
{
  size_t len = httpd_req_get_hdr_value_len(req, header) + 1;
  if (len > 1) {
    char * buf = (char*) malloc(len);
    if (buf) {
      if (httpd_req_get_hdr_value_str(req, header, buf, len) == ESP_OK) {
        return buf;
      }
      free(buf);
    }
  }
  return NULL; 
}

#define URI_DATA_PREFIX "/data/"
#define URI_PAGE_PREFIX "/page/"
#define URI_UPLOAD_PREFIX "/upload/"

//
// Provide access to embedded static resource 
//
static esp_err_t uri_data_get_handler(httpd_req_t *req)
{
  ESP_LOGI(TAG, "request uri  %s", req->uri);
  const resource_t *res = resource_get(req->uri);
  if (res) {
    httpd_resp_set_type(req, res->type);            
    httpd_resp_send(req, res->data, res->size); 
    return ESP_OK;
  }
  
  httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Unknown resource");
  return ESP_FAIL;
}

static const httpd_uri_t uri_data = {
    .uri       = URI_DATA_PREFIX "*",
    .method    = HTTP_GET,
    .handler   = uri_data_get_handler,
    .user_ctx  = NULL
};

#define VARSIZE 16

static bool is_alpha(char c) { return ('a' <= c && c <='z') || ('A' <= c && c <='Z') ; }
static bool is_digit(char c) { return ('0' <= c && c <='9') ; }
static bool is_alphanum(char c) { return is_alpha(c) || is_digit(c) ; }


bool parse_char(const char *text, int *at, char c) {
  int pos = *at;
  if ( text[pos++]==c ) {
    *at=pos;
    return true; 
  }
  return false;
}

// Parse one of $VARNAME, $!VARNAME and $@VARNAME
bool parse_varname(const char *text, int *at, char *varopt, char varname[VARSIZE])
{
  int  pos = *at;
  char opt = 0; 
  char c = text[pos];
  if ( c=='!' || c=='@' ) {
    opt=c;
    c=text[pos++];
  }
  if ( is_alpha(c) || c=='_' ) {
    int i;
    varname[0]=c;
    for (i=1;i<VARSIZE;i++) {
      char c = text[pos+i];
      if ( is_alphanum(c) || c=='_' ) {
        varname[i]=c ;
      } else {
        break;
      }
    }
    varname[i]='\0';
    *varopt = opt ; 
    *at = pos+i ; 
    return true; 
  }
  return false;
}

// Parse one of ${VARNAME} ${!VARNAME} and ${@VARNAME}
//
bool parse_quoted_varname(const char *text, int *at, char *varopt, char varname[VARSIZE])
{
  int pos = *at;
  char opt='\0';
  if ( parse_char(text,&pos,'{') &&
       parse_varname(text, &pos, &opt, varname) &&
       parse_char(text,&pos,'}') ) {
    *at = pos;
    *varopt = opt;
    return true; 
  }  
  return false;
}


//
// Provide access to preprocessed html pages 
//
static esp_err_t uri_page_handler(httpd_req_t *req)
{
  ESP_LOGI(TAG, "request page uri %s", req->uri);

  // Allow both GET and POST
  if (! ( req->method == HTTP_GET || req->method == HTTP_POST )) {
    httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, NULL);
    return ESP_FAIL;
  }
  
  const resource_t *res = resource_get_string(req->uri);
  if (!res) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Unknown resource");
    return ESP_FAIL;
  }

  if ( req->method == HTTP_POST ) {
    // TODO: Process the arguments in the body
  }

  // Not really needed but lets's be paranoid.
  if (true && strlen(res->data) != res->size) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, res->type);

  // pre-process and send the resource
  char varname[VARSIZE];
  char varopt;
  const char *text = res->data;
  int at=0 ;
  int size=res->size ;
  while (at < size) {
    const char *escape = strchr(text+at,'$');
    if (escape==NULL) {
      // This is the last chunk
      httpd_resp_send_chunk(req, text+at, size-at );
      at = size;
      break;
    }
    // Send the chunk before the '$'
    int chunk_size = escape-(text+at);
    if (chunk_size>0) {
     printf("==> send chunk %d\n",chunk_size);
     httpd_resp_send_chunk(req, text+at, chunk_size);
      at += chunk_size ;
    }
    printf("==> found %d chars then '$'\n",chunk_size);
    at++; // skip the $
    // And parse the rest of the escape
    varopt=0;
    if (parse_char(text,&at,'$')) {
      // A double '$$' -> '$'
      printf("==> send standalone '$'\n");
      httpd_resp_send_chunk(req, "$", 1 );
      continue;
    } else if (parse_varname(text, &at, &varopt, varname) ||
               parse_quoted_varname(text, &at, &varopt, varname )) {
      printf("==> found '$%s'\n ",varname);
      httpd_resp_send_chunk(req, "{{{", HTTPD_RESP_USE_STRLEN );
      httpd_resp_send_chunk(req, varname, HTTPD_RESP_USE_STRLEN );
      httpd_resp_send_chunk(req, "}}}", HTTPD_RESP_USE_STRLEN );     
    } else {
      /// An orphan $. What should be do?
      ESP_LOGI(TAG, "Found orphan '$'");
      httpd_resp_send_chunk(req, "$", HTTPD_RESP_USE_STRLEN );
      continue;
    }
  }
  httpd_resp_send_chunk(req, NULL, 0 );

  return ESP_OK;
}

static const httpd_uri_t uri_page = {
    .uri       = URI_PAGE_PREFIX "*",
    .method    = (httpd_method_t)HTTP_ANY,  // GET or POST
    .handler   = uri_page_handler,
    .user_ctx  = NULL
};


static esp_err_t uri_dump_post_handler(httpd_req_t *req)
{
  // Allow both GET and POST
  if (! ( req->method == HTTP_GET || req->method == HTTP_POST )) {
    httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, NULL);
    return ESP_FAIL;
  }
  
  printf("==> method : %d\n", req->method );
  
  char *content_type = find_header(req, "Content-Type");
  if (content_type) {
    ESP_LOGI(TAG, "Content-Type: %s", content_type);
  }
  free(content_type);

  extern const char favicon_start[] asm("_binary_" "test" "_png_start");
  extern const char favicon_end[] asm("_binary_test_png_end");
  size_t favicon_len = favicon_end - favicon_start;
  
  httpd_resp_set_type(req, "image/png");
  httpd_resp_send(req, favicon_start, favicon_len);
    
//  const char* resp_str = "ok" ;
//  httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    
  return ESP_OK;  
}

static const httpd_uri_t uri_dump = {
    .uri       = "/dump/?*",
    .method    = (httpd_method_t) HTTP_ANY,
    .handler   = uri_dump_post_handler,
    .user_ctx  = NULL
};

/* An HTTP GET handler */
static esp_err_t uri_hello_get_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;

    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
    if (buf_len > 1) {
      buf = (char*)malloc(buf_len);
        ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "buffer alloc failed");
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Host: %s", buf);
        }
        free(buf);
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Test-Header-2") + 1;
    if (buf_len > 1) {
        buf = (char*)malloc(buf_len);
        ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "buffer alloc failed");
        if (httpd_req_get_hdr_value_str(req, "Test-Header-2", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Test-Header-2: %s", buf);
        }
        free(buf);
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Test-Header-1") + 1;
    if (buf_len > 1) {
        buf = (char*)malloc(buf_len);
        ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "buffer alloc failed");
        if (httpd_req_get_hdr_value_str(req, "Test-Header-1", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Test-Header-1: %s", buf);
        }
        free(buf);
    }

    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = (char*)malloc(buf_len);
        ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "buffer alloc failed");
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            char param[EXAMPLE_HTTP_QUERY_KEY_MAX_LEN] ;
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "query1", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query1=%s", param);
                //example_uri_decode(dec_param, param, strnlen(param, EXAMPLE_HTTP_QUERY_KEY_MAX_LEN));
                //ESP_LOGI(TAG, "Decoded query parameter => %s", dec_param);
            }
            if (httpd_query_key_value(buf, "query3", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query3=%s", param);
                //example_uri_decode(dec_param, param, strnlen(param, EXAMPLE_HTTP_QUERY_KEY_MAX_LEN));
                //ESP_LOGI(TAG, "Decoded query parameter => %s", dec_param);
            }
            if (httpd_query_key_value(buf, "query2", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query2=%s", param);
                //example_uri_decode(dec_param, param, strnlen(param, EXAMPLE_HTTP_QUERY_KEY_MAX_LEN));
                //ESP_LOGI(TAG, "Decoded query parameter => %s", dec_param);
            }
        }
        free(buf);
    }

    /* Set some custom headers */
    httpd_resp_set_hdr(req, "Custom-Header-1", "Custom-Value-1");
    httpd_resp_set_hdr(req, "Custom-Header-2", "Custom-Value-2");

    /* Send response with custom headers and body set as the
     * string passed in user context*/
    const char* resp_str = (const char*) req->user_ctx;
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

    /* After sending the HTTP response the old HTTP request
     * headers are lost. Check if HTTP request headers can be read now. */
    if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
        ESP_LOGI(TAG, "Request headers lost");
    }
    return ESP_OK;
}

static const httpd_uri_t uri_hello = {
    .uri       = "/hello",
    .method    = HTTP_GET,
    .handler   = uri_hello_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = (void*)"Hello World!"
};


//
// Example:
//    curl -X POST --data-binary @path/to/foobar.txt  http://mydevice/upload/foobar.txt
//
//
static esp_err_t uri_upload_post_handler(httpd_req_t *req)
{
  const char * prefix = (const char *)req->user_ctx;
  const char * name = req->uri + strlen(prefix);

  ESP_LOGI(TAG, "Uploading '%s'", name);

  if ( req->content_len == 0 ) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No content");
    return ESP_FAIL;
  }

  // Do not allow very large uploads
  if ( req->content_len > 10*1024 ) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Upload is too large");
    return ESP_FAIL;
  }
  
  char *data = (char*)malloc(req->content_len) ;
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

  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}


static const httpd_uri_t uri_upload = {
    .uri       = URI_UPLOAD_PREFIX "*",
    .method    = HTTP_POST,
    .handler   = uri_upload_post_handler,
    .user_ctx  = (void*) URI_UPLOAD_PREFIX,
};

/* This handler allows the custom error handling functionality to be
 * tested from client side. For that, when a PUT request 0 is sent to
 * URI /ctrl, the /hello and /echo URIs are unregistered and following
 * custom error handler http_404_error_handler() is registered.
 * Afterwards, when /hello or /echo is requested, this custom error
 * handler is invoked which, after sending an error message to client,
 * either closes the underlying socket (when requested URI is /echo)
 * or keeps it open (when requested URI is /hello). This allows the
 * client to infer if the custom error handler is functioning as expected
 * by observing the socket state.
 */
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    if (strcmp("/hello", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/hello URI is not available");
        /* Return ESP_OK to keep underlying socket open */
        return ESP_OK;
    } else if (strcmp("/echo", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/echo URI is not available");
        /* Return ESP_FAIL to close underlying socket */
        return ESP_FAIL;
    }
    /* For any other URI send 404 and close socket */
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
    return ESP_FAIL;
}

/* An HTTP PUT handler. This demonstrates realtime
 * registration and deregistration of URI handlers
 */
static esp_err_t uri_ctrl_put_handler(httpd_req_t *req)
{
    char buf;
    int ret;
    
    ret = httpd_req_recv(req, &buf, 1);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }

    if (buf == '0') {
        /* URI handlers can be unregistered using the uri string */
        ESP_LOGI(TAG, "Unregistering /hello and /upload URIs");
        httpd_unregister_uri(req->handle, uri_hello.uri);
        httpd_unregister_uri(req->handle, uri_upload.uri );
        /* Register the custom error handler */
        httpd_register_err_handler(req->handle, HTTPD_404_NOT_FOUND, http_404_error_handler);
    }
    else {
        ESP_LOGI(TAG, "Registering /hello and /upload URIs");
        httpd_register_uri_handler(req->handle, &uri_hello);
        httpd_register_uri_handler(req->handle, &uri_upload);
        /* Unregister custom error handler */
        httpd_register_err_handler(req->handle, HTTPD_404_NOT_FOUND, NULL);
    }

    /* Respond with empty body */
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t uri_ctrl = {
    .uri       = "/ctrl",
    .method    = HTTP_PUT,
    .handler   = uri_ctrl_put_handler,
    .user_ctx  = NULL
};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.server_port = 80;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
      
    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &uri_data);
        httpd_register_uri_handler(server, &uri_page);
        httpd_register_uri_handler(server, &uri_hello);
        httpd_register_uri_handler(server, &uri_upload);
        httpd_register_uri_handler(server, &uri_ctrl);
        httpd_register_uri_handler(server, &uri_dump);
        httpd_register_basic_auth(server);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

#if !CONFIG_IDF_TARGET_LINUX
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
#endif // !CONFIG_IDF_TARGET_LINUX

//
// Must be called after before connecting to Wifi
// but after esp_netif_init() and esp_event_loop_create_default()
//
void start_http_ui()
{
    static httpd_handle_t server = NULL;

    // Stop server when WiFi is disconnected
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));

    // Start server when WiFi is connected and get an IP address
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,   IP_EVENT_STA_GOT_IP, &connect_handler, &server));

    /* Start the server for the first time */
    server = start_webserver();

    //while (server) {
    //    sleep(5);
    //}
}
