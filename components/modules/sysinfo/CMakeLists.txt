# 契约 I3 系统/网络信息只读查询（[HUB]）
# 平台移植层：ESP=sysinfo_port_esp.c（esp_netif）；PC 测试=sysinfo_port.c（mock）
idf_component_register(
    SRCS "sysinfo.c" "sysinfo_port_esp.c"
    INCLUDE_DIRS "."
    REQUIRES net_mgr logger
    PRIV_REQUIRES esp_wifi esp_netif esp_app_format esp_timer
)
