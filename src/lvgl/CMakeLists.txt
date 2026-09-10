# LVGL UI 应用层组件（ESP 端）
# 六层架构: ui → widgets → pages → navigator → bridge → presenters
# 平台边界: port/ 仅 ESP 硬件对接；PC 模拟器复用六层实现，不编译
#           lvgl_app.c 与 port/（见 sim/CMakeLists.txt）
# 桥接开关: OVS_BRIDGE_HUB_REAL=1 接 HUB 真身（i18n/sensor_cache/sysinfo/
#           time_svc 交付后置 1 并补 REQUIRES）；OVS_BRIDGE_AUDIO=1 音量实调
#           audio_player（ESP 恒开，PC 模拟关闭）

set(LVGL_UI_SRCS
    # 应用入口（ESP 运行时任务）
    lvgl_app.c

    # 主题 (themes)
    themes/theme_base.c

    # 控件层 (ui/controls)
    ui/controls/ui_card.c
    ui/controls/ui_list_item.c
    ui/controls/ui_round_btn.c

    # 指示层 (ui/indicators)
    ui/indicators/ui_status_dot.c
    ui/indicators/ui_value_label.c

    # 页面层 (pages)
    pages/page_home.c
    pages/page_settings.c
    pages/page_standby.c

    # 导航层 (navigator)
    navigator/nav_stack.c
    navigator/navigator.c

    # 桥接层 (bridge，唯一可 include HUB 头/audio_player.h 的地方)
    bridge/bridge.c

    # 展示器层 (presenters)
    presenters/presenter_home.c
    presenters/presenter_settings.c
    presenters/presenter_standby.c
    presenters/ui_bootstrap.c

    # 平台移植层 (port，仅 ESP)
    port/display_port.c
)

idf_component_register(
    SRCS ${LVGL_UI_SRCS}
    INCLUDE_DIRS
        "."
        "ui"
        "ui/controls"
        "ui/indicators"
        "widgets"
        "pages"
        "navigator"
        "bridge"
        "presenters"
        "port"
        "themes"
        "fonts"
        "assets"
    REQUIRES lvgl_lib logger dtbs mem_pool event_bus tasker_api audio_player
    PRIV_REQUIRES esp_timer heap freertos
)

# mem_pool 模块标签：UI 层分配记入 LVGL 桶
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_LVGL)

# 桥接模式：ESP 端音量走 audio_player 真身；HUB 数据未交付前用 mock 常量
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    OVS_BRIDGE_AUDIO=1
    OVS_BRIDGE_HUB_REAL=0   # HUB 交付 I1~I4 后置 1（同时补 REQUIRES i18n sensor_cache sysinfo time_svc）
)
