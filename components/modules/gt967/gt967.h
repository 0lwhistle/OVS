/**
 * @file gt967.h
 * @brief GT967 单层 5 点电容触控模块（GT9xx 系，I2C）
 *
 * 真机适配（2026-09-10）：底板触控芯片为 GT967（汇顶 GT9xx 系），
 * 取代原 cst816s 组件。16 位寄存器地址（0x814E 等），经
 * "写 2 字节寄存器指针 + 读" 访问；I2C 地址经 RST/INT 复位时序
 * 选定 0x5D（备选 0x14），设备树 gt967-touch 节点配置。
 * 周期轮询（tasker）读取触点并发布 EVENT_TOUCH_* 事件。
 */

#ifndef GT967_H
#define GT967_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 错误码 ========== */
typedef enum {
    GT967_OK = 0,
    GT967_ERR_NOT_INIT = -1,
    GT967_ERR_PARAM = -2,
    GT967_ERR_I2C = -3,
    GT967_ERR_NO_TOUCH = -4,
} gt967_err_t;

/* ========== 类型定义 ========== */

/**
 * @brief 触点数据
 */
typedef struct {
    uint16_t x;                 /**< X坐标 */
    uint16_t y;                 /**< Y坐标 */
    uint8_t  track_id;          /**< 触点跟踪 ID（多点区分用） */
    uint8_t  area;              /**< 触点面积（低 8 位） */
} gt967_point_t;

/**
 * @brief 触控帧数据（最多 5 点）
 */
typedef struct {
    gt967_point_t points[5];    /**< 触点数组 */
    uint8_t point_num;          /**< 有效触点数 */
    uint32_t timestamp;         /**< 时间戳 (ms) */
} gt967_touch_t;

/* ========== 公共 API ========== */

/**
 * @brief 初始化GT967触控
 *
 * 按 compatible="gt967-touch" 定位设备节点，父节点为所属 I2C 总线；
 * 复位时序选定 I2C 地址并校验通信（读产品 ID）。
 *
 * @return GT967_OK 成功
 * @return GT967_ERR_I2C I2C 通信失败
 */
gt967_err_t gt967_init(void);

/** 反初始化 */
gt967_err_t gt967_deinit(void);

/**
 * @brief 读取当前触点（轮询一次）
 * @return GT967_OK 有触点，touch 有效
 * @return GT967_ERR_NO_TOUCH 无触摸
 * @return GT967_ERR_I2C I2C错误
 */
gt967_err_t gt967_read(gt967_touch_t* touch);

/** 是否已初始化 */
bool gt967_is_initialized(void);

/**
 * @brief 启动周期轮询（tasker，事件发布）
 * @param interval_ms 轮询间隔 (ms)，0 用缺省 20
 */
gt967_err_t gt967_start_periodic_read(uint32_t interval_ms);

/** 停止周期轮询 */
gt967_err_t gt967_stop_periodic_read(void);

#ifdef __cplusplus
}
#endif

#endif /* GT967_H */
