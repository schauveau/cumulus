
#include <stdio.h>
#include <string.h>

#include <stdint.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "esp_log.h"

#include "driver/gpio.h"
#include "driver/gptimer.h"

#include "acr.h"

// Notations:
// 
//    - The term cycle means the half-period of the AC current. So there are 100 cycles per seconds in AC 50hz.
//    - Each cycle can be either ON (1) or OFF (0).
//    - Each cycle also has a sign that alternates between +1 and -1.
//         -> Our sign is not not really synchronized with actual sign or direction of the AC current (i.e. positive and negative voltage). 
//    - The variance is the accumulation of the signs when the cycles are ON.
//         -> The variance must be kept to a small value to avoid accumulations of charges that could trip the circuit breaker..
//


// acr_counter_t must be an unsigned integer type large enough to hold ACR_MAX_FRAME_SIZE
#if ACR_MAX_FRAME_SIZE <= UINT8_MAX 
typedef uint8_t acr_count_t;
#else
typedef uint16_t acr_count_t;  
#endif

ESP_STATIC_ASSERT( ACR_PREFER==0 || ACR_PREFER==1 , "ACR_PREFER must be 0 or 1");

// The number of ON cycles obtained during the LAST frame.

#define ACR_MAX_VARIANCE 5

typedef struct {
  portMUX_TYPE mutex;  // variables with prefix p_ shall be protected with this mutex
  unsigned p_frame_size; // Number of cycles in a frame
  acr_count_t p_frame_on_target; // Number of cycles that should be ON during a frame
  int gpio;
  double target_ratio; // The requested target ratio
  acr_count_t on_count[ACR_MAX_FRAME_SIZE];
  int index;     // current position in .on_count[]
  int sign;      // Will oscilate between +1 and -1 
  int variance;  // Used to equilibrate the number of positive and negative ON phases
  int last_frame_on_count; // number of ON cycles (written by interrupt)
} acr_state_t ;

static acr_state_t acr_state =
    {
      .mutex = portMUX_INITIALIZER_UNLOCKED,
      .p_frame_size = ACR_DEFAULT_FRAME_SIZE,
      .p_frame_on_target = 0,
      .target_ratio = 0, 
      .on_count = {0}, // Important! The initial on_count must be all 0 
      .index = 0, 
      .sign = +1,
      .variance = 0,
      .last_frame_on_count=0, 
    };


// acr_on_count is a cyclic buffer that provides a recent history of the ON/OFF status 
// by increasing a counter by 1 for each ON cycle.
//
// acr_last is the last index that was written (it is decremented each cycle)
//
// For example, acr_on_count may change as follow (the * indicates acr_last)
//
//  - initial content
//     [ 41, 41, 41, 40, 39, 39, 39, 38, ... , 24, 24, 23,*48, 47, 47, 46, 46, 46, 46, 45, 44, 44, 43, 42 ]
//  - after a OFF cycle                                      
//     [ 41, 41, 41, 40, 39, 39, 39, 38, ... , 24, 24,*48, 48, 47, 47, 46, 46, 46, 46, 45, 44, 44, 43, 42 ]
//  - after a ON cycle                                                    
//     [ 41, 41, 41, 40, 39, 39, 39, 38, ... , 24,*49, 48, 48, 47, 47, 46, 46, 46, 46, 45, 44, 44, 43, 42 ]
//
// The number of ON cycles during the last N cycles can be obtained by subtracting the value at acr_last+N 
// from the value at acr_last.   
//   
// Remark:
//   Overflows in acr_count_t is not a problem as long as the proper unsigned arithmetic is used.
//   For example     
//      (uint8_t) (0x03u - 0xF3u) = (uint8_t) 0xFFFFFF10u = 0x10u = 16
//  
//

