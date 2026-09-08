/**
 * @file ota.h
 * @brief OTA 固件升级模块（平台无关接口）
 *
 * 设计：
 *   - 流式写入：调用方（传输层，如 web）逐块推送，内部直写备用分区，
 *     内存占用恒定（无整包缓冲），阻塞写形成自然 TCP 背压
 *   - 完整性：接收过程流式计算 SHA256（供上位机比对），
 *     结束时由 esp_ota_end 做标准 IDF 镜像校验，不依赖自定义尾部签名
 *   - 回滚保护：启用 CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 后，
 *     新固件启动 15s 内未 ota_confirm_running() 则 bootloader 自动回退旧槽；
 *     ota_init() 会自动启动该确认定时器
 *
 * 线程模型：单写者约束——ota_begin/write/end/abort 只允许
 * 同一任务（传输层任务）顺序调用；状态查询任意任务安全。
 * 平台相关调用（esp_ota_ops）隔离在本 .c 内。
 */

#ifndef OTA_H
#define OTA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 错误码（正值成功，负值失败） */
typedef enum {
    OTA_OK                = 0,
    OTA_ERR_NOT_INIT      = -1,  /**< 未调用 ota_init */
    OTA_ERR_INVALID_PARAM = -2,  /**< 参数无效 */
    OTA_ERR_INVALID_STATE = -3,  /**< 状态机不允许的操作 */
    OTA_ERR_NO_MEMORY     = -4,  /**< 内存不足 */
    OTA_ERR_FLASH         = -5,  /**< 分区写入/擦除失败 */
    OTA_ERR_IMAGE_INVALID = -6,  /**< 镜像校验失败（损坏或不完整） */
    OTA_ERR_NOT_FOUND     = -7,  /**< 找不到备用 OTA 分区 */
} ota_err_t;

/** 升级状态机 */
typedef enum {
    OTA_STATE_IDLE      = 0,  /**< 空闲 */
    OTA_STATE_RECEIVING,      /**< 接收写入中 */
    OTA_STATE_VERIFYING,      /**< 结束校验中 */
    OTA_STATE_DONE,           /**< 校验通过、已切换启动槽，等待重启 */
    OTA_STATE_FAILED,         /**< 失败（可重新 begin） */
} ota_state_t;

#define OTA_VERSION_STR_MAX 32
#define OTA_PROJECT_STR_MAX 32

/** 升级状态快照 */
typedef struct {
    ota_state_t state;
    size_t      received;        /**< 已接收字节数 */
    size_t      expected;        /**< 预期总大小（0 = 未知） */
    uint8_t     sha256[32];      /**< 本次接收流的 SHA256（RECEIVING 为中间值） */
    int         current_slot;    /**< 当前运行槽（0/1，-1 未知） */
    int         target_slot;     /**< 本次写入的备用槽（0/1，-1 无） */
    bool        pending_verify;  /**< 回滚保护：当前固件待确认 */
    char        version[OTA_VERSION_STR_MAX];   /**< 运行中固件版本（app_desc） */
    char        project[OTA_PROJECT_STR_MAX];   /**< 工程名 */
} ota_status_t;

/**
 * @brief 初始化（幂等）。同时启动 15s 一次性的回滚确认定时器。
 */
ota_err_t ota_init(void);

/**
 * @brief 开始一次升级：定位备用分区、擦除、初始化 SHA256
 * @param expected_size 预期固件大小（0 = 未知，仅影响进度显示）
 */
ota_err_t ota_begin(size_t expected_size);

/**
 * @brief 流式写入一块数据（允许任意块大小；内部阻塞写形成背压）
 */
ota_err_t ota_write(const void *data, size_t len);

/**
 * @brief 结束接收：完成 SHA256、esp_ota_end 镜像校验、切换启动槽。
 * 成功后设备处于 OTA_STATE_DONE，由调用方回复响应后调 ota_reboot()。
 */
ota_err_t ota_end(void);

/**
 * @brief 中止本次升级并释放资源
 */
ota_err_t ota_abort(void);

/**
 * @brief 读取状态快照（任意任务可调用）
 */
ota_err_t ota_get_status(ota_status_t *out);

/**
 * @brief 升级完成后的受控重启（延时 500ms 保证响应发出）
 */
void ota_reboot(void);

/**
 * @brief 标记当前运行的固件有效（取消回滚保护待确认状态）
 */
void ota_confirm_running(void);

const char *ota_err_to_str(ota_err_t err);
const char *ota_state_to_str(ota_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* OTA_H */
