# mem_pool 核心内存管理组件（记账分配 / 防护 / 定长块池）
# 设计文档: docs/mem_pool_overview.md
idf_component_register(
    SRCS "mem.c" "mem_pool.c"
    INCLUDE_DIRS "."
    REQUIRES logger
    PRIV_REQUIRES freertos heap
)