static bool IRAM_ATTR on_ac_cycle_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
  acr_state_t *S = (acr_state_t*) user_data;
  
  // TODO: ESP32 supports 32bit atomics. That could be a cheaper alternative to the mutex.
  taskENTER_CRITICAL_ISR(&S->mutex);
  unsigned frame_size          = S->p_frame_size;
  acr_count_t frame_on_target  = S->p_frame_on_target;
  taskEXIT_CRITICAL_ISR(&S->mutex);

  acr_count_t *on_count = S->on_count ;
  int sign = S->sign ;

  // Those are cyclic indices in acr_on_count[] so 0..ACR_MAX_FRAME_SIZE-1
  int previous = S->index;
  int index  = (previous+ACR_MAX_FRAME_SIZE-1) % ACR_MAX_FRAME_SIZE;  // current index 
  int before = (index+frame_size) % ACR_MAX_FRAME_SIZE;  // index from frame_size cycles ago 

  // This is the number of ON cycles during the last frame_size-1 cycles.
  // Warning: Converting to ac_count_t is really needed for overflow correction. DO NOT REMOVE 
  acr_count_t frame_on_count =  (acr_count_t) ( on_count[previous] - on_count[before] ) ;

  int state = ( frame_on_count < frame_on_target ) ? 1 : 0 ;

  // That may not be obvious but variance detection can only prevent some ON->OFF
  // or some OFF->ON transitions and so will cause a slight reduction of either OFF
  // or ON cycles.
  // Consequently, we apply variance detection to the state that we do not prefer.
  if (state!=ACR_PREFER) {
    int new_variance = S->variance + sign;
    if ( (-ACR_MAX_VARIANCE <= new_variance) && (new_variance <= ACR_MAX_VARIANCE) ) {
      S->variance = new_variance;
    } else {
      // Variance would go out of bounds so do the opposite
      state = !state;
    }
  }

  on_count[index] = on_count[previous] + state;
  
  gpio_set_level(S->gpio, state);

  S->last_frame_on_count = frame_on_count + state ;

  S->sign = -sign ; 
  S->index = index ; 
 
  return false;
}


void acr_start(int freq, int gpio_num) {

  acr_state.gpio = gpio_num ;

  // Setup the timer ///////////
  
  gptimer_handle_t gptimer = NULL;
  gptimer_config_t timer_config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT,
    .direction = GPTIMER_COUNT_UP,
    .intr_priority = 0,
    .resolution_hz = 1000*1000,  // 1Mhz 
  };
  ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

  // Setup the alarm at twice the AC frequency 
  // 
  // TODO: add or subtract 1 to the alarm count to be very slightly desynchronized with AC?
  gptimer_alarm_config_t alarm_config = {
    .reload_count = 0,
    .alarm_count = timer_config.resolution_hz / (2*freq),  
    .flags.auto_reload_on_alarm = true,
  };
  gptimer_event_callbacks_t cbs = {
    .on_alarm = on_ac_cycle_cb,
  };
  ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, &acr_state ));
  ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));
  ESP_ERROR_CHECK(gptimer_enable(gptimer));
  ESP_ERROR_CHECK(gptimer_start(gptimer));
}


//
// Compute the number of ON frames required to obtain or approximate
// the specified ratio for the given frame size
//
static int compute_frame_on_target(double ratio, int frame_size) {

  int n = ACR_PREFER ? ceil(frame_size*ratio) : floor(frame_size*ratio) ;

  // Make sure that we are within bounds.
  // That should not be needed but I am paranoid when using floating points. 
  if (n<0)
    n=0; 
  else if (n>acr_state.p_frame_size)
    n=acr_state.p_frame_size;

  return n ;
}

double acr_set_target_ratio(double ratio) {

  if (isnan(ratio))
    ratio = 0.0 ;
  else if (ratio < 0.0)
    ratio = 0.0 ;
  else if (ratio> 1.0)
    ratio = 1.0 ;

  int frame_on_target = compute_frame_on_target(ratio, acr_state.p_frame_size);
    
  acr_state.target_ratio = ratio ;
  
  taskENTER_CRITICAL(&acr_state.mutex);
  acr_state.p_frame_on_target = frame_on_target ; 
  taskEXIT_CRITICAL(&acr_state.mutex);

  return acr_state.target_ratio ;
}

double acr_get_target_ratio(void) {
  return acr_state.target_ratio;
}

double acr_get_achievable_ratio() {
  return ((double)(acr_state.p_frame_on_target)) / acr_state.p_frame_size; 
}

double acr_get_last_achieved_ratio() {
  return ((double)(acr_state.last_frame_on_count)) / acr_state.p_frame_size; 
}

int acr_set_frame_size(int frame_size) {

  if (frame_size<ACR_MIN_FRAME_SIZE) {
    frame_size = ACR_MIN_FRAME_SIZE;
  } else if (frame_size>ACR_MAX_FRAME_SIZE) {
    frame_size = ACR_MAX_FRAME_SIZE;
  }

  int frame_on_target = compute_frame_on_target(acr_state.target_ratio, frame_size) ;

  taskENTER_CRITICAL(&acr_state.mutex);
  acr_state.p_frame_size      = frame_size;
  acr_state.p_frame_on_target = frame_on_target; 
  taskEXIT_CRITICAL(&acr_state.mutex);
  
  return frame_size; 
}

int acr_get_frame_size() {
  return acr_state.p_frame_size ; 
}

