/**
 * @file event_bus_test.h
 * @brief 事件总线测试头文件
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#ifndef EVENT_BUS_TEST_H
#define EVENT_BUS_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 运行所有事件总线测试
 * 
 * 测试内容:
 * - 初始化/反初始化
 * - 事件发布
 * - 事件订阅/取消订阅
 * - 事件处理
 * - 多订阅者
 * - 统计信息
 * - 状态查询
 * - 调试函数
 */
void event_bus_test_all(void);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_BUS_TEST_H */
