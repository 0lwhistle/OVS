/**
 * @file audio_player_port_stub.c
 * @brief 音量持久化 PC/测试桩实现（不可持久化，load 恒失败走缺省音量）
 */

#include "audio_player_port.h"

int apl_port_volume_load(uint8_t* out_volume) {
    (void)out_volume;
    return -1;
}

int apl_port_volume_save(uint8_t volume) {
    (void)volume;
    return -1;
}
