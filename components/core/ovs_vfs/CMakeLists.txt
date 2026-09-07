set(COMPONENT_NAME ovs_vfs)
idf_component_register(
    SRCS 
        "src/vfs.c"
        "src/vfs_block_dev.c"
        "src/vfs_littlefs_adapter.c"
    INCLUDE_DIRS 
        "include"
    PRIV_REQUIRES
        esp_littlefs
        esp_blockdev
        logger
        vfs
)
