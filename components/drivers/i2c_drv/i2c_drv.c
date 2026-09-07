/**
 * @file i2c_drv.c
 * @brief I2C驱动实现
 * 
 * 实现I2C总线的初始化、配置和数据传输功能。
 * 硬件参数从设备树读取，不在代码中硬编码。
 * 使用ESP-IDF I2C Master API。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#include "i2c_drv.h"
#include "logger.h"

#include "driver/i2c_master.h"
#include "esp_err.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static const char* TAG = "[I2C_DRV]";

/* ========================================================================== */
/*                              内部数据结构                                   */
/* ========================================================================== */

/**
 * @brief I2C设备句柄
 */
typedef struct i2c_dev_node {
    uint8_t addr;                       /**< 设备地址 */
    i2c_master_dev_handle_t dev_handle; /**< ESP-IDF设备句柄 */
    struct i2c_dev_node* next;          /**< 下一个设备 */
} i2c_dev_node_t;

/**
 * @brief I2C驱动句柄结构
 */
struct i2c_drv_handle {
    i2c_master_bus_handle_t bus_handle;  /**< I2C主机总线句柄 */
    i2c_drv_config_t config;             /**< 配置信息 */
    bool initialized;                    /**< 初始化标志 */
    i2c_dev_node_t* dev_list;            /**< 设备链表 */
};

/* ========================================================================== */
/*                              内部函数                                       */
/* ========================================================================== */

/**
 * @brief 查找或创建设备句柄
 */
static i2c_master_dev_handle_t get_or_create_dev(struct i2c_drv_handle* h, uint8_t addr) {
    /* 先在链表中查找 */
    i2c_dev_node_t* node = h->dev_list;
    while (node) {
        if (node->addr == addr) {
            return node->dev_handle;
        }
        node = node->next;
    }
    
    /* 创建新设备 */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = h->config.freq_hz,
    };
    
    i2c_master_dev_handle_t dev_handle;
    esp_err_t ret = i2c_master_bus_add_device(h->bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to add device 0x%02X: %s", addr, esp_err_to_name(ret));
        return NULL;
    }
    
    /* 添加到链表 */
    i2c_dev_node_t* new_node = (i2c_dev_node_t*)malloc(sizeof(i2c_dev_node_t));
    if (!new_node) {
        i2c_master_bus_rm_device(dev_handle);
        return NULL;
    }
    new_node->addr = addr;
    new_node->dev_handle = dev_handle;
    new_node->next = h->dev_list;
    h->dev_list = new_node;
    
    LOGD(TAG, "Added device 0x%02X", addr);
    
    return dev_handle;
}

/* ========================================================================== */
/*                              公共API实现                                    */
/* ========================================================================== */

i2c_drv_err_t i2c_drv_load_config(i2c_drv_config_t* config) {
    if (!config) {
        return I2C_DRV_ERR_PARAM;
    }
    
    dtree_err_t err;
    
    err = DTREE_INT("i2c.bus", "sda_pin", &config->sda_pin);
    if (DTREE_CHECK_ERROR("Read sda_pin", err)) {
        return I2C_DRV_ERR_CONFIG;
    }
    
    err = DTREE_INT("i2c.bus", "scl_pin", &config->scl_pin);
    if (DTREE_CHECK_ERROR("Read scl_pin", err)) {
        return I2C_DRV_ERR_CONFIG;
    }
    
    err = DTREE_INT("i2c.bus", "freq_hz", &config->freq_hz);
    if (DTREE_CHECK_ERROR("Read freq_hz", err)) {
        return I2C_DRV_ERR_CONFIG;
    }
    
    err = DTREE_BOOL("i2c.bus", "pullup", &config->pullup);
    if (DTREE_CHECK_ERROR("Read pullup", err)) {
        return I2C_DRV_ERR_CONFIG;
    }
    
    err = DTREE_INT("i2c.bus", "pullup_resistor_ohm", &config->pullup_resistor_ohm);
    if (DTREE_CHECK_ERROR("Read pullup_resistor_ohm", err)) {
        return I2C_DRV_ERR_CONFIG;
    }
    
    LOGI(TAG, "I2C config loaded: sda=%" PRId32 ", scl=%" PRId32 
         ", freq=%" PRId32 ", pullup=%d, resistor=%" PRId32,
         config->sda_pin, config->scl_pin, config->freq_hz, 
         config->pullup, config->pullup_resistor_ohm);
    
    return I2C_DRV_OK;
}

i2c_drv_err_t i2c_drv_init(const i2c_drv_config_t* config, i2c_drv_handle_t* handle) {
    if (!config || !handle) {
        return I2C_DRV_ERR_PARAM;
    }
    
    LOGI(TAG, "Initializing I2C driver...");
    LOGI(TAG, "  SDA pin: %" PRId32, config->sda_pin);
    LOGI(TAG, "  SCL pin: %" PRId32, config->scl_pin);
    LOGI(TAG, "  Freq: %" PRId32 " Hz", config->freq_hz);
    
    /* 分配句柄 */
    struct i2c_drv_handle* h = (struct i2c_drv_handle*)malloc(sizeof(struct i2c_drv_handle));
    if (!h) {
        LOGE(TAG, "Failed to allocate handle");
        return I2C_DRV_ERR_HW;
    }
    memset(h, 0, sizeof(struct i2c_drv_handle));
    
    /* 配置I2C主机总线 */
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = (int)config->scl_pin,
        .sda_io_num = (int)config->sda_pin,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = config->pullup,
    };
    
    esp_err_t ret = i2c_new_master_bus(&bus_config, &h->bus_handle);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to create I2C master bus: %s", esp_err_to_name(ret));
        free(h);
        return I2C_DRV_ERR_HW;
    }
    
    /* 保存配置 */
    h->config = *config;
    h->initialized = true;
    h->dev_list = NULL;
    
    *handle = h;
    
    LOGI(TAG, "I2C driver initialized successfully");
    
    return I2C_DRV_OK;
}

