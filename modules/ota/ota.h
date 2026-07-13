#ifndef OTA_H
#define OTA_H

#include "esp_err.h"

/**
 * @brief OTA 块大小（每块 16KB）
 */
#define OTA_CHUNK_SIZE (16 * 1024)

/**
 * @brief OTA 队列深度
 */
#define OTA_QUEUE_DEPTH 128

/**
 * @brief OTA 魔术数（用于固件校验）
 * 固件末尾会附加此魔术数 + SHA256 哈希
 */
#define OTA_MAGIC 0x4F56414F  // "OVAO"

/**
 * @brief OTA 校验信息结构（附加在固件末尾）
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;             // OTA_MAGIC
    uint8_t  sha256[32];        // 固件 SHA256 哈希
    uint32_t firmware_size;     // 固件原始大小（不含校验尾）
} ota_verify_t;

/**
 * @brief 初始化 OTA 模块
 * 
 * 创建 OTA 写入队列和后台写入任务。
 * 在系统初始化时调用一次。
 */
void ota_init(void);

/**
 * @brief 开始 OTA 升级
 * 
 * 准备备用 OTA 分区，开始接收固件数据。
 * 必须在推送数据前调用。
 * 
 * @param total_size 固件总大小（字节），用于进度计算，0 表示未知
 * @param resume     是否续传。若为 true 且已有未完成的 OTA，则继续写入
 * @return esp_err_t
 */
esp_err_t ota_start(size_t total_size, bool resume);

/**
 * @brief 推送一块固件数据到 OTA 队列
 * 
 * 数据会从 PSRAM 动态分配拷贝，放入队列等待后台写入。
 * 调用者可以立即释放原始数据。
 * 
 * @param data 固件数据指针
 * @param len  数据长度（不能超过 OTA_CHUNK_SIZE）
 * @return esp_err_t
 */
esp_err_t ota_push_data(const char *data, size_t len);

/**
 * @brief 结束 OTA 升级
 * 
 * 等待队列中所有数据写入完成，校验固件（魔术数 + SHA256），
 * 设置启动分区，重启设备。
 */
void ota_finish(void);

/**
 * @brief 取消 OTA 升级
 * 
 * 放弃当前 OTA 写入，清理资源。
 */
void ota_abort(void);

/**
 * @brief 获取当前 OTA 进度（已写入的字节数）
 * @return 已写入的字节数，如果没有 OTA 在进行则返回 0
 */
size_t ota_get_progress(void);

/**
 * @brief 获取当前 OTA 的总大小
 * @return 总大小，0 表示未知
 */
size_t ota_get_total_size(void);

/**
 * @brief 获取当前运行的 OTA 分区编号
 * @return 0 或 1，-1 失败
 */
int ota_get_current_slot(void);

/**
 * @brief 获取备用（待升级）的 OTA 分区编号
 * @return 0 或 1，-1 失败
 */
int ota_get_next_slot(void);

#endif // OTA_H
