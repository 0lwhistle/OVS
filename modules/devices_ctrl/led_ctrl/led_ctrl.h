#ifndef LED_CTRL_H
#define LED_CTRL_H

#include "driver/gpio.h"
#include "esp_log.h"
#include "gpio_ctrl.h"

#define LED_NUM 34

void led_ctrl_init(uint8_t gpio_num, uint8_t gpio_val);

int led_ctrl_on(uint8_t gpio_num);

int led_ctrl_off(uint8_t gpio_num);

int led_ctrl_toggle(uint8_t gpio_num);


#endif // LED_CTRL_H
