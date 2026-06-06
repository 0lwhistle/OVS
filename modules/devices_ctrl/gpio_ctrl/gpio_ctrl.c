#include "gpio_ctrl.h"

static const char *TAG = "[GPIO_CTRL]";

uint8_t gpio_init_vec[GPIO_NUM] = {0};
uint8_t gpio_cur_val_vec[GPIO_NUM] = {0};
uint8_t gpio_reset_val_vec[GPIO_NUM] = {0};

void gpio_ctrl_init(uint8_t gpio_num, uint8_t gpio_val, gpio_config_t io_conf){

	if (gpio_init_vec[gpio_num]){
		ESP_LOGW(TAG, "gpio %d already init", gpio_num);
		return;
	}
	gpio_config(&io_conf);
    gpio_set_level(gpio_num, gpio_val);

	gpio_init_vec[gpio_num] = 1;
	gpio_cur_val_vec[gpio_num] = gpio_val;
	gpio_reset_val_vec[gpio_num] = gpio_val;
}

int gpio_ctrl_set(uint8_t gpio_num, uint8_t gpio_val){
	if (!gpio_init_vec[gpio_num]){
		ESP_LOGW(TAG, "gpio %d not init", gpio_num);
		return GPIO_ERR_INIT;
	}

	if (gpio_set_level(gpio_num, gpio_val) == ESP_OK){
		gpio_cur_val_vec[gpio_num] = gpio_val;
		return GPIO_OK;
	}

	return GPIO_FAIL;
}

int gpio_ctrl_reset(uint8_t gpio_num){
	if (!gpio_init_vec[gpio_num]){
		ESP_LOGW(TAG, "gpio %d not init", gpio_num);
		return GPIO_ERR_INIT;
	}

	if (gpio_set_level(gpio_num, gpio_reset_val_vec[gpio_num]) == ESP_OK){
		gpio_cur_val_vec[gpio_num] = gpio_reset_val_vec[gpio_num];
		return GPIO_OK;
	}

	return GPIO_FAIL;
}


int gpio_ctrl_toggle(uint8_t gpio_num){
	if (!gpio_init_vec[gpio_num]){
		ESP_LOGW(TAG, "gpio %d not init", gpio_num);
		return GPIO_ERR_INIT;
	}

	if (gpio_set_level(gpio_num, !gpio_cur_val_vec[gpio_num]) == ESP_OK){
		gpio_cur_val_vec[gpio_num] = !gpio_cur_val_vec[gpio_num];
		return GPIO_OK;
	}

	return GPIO_FAIL;
}

uint8_t gpio_ctrl_read(uint8_t gpio_num){
	if (!gpio_init_vec[gpio_num]){
		ESP_LOGW(TAG, "gpio %d not init", gpio_num);
		return GPIO_ERR_INIT;
	}

	return gpio_get_level(gpio_num);
}
