#pragma once

// 定义 MG_ARCH 为 ESP32
#define MG_ARCH MG_ARCH_ESP32

// 禁用 mongoose 调试日志（避免 UART 输出阻塞导致 watchdog 超时）
#ifndef MG_ENABLE_LOG
#define MG_ENABLE_LOG 0
#endif

// 增大 IO 缓冲区增长粒度，减少 mg_iobuf_resize 的 memmove 次数
// 默认 512 字节，改为 16384（16KB），memmove 次数减少 32 倍
#ifndef MG_IO_SIZE
#define MG_IO_SIZE 16384
#endif

// 限制最大接收缓冲区大小（3MB 太大，改为 1MB）
#ifndef MG_MAX_RECV_SIZE
#define MG_MAX_RECV_SIZE (1UL * 1024UL * 1024UL)
#endif
