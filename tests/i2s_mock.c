/**
 * @file i2s_mock.c
 * @brief i2s_drv mock 实现（ovs_tests 音频门禁用）
 *
 * write=即时消费并捕获全部字节（供音量缩放断言）；read=返回静音。
 * 捕获缓冲环形复用，i2s_mock_reset 清空。
 */

#include "i2s_drv.h"
#include "logger.h"

#include <string.h>
#include <stdio.h>

struct i2s_drv_handle {
    uint32_t magic;
};
#define I2S_MOCK_MAGIC 0x4932534Du   /* "I2SM" */

#define I2S_MOCK_CAP_SIZE (1u << 17) /* 128KB 捕获缓冲 */

static struct i2s_drv_handle s_mock_handle = { I2S_MOCK_MAGIC };
static uint8_t s_capture[I2S_MOCK_CAP_SIZE];
static size_t s_capture_len = 0;
static i2s_drv_config_t s_last_config;

void i2s_mock_reset(void) {
    s_capture_len = 0;
}

size_t i2s_mock_capture_len(void) {
    return s_capture_len;
}

/** 读取已捕获字节（最多 cap，返回实读） */
size_t i2s_mock_capture_copy(uint8_t* out, size_t cap) {
    size_t n = s_capture_len < cap ? s_capture_len : cap;
    memcpy(out, s_capture, n);
    return n;
}

i2s_drv_err_t i2s_drv_load_config(dtree_node_t* bus_node, i2s_drv_config_t* config) {
    (void)bus_node;   /* mock 不解析设备树，回填固定 16k/16bit/mono */
    if (!config) {
        return I2S_DRV_ERR_PARAM;
    }
    memset(config, 0, sizeof(*config));
    config->port = 0;
    config->bclk_pin = 6;
    config->ws_pin = 5;
    config->data_in_pin = 7;
    config->data_out_pin = 15;
    config->sample_rate = 16000;
    config->data_bits = 16;
    config->is_mono = true;
    return I2S_DRV_OK;
}

i2s_drv_err_t i2s_drv_init(const i2s_drv_config_t* config, i2s_drv_handle_t* handle) {
    if (!config || !handle) {
        return I2S_DRV_ERR_PARAM;
    }
    s_last_config = *config;
    *handle = &s_mock_handle;
    printf("  [i2s_mock] init: rate=%d bits=%d mono=%d\n",
           config->sample_rate, config->data_bits, (int)config->is_mono);
    return I2S_DRV_OK;
}

i2s_drv_err_t i2s_drv_deinit(i2s_drv_handle_t handle) {
    (void)handle;
    return I2S_DRV_OK;
}

i2s_drv_err_t i2s_drv_read(i2s_drv_handle_t handle, void* buffer, size_t size, size_t* bytes_read) {
    (void)handle;
    if (!buffer || !bytes_read) {
        return I2S_DRV_ERR_PARAM;
    }
    memset(buffer, 0, size);   /* 静音 */
    *bytes_read = size;
    return I2S_DRV_OK;
}

i2s_drv_err_t i2s_drv_write(i2s_drv_handle_t handle, const void* buffer, size_t size, size_t* bytes_written) {
    (void)handle;
    if (!buffer || !bytes_written) {
        return I2S_DRV_ERR_PARAM;
    }
    if (s_capture_len + size <= I2S_MOCK_CAP_SIZE) {
        memcpy(s_capture + s_capture_len, buffer, size);
        s_capture_len += size;
    } else {
        printf("  [i2s_mock] capture overflow, dropped %zu bytes\n",
               s_capture_len + size - I2S_MOCK_CAP_SIZE);
    }
    *bytes_written = size;   /* 即时消费 */
    return I2S_DRV_OK;
}
