/**
 * @file i2c_mock.h
 * @brief mock i2c 驱动测试控制接口（test_ath30 用）
 */

#ifndef I2C_MOCK_H
#define I2C_MOCK_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    I2C_MOCK_MODE_NORMAL = 0,   /* 正常返回合法 7 字节（50%RH / 25°C） */
    I2C_MOCK_MODE_NO_ACK,       /* 读/写返回 HW 错误（模拟无应答超时） */
    I2C_MOCK_MODE_BAD_CRC,      /* 数据正确但 CRC 字节破坏 */
} i2c_mock_mode_t;

void i2c_mock_set_mode(i2c_mock_mode_t mode);
i2c_mock_mode_t i2c_mock_get_mode(void);

/* 统计：触发测量命令（0xAC）收到次数（停采验证用） */
int i2c_mock_trigger_count(void);
void i2c_mock_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* I2C_MOCK_H */
