
#include "led_strip.h"

#include <esp_log.h>

#include "rgb_led.h"


// The led on my espc6-zero is using RGB instead of GRB
// so I need to swap Red and Green components
// There is a patch ready in https://github.com/espressif/idf-extra-components/issues/341
#define LED_FORMAT_IS_REALLY_RGB

static const char TAG[]="rgb_led";

static led_strip_handle_t led_strip;

void led_init(void)
{
  ESP_LOGI(TAG, "Configure the RGB led");
  /* LED strip initialization with the GPIO and pixels number*/
  led_strip_config_t strip_config = {
    .strip_gpio_num   = CONFIG_RGB_GPIO,
    .led_model        = LED_MODEL_WS2812,
    .led_pixel_format = LED_PIXEL_FORMAT_GRB, // should be LED_PIXEL_FORMAT_RGB    
    .max_leds         = 1, 
  };
  led_strip_rmt_config_t rmt_config = {
    .resolution_hz = 10 * 1000 * 1000, // 10MHz is the default
    .flags.with_dma = false,
  };
  ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
  led_strip_clear(led_strip);
}

void led_set_rgb(uint32_t rgb)
{
  uint8_t r = rgb>>16;
  uint8_t g = rgb>>8; 
  uint8_t b = rgb>>0; 
  ESP_LOGI(TAG, "Set RGB led r=%u g=%u b=%u", r,g,b);
#ifdef LED_FORMAT_IS_REALLY_RGB
  uint8_t tmp = r ; r=g ; g=tmp ; // swap RED and GREEN
#endif
  led_strip_set_pixel(led_strip,0,r,g,b);
  led_strip_refresh(led_strip);
}

void led_set_hsv(uint16_t h, uint8_t s, uint8_t v)
{
  h=h%360 ; 
#ifdef LED_FORMAT_IS_REALLY_RGB
  // So red (H=0) and green (H=120) are inverted but blue (H=240) is fine.
  h = (480-h)%360 ; // That should do the trick
#endif
  
  led_strip_set_pixel_hsv(led_strip,0,h,s,v);
  led_strip_refresh(led_strip);
}
