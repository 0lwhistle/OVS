#include "led_ctrl.h"

void led_ctrl_init(uint8_t gpio_num, uint8_t gpio_val){
	gpio_config_t io_conf = {0};

	io_conf.intr_type = GPIO_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_OUTPUT;
	io_conf.pin_bit_mask = (1ULL << gpio_num);
	io_conf.pull_down_en = 0;
	io_conf.pull_up_en = 0;
	
	gpio_ctrl_init(gpio_num, gpio_val, io_conf);
}

int led_ctrl_on(uint8_t gpio_num){
	return gpio_ctrl_set(gpio_num, 1);
}

int led_ctrl_off(uint8_t gpio_num){
	return gpio_ctrl_set(gpio_num, 0);
}

int led_ctrl_toggle(uint8_t gpio_num){
	if (gpio_ctrl_toggle(gpio_num) < 0) {
		led_ctrl_init(gpio_num, 0);
		return gpio_ctrl_toggle(gpio_num);
	}
	return GPIO_OK;
}
