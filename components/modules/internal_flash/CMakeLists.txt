set(COMPONENT_NAME internal_flash)
idf_component_register(
    SRCS 
        "internal_flash_vfs.c"
    INCLUDE_DIRS "."
    REQUIRES 
        ovs_vfs
        logger
    PRIV_REQUIRES
        esp_partition
)
