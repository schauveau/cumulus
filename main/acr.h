#pragma once

#ifdef __cplusplus
extern "C" {
#endif


#define ACR_MIN_FRAME_SIZE 6
#define ACR_MAX_FRAME_SIZE 128

#define ACR_DEFAULT_FRAME_SIZE 100


// It is not always possible to produce exactly the given ratio of ON/OFF cycles.
//
// The algorithm can be tuned to favor one kind of cycles.
//
// Set to 1 if you have a preference for ON cycles.
// Set to 0 if you have a preference for OFF cycles.
//
#define ACR_PREFER 1


// Start the acr service
//
//  ac_freq is the AC frequency (typically 50 or 60)
// 
void acr_start(int ac_freq, int gpio_num);

// Set the target ratio. It is always clamped between 0.0 and 1.0.
//
// Return the target ratio that was actually set.
//
double acr_set_target_ratio(double ratio);


// Get the current target ratio as set by acr_set_target_ratio()
double acr_get_target_ratio(void) ;


// Get the achievable ratio.
//
// This is the actual ratio that can be achieved using the current
// target ratio and frame size.
//
// For example, if the frame_size is 13 and the target ratio is 0.7
// then the achievable ratio will be one of 
//    9/13 (0.6923) if ACR_PREFER==0
// or
//   10/11 (0.7692) if ACR_PREFER==1
//
double acr_get_achievable_ratio();

// Get the ratio that was achieved during the last frame. 
double acr_get_last_achieved_ratio();

// 
// Set the frame size.
//
// The provided value is clamped between ACR_MIN_FRAME_SIZE and ACR_MAX_FRAME_SIZE
//
// Return the new (clamped) frame size.
//
int acr_set_frame_size(int frame_size);


// Get the current frame size
int acr_get_frame_size(void);

void acr_dump() ;

#ifdef __cplusplus
}
#endif
