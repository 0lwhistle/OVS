#ifndef GPIO_CTRL_H
#define GPIO_CTRL_H

#include "driver/gpio.h"
#include "esp_log.h"
#include "logger.h"
#define GPIO_NUM 34

enum gpio_err{
    GPIO_OK = 0,
    GPIO_FAIL = -1,
    GPIO_ERR_INIT = -2,
    GPIO_ERR_SET = -3,
    GPIO_ERR_RESET = -4,
    GPIO_ERR_TOGGLE = -5,
    GPIO_ERR_READ = -6,
};

extern uint8_t gpio_init_vec[GPIO_NUM];
extern uint8_t gpio_cur_val_vec[GPIO_NUM];
extern uint8_t gpio_reset_val_vec[GPIO_NUM];

void gpio_ctrl_init(uint8_t gpio_num, uint8_t gpio_val, gpio_config_t io_conf);

int gpio_ctrl_set(uint8_t gpio_num, uint8_t gpio_val);

int gpio_ctrl_reset(uint8_t gpio_num);

int gpio_ctrl_toggle(uint8_t gpio_num);

uint8_t gpio_ctrl_read(uint8_t gpio_num);


#endif // GPIO_CTRL_H