i2c_drv_err_t i2c_drv_deinit(i2c_drv_handle_t handle) {
    if (!handle) {
        return I2C_DRV_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return I2C_DRV_ERR_NOT_INIT;
    }
    
    LOGI(TAG, "Deinitializing I2C driver...");
    
    /* 释放设备链表 */
    i2c_dev_node_t* node = handle->dev_list;
    while (node) {
        i2c_dev_node_t* next = node->next;
        i2c_master_bus_rm_device(node->dev_handle);
        free(node);
        node = next;
    }
    
    /* 删除I2C主机总线 */
    if (handle->bus_handle) {
        i2c_del_master_bus(handle->bus_handle);
        handle->bus_handle = NULL;
    }
    
    handle->initialized = false;
    free(handle);
    
    LOGI(TAG, "I2C driver deinitialized");
    
    return I2C_DRV_OK;
}

i2c_drv_err_t i2c_drv_write(i2c_drv_handle_t handle, uint8_t device_addr, 
                             const void* data, size_t size) {
    if (!handle || !data) {
        return I2C_DRV_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return I2C_DRV_ERR_NOT_INIT;
    }
    
    if (size == 0) {
        return I2C_DRV_OK;
    }
    
    /* 获取或创建设备句柄 */
    i2c_master_dev_handle_t dev = get_or_create_dev(handle, device_addr);
    if (!dev) {
        return I2C_DRV_ERR_HW;
    }
    
    /* 使用ESP-IDF I2C主机API写入数据 */
    esp_err_t ret = i2c_master_transmit(dev, (uint8_t*)data, size, 1000);
    
    if (ret != ESP_OK) {
        LOGE(TAG, "I2C write failed: addr=0x%02X, size=%zu, err=%s",
             device_addr, size, esp_err_to_name(ret));
        return I2C_DRV_ERR_HW;
    }
    
    return I2C_DRV_OK;
}

i2c_drv_err_t i2c_drv_read(i2c_drv_handle_t handle, uint8_t device_addr, 
                            void* buffer, size_t size, size_t* bytes_read) {
    if (!handle || !buffer || !bytes_read) {
        return I2C_DRV_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return I2C_DRV_ERR_NOT_INIT;
    }
    
    if (size == 0) {
        *bytes_read = 0;
        return I2C_DRV_OK;
    }
    
    /* 获取或创建设备句柄 */
    i2c_master_dev_handle_t dev = get_or_create_dev(handle, device_addr);
    if (!dev) {
        *bytes_read = 0;
        return I2C_DRV_ERR_HW;
    }
    
    /* 使用ESP-IDF I2C主机API读取数据 */
    esp_err_t ret = i2c_master_receive(dev, (uint8_t*)buffer, size, 1000);
    
    if (ret != ESP_OK) {
        LOGE(TAG, "I2C read failed: addr=0x%02X, size=%zu, err=%s",
             device_addr, size, esp_err_to_name(ret));
        *bytes_read = 0;
        return I2C_DRV_ERR_HW;
    }
    
    *bytes_read = size;
    
    return I2C_DRV_OK;
}

i2c_drv_err_t i2c_drv_write_reg(i2c_drv_handle_t handle, uint8_t device_addr, 
                                 uint8_t reg_addr, const void* data, size_t size) {
    if (!handle || !data) {
        return I2C_DRV_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return I2C_DRV_ERR_NOT_INIT;
    }
    
    /* 构造写入缓冲区: 寄存器地址 + 数据 */
    size_t total_size = 1 + size;
    uint8_t* buf = (uint8_t*)malloc(total_size);
    if (!buf) {
        LOGE(TAG, "Failed to allocate write buffer");
        return I2C_DRV_ERR_HW;
    }
    
    buf[0] = reg_addr;
    memcpy(buf + 1, data, size);
    
    /* 写入数据 */
    i2c_drv_err_t ret = i2c_drv_write(handle, device_addr, buf, total_size);
    
    free(buf);
    
    return ret;
}

i2c_drv_err_t i2c_drv_read_reg(i2c_drv_handle_t handle, uint8_t device_addr, 
                                uint8_t reg_addr, void* buffer, size_t size, 
                                size_t* bytes_read) {
    if (!handle || !buffer || !bytes_read) {
        return I2C_DRV_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return I2C_DRV_ERR_NOT_INIT;
    }
    
    /* 获取或创建设备句柄 */
    i2c_master_dev_handle_t dev = get_or_create_dev(handle, device_addr);
    if (!dev) {
        *bytes_read = 0;
        return I2C_DRV_ERR_HW;
    }
    
    /* 使用ESP-IDF I2C主机API写入寄存器地址再读取数据 */
    esp_err_t ret = i2c_master_transmit_receive(dev, &reg_addr, 1, 
                                                 (uint8_t*)buffer, size, 1000);
    
    if (ret != ESP_OK) {
        LOGE(TAG, "I2C read_reg failed: addr=0x%02X, reg=0x%02X, err=%s",
             device_addr, reg_addr, esp_err_to_name(ret));
        *bytes_read = 0;
        return I2C_DRV_ERR_HW;
    }
    
    *bytes_read = size;
    
    return I2C_DRV_OK;
}
