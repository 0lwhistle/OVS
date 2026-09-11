# 契约 I4 时间服务（[HUB]）
idf_component_register(
    SRCS "time_svc.c" "time_svc_port.c"
    INCLUDE_DIRS "."
    REQUIRES logger
    PRIV_REQUIRES esp_timer
)
