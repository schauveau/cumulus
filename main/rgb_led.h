#pragma once


enum {
  RGB_NONE    = 0x000000,
  RGB_OFF     = RGB_NONE,
  // My led is very bright so do not use full intensity
  RGB_WHITE   = 0x554444,
  RGB_RED     = 0x551111,
  RGB_GREEN   = 0x003300,
  RGB_BLUE    = 0x000022,
  RGB_ORANGE  = 0x552200,  
  RGB_PURPLE  = 0x440044,
} ;

// Initialize the RGB led
void led_init(void);

void led_set_rgb(uint32_t rgb);
void led_set_hsv(uint16_t h, uint8_t s, uint8_t v);


typedef enum {
  LED_WIFI_DISCONNECTED,
  LED_WIFI_WPS,
  LED_WIFI_CONNECTING,
  LED_WIFI_CONNECTED,
} led_wifi_status_t ;

extern led_wifi_status_t led_wifi_status ;
extern bool led_mqtt_connected ; // true if successfully connected to a MQTT broker



  
