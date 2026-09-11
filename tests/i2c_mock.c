/**
 * @file i2c_mock.c
 * @brief mock i2c 驱动实现（test_ath30 三场景：正常 / 无应答 / CRC 错误）
 *
 * 只覆盖 ath30 路径用到的 5 个 API；NORMAL 模式返回 ath30 帧格式的
 * 合法 7 字节（状态 0x08 + 湿度 20bit=50%RH + 温度 20bit=25°C + CRC8）。
 */

#include "i2c_drv.h"
#include "i2c_mock.h"

#include <string.h>

static i2c_mock_mode_t s_mode = I2C_MOCK_MODE_NORMAL;
static int s_trigger_count = 0;

void i2c_mock_set_mode(i2c_mock_mode_t mode) { s_mode = mode; }
i2c_mock_mode_t i2c_mock_get_mode(void) { return s_mode; }
int i2c_mock_trigger_count(void) { return s_trigger_count; }
void i2c_mock_reset_stats(void) { s_trigger_count = 0; }

/* 与 ath30.c 相同的 CRC8-ATMUA(SHT) 算法（poly 0x31, init 0xFF） */
static uint8_t mock_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

i2c_drv_err_t i2c_drv_load_config(dtree_node_t* bus_node, i2c_drv_config_t* config) {
    (void)bus_node;
    if (!config) {
        return I2C_DRV_ERR_PARAM;
    }
    memset(config, 0, sizeof(*config));
    return I2C_DRV_OK;
}

i2c_drv_err_t i2c_drv_init(const i2c_drv_config_t* config, i2c_drv_handle_t* handle) {
    (void)config;
    if (!handle) {
        return I2C_DRV_ERR_PARAM;
    }
    *handle = (i2c_drv_handle_t)1;   /* 非 NULL 假句柄 */
    return I2C_DRV_OK;
}

i2c_drv_err_t i2c_drv_deinit(i2c_drv_handle_t handle) {
    (void)handle;
    return I2C_DRV_OK;
}

i2c_drv_err_t i2c_drv_write(i2c_drv_handle_t handle, uint8_t device_addr,
                            const void* data, size_t size) {
    (void)device_addr;
    if (!handle || !data) {
        return I2C_DRV_ERR_PARAM;
    }
    const uint8_t* cmd = (const uint8_t*)data;
    if (size >= 1 && cmd[0] == 0xAC) {
        s_trigger_count++;
    }
    if (s_mode == I2C_MOCK_MODE_NO_ACK) {
        return I2C_DRV_ERR_HW;
    }
    return I2C_DRV_OK;
}

i2c_drv_err_t i2c_drv_read(i2c_drv_handle_t handle, uint8_t device_addr,
                           void* buffer, size_t size, size_t* bytes_read) {
    (void)device_addr;
    if (!handle || !buffer || !bytes_read) {
        return I2C_DRV_ERR_PARAM;
    }
    if (s_mode == I2C_MOCK_MODE_NO_ACK) {
        return I2C_DRV_ERR_TIMEOUT;
    }
    if (size < 7) {
        return I2C_DRV_ERR_PARAM;
    }

    uint8_t* buf = (uint8_t*)buffer;
    /* 50%RH: raw=0x80000 → {0x80,0x00,0x00}；25°C: raw=0x60000 → 低半
     * 字节进 buf[3] 高 nibble 位段 */
    buf[0] = 0x08;   /* 状态：非 BUSY、已校准 */
    buf[1] = 0x80;
    buf[2] = 0x00;
    buf[3] = 0x06;   /* 湿度低 4 位=0 | 温度高 4 位=0x6 */
    buf[4] = 0x00;
    buf[5] = 0x00;
    buf[6] = mock_crc8(buf, 6);
    if (s_mode == I2C_MOCK_MODE_BAD_CRC) {
        buf[6] = (uint8_t)(buf[6] ^ 0xFF);
    }

    *bytes_read = 7;
    return I2C_DRV_OK;
}
