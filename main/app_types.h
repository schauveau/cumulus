#pragma once

#include "char_buffer.h"

typedef enum {
  AC_MODE_AUTO,    // Use information provided by energy_meter (via MQTT) to adjust the production_target
  AC_MODE_MANUAL,  // Set to a fixed ratio  
} ac_mode_t;

#define APP_TIMEZONE_MAXLEN 64
#define APP_HOSTNAME_MAXLEN 32
#define APP_SSID_MAXLEN     32
#define APP_PASSWORD_MAXLEN 64
#define APP_MQTT_URI_MAXLEN 64

typedef char_buffer<32> app_ssid_t;
typedef char_buffer<32> app_password_t;
typedef char_buffer<64> app_timezone_t;
typedef char_buffer<32> app_hostname_t;
typedef char_buffer<64> app_mqtt_uri_t;

typedef struct {
  app_ssid_t ssid;
  app_password_t password; 
} app_wifi_cred_t  ;

// This is the public state of the application.
//
// Unless specified otherwise, all char buffers are NULL-terminated.  
//
typedef struct {
  ac_mode_t mode; 
  int  full_power;   // The estimated AC power when 100% ON (saved in nvs)
  int  frame_size;   // The default frame size (saved in nvs).
  app_timezone_t timezone; // The timezone in POSIX format (saved in nvs)
  app_hostname_t hostname; // The hostname (saved in nvs)
  struct {
    app_password_t password; // The password used by the user-interfaces
  } ui ; 
  app_wifi_cred_t wifi ;
  struct {
    app_mqtt_uri_t uri;
  } mqtt;
  // Settings for AUTO mode (using MQTT)
  struct {
    int available_power; // The available power as computed by mqtt (updated frequently)
    int over_power;      // Overshoot power by that amount.
    int min_power;       // Do not produce less than this value.
  } a;
  // Settings for MANUAL mode only
  struct {
    int power;  // The target power 
  } m;
  // Settings for Fallback mode
  struct {
    int power;  // The target power
  } f;
} app_state_t ; 

// 'stf' stands for STate Field
//
// Basically, stf::mask_t is a bit mask to address one or more fields of the state
// 
namespace stf {

typedef uint32_t mask_t;  

constexpr mask_t mode       = 1<<0 ;
constexpr mask_t frame_size = 1<<1 ;
constexpr mask_t full_power = 1<<2 ;
constexpr mask_t timezone   = 1<<3 ;
constexpr mask_t hostname   = 1<<4 ;
constexpr mask_t wifi_ssid  = 1<<5 ;
constexpr mask_t wifi_password = 1<<6 ;
constexpr mask_t ui_password  = 1<<7 ;
constexpr mask_t auto_available_power = 1<<8 ;
constexpr mask_t auto_over_power = 1<<9 ;
constexpr mask_t auto_min_power = 1<<10 ;
constexpr mask_t manual_power = 1<<11 ;
constexpr mask_t mqtt_uri   = 1<<12 ;

constexpr mask_t all   = mask_t(-1) ;

// All fields that are saved in NVS 
constexpr mask_t all_saved =
  frame_size | full_power | timezone | hostname |
  wifi_ssid | wifi_password | ui_password | mqtt_uri;

} 
