/**
 * @file ota_sha256.h
 * @brief 自包含 SHA-256（公有领域实现，避免依赖平台加密库）
 *
 * OTA 场景只需流式摘要，无需密钥体系；内置实现保证模块
 * 在不同平台（ESP32/STM32/Linux）上零依赖可用。
 */

#ifndef OTA_SHA256_H
#define OTA_SHA256_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  block[64];
    size_t   block_len;
} ota_sha256_ctx_t;

void ota_sha256_init(ota_sha256_ctx_t *ctx);
void ota_sha256_update(ota_sha256_ctx_t *ctx,
                       const uint8_t *data, size_t len);
/* out 必须至少 32 字节 */
void ota_sha256_final(ota_sha256_ctx_t *ctx, uint8_t out[32]);

#ifdef __cplusplus
}
#endif

#endif /* OTA_SHA256_H */
