/**
 * @file presenter_home.h
 * @brief 展示器层：主页 presenter（bridge 数据 → 页面注入；事件订阅）
 */

#ifndef PRESENTER_HOME_H
#define PRESENTER_HOME_H

#ifdef __cplusplus
extern "C" {
#endif

/** 注册主页并挂接事件订阅（navigator_register） */
void presenter_home_init(void);

#ifdef __cplusplus
}
#endif

#endif /* PRESENTER_HOME_H */
