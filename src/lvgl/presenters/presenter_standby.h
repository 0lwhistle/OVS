/**
 * @file presenter_standby.h
 * @brief 展示器层：待机 presenter（I6 待机画面接线）
 */

#ifndef PRESENTER_STANDBY_H
#define PRESENTER_STANDBY_H

#ifdef __cplusplus
extern "C" {
#endif

/** 启用待机（注册默认待机画面与超时） */
void presenter_standby_init(void);

#ifdef __cplusplus
}
#endif

#endif /* PRESENTER_STANDBY_H */
