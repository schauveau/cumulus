/* Custom button driver via GPIO.
 *    ==> Modified from ESP-IDF official example
 #
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * Zigbee switch driver example
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#pragma once

#include "project.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* user should configure which I/O port as toggle switch input, default is GPIO9 */
//#define GPIO_INPUT_IO_TOGGLE_SWITCH  GPIO_NUM_9

/* config button level depends on the pull up/down setting
 * push button level is on level = 1 when pull-down enable
 * push button level is on level = 0 when pull-up enable
 */
#define GPIO_INPUT_LEVEL_ON     0


#define PAIR_SIZE(TYPE_STR_PAIR) (sizeof(TYPE_STR_PAIR) / sizeof(TYPE_STR_PAIR[0]))

typedef enum {
    BUTTON_IDLE,
    BUTTON_PRESS_ARMED,
    BUTTON_PRESS_DETECTED,
    BUTTON_PRESSED,
    BUTTON_RELEASE_DETECTED,
} button_state_t;


typedef enum {
    BUTTON_ON_CONTROL,
    BUTTON_OFF_CONTROL,
    BUTTON_ONOFF_TOGGLE_CONTROL,
    BUTTON_LEVEL_UP_CONTROL,
    BUTTON_LEVEL_DOWN_CONTROL,
    BUTTON_LEVEL_CYCLE_CONTROL,
    BUTTON_COLOR_CONTROL,
} button_func_t;


typedef struct {  
  int           id;   // a user defined identifier
  gpio_num_t    gpio;
  int           active_level; // gpio level when pressed: 0 or 1. 
} button_info_t;

// The application must provide project_buttons[] and project_button_handler()
// and should also define BUTTON_COUNT in project.h

extern const button_info_t project_buttons[BUTTON_COUNT] ;

extern void project_button_handler(const button_info_t *button, int duration_ms);

//
// Initialize the button driver
//
bool button_driver_init();

#ifdef __cplusplus
} // extern "C"
#endif
