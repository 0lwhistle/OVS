/**
 * @file audio_player_port.h
 * @brief audio_player 平台移植点：音量持久化（内部头）
 *
 * ESP 端链接 audio_player_port_esp.c（NVS "audio_player" 命名空间）；
 * PC/测试端链接 audio_player_port_stub.c（不可持久化）。
 * 平台选择由 CMake 完成，业务代码不出现条件编译。
 */

#ifndef AUDIO_PLAYER_PORT_H
#define AUDIO_PLAYER_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 读出持久化音量：0=成功，非 0=无存储/失败 */
int apl_port_volume_load(uint8_t* out_volume);

/** 持久化音量：0=成功，非 0=失败（调用方仅 LOGW 降级） */
int apl_port_volume_save(uint8_t volume);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PLAYER_PORT_H */
