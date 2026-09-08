/**
 * @file vfs_stress.h
 * @brief VFS 文件系统压力测试
 */

#ifndef VFS_STRESS_H
#define VFS_STRESS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 阻塞运行完整的文件系统压力测试套件
 *
 * 覆盖：数据完整性、随机读写、目录/文件管理、错误处理、
 * 容量打满(ENOSPC)、碎片化、多任务并发、性能统计。
 * 逐项记录通过/失败，结束后打印总结（失败项明细）。
 *
 * @return 失败的检查项数量（0 = 全部通过）
 */
int vfs_stress_run(void);

#ifdef __cplusplus
}
#endif

#endif /* VFS_STRESS_H */
