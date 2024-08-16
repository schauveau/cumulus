#pragma once

#include "app_types.h"

void app_init() ;

void app_sync(void);

void app_post_reboot(void);

// Request that a copy of the application state shall be put into state_copy.
// 
// If async is true then app_sync shall be called to insure that the copy is available.
//
void app_post_query_state(app_state_t *state_copy, bool async=false);

// Set the operation mode
void app_post_mode(ac_mode_t mode);

// Set the AC frame size in half-period (so 1/100 seconds if AC 50Hz)
// If save is true then also save that value in NVS as the new default. 
void app_post_frame_size(int value, bool save=false);

// Set the expected power when the AC relay is fully ON
void app_post_full_power(int value, bool save=false);

// Report a button press or release
// The idea is that app_post_button_press shall be called at regular intervals
// as long as the button remains pressed (with an increasing duration_ms).
// it is released and app_post_button_release is called.
void app_post_button_press(char id, int duration_ms);
void app_post_button_release(char id, int duration_ms);

// Set the new timezone (in POSIX format)
void app_post_timezone(const char *timezone);

void app_post_wifi_cred(const char *ssid, const char *password);

// Set values for AUTO mode 
void app_post_auto_available_power(int value) ;
void app_post_auto_min_power(int value) ;
void app_post_auto_over_power(int value) ;
void app_post_auto_fallback_power(int value) ;

// Set values for MANUAL mode 
void app_post_manual_power(int value) ;

// Set the hostname. Its length shall be between 1 and APP_HOSTNAME_MAXLEN
void app_post_hostname(const char *hostname);

// Set the MQTT broker URI (will take effect after reboot)
void app_post_mqtt_uri(const char *uri);

//void set_hostname(const char *hostname) ; // TODO: make an APP event
//void set_full_power(int value) ; // TODO: make an APP event
void set_default_frame_size(int value) ; // TODO: make an APP event

// Set UI password (will take effect after reboot)
void app_post_ui_password(const char *password) ;
