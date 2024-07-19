#pragma once

void app_init() ;


void app_sync(void);

void app_post_set_mode_manual(void);
void app_post_set_mode_auto(void);
void app_post_set_available_power(double value) ;

// TODO: Everything below should be accessed via events.


extern char    app_hostname[32]; // The hostname (saved in nvs)
extern int32_t app_full_power;  // The estimated AC power when 100% ON (saved in nvs)
extern int32_t app_frame_size; // The default frame size (saved in nvs). 


void set_hostname(const char *hostname) ; // TODO: make an APP event
void set_full_power(int value) ; // TODO: make an APP event
void set_default_frame_size(int value) ; // TODO: make an APP event

