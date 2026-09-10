/**
 * @file presenter_standby.c
 * @brief 展示器层：待机 presenter 实现
 *
 * 契约 I6：navigator_set_standby(cb, timeout_s)。本期注册占位画面
 * （page_standby_screen_create，60s 无输入触发）；未来聊天/语音画面
 * 只需实现自己的 screen_create_cb 注入即可，navigator/pages 不改。
 */

#include "presenter_standby.h"
#include "page_standby.h"
#include "navigator.h"

#define STANDBY_TIMEOUT_S 60

void presenter_standby_init(void) {
    navigator_set_standby(page_standby_screen_create, STANDBY_TIMEOUT_S);
}
