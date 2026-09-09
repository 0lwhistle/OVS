# LVGL UI 应用层组件（ESP 端）
# 六层架构: ui → widgets → pages → navigator → bridge → presenters
# 平台边界: port/ 仅 ESP 硬件对接；PC 模拟器复用 navigator/pages/bridge 头与
#           实现，不编译 lvgl_app.c 与 port/（见 sim/CMakeLists.txt）

set(LVGL_UI_SRCS
    # 应用入口（ESP 运行时任务）
    lvgl_app.c

    # 页面层 (pages)
    pages/page_home.c

    # 导航层 (navigator)
    navigator/nav.c

    # 平台移植层 (port，仅 ESP)
    port/display_port.c
    port/bridge_time_esp.c

    # 控件层 (ui)          TODO: ui/base/*.c ui/controls/*.c
    # 窗口层 (widgets)     TODO: widgets/*.c
    # 桥接接口实现         TODO: bridge_time 之外的后端桥（wifi/audio/media...）
    # 展示器层 (presenters) TODO: presenters/*.c
    # 主题                 TODO: themes/*.c
)

idf_component_register(
    SRCS ${LVGL_UI_SRCS}
    INCLUDE_DIRS
        "."
        "ui"
        "widgets"
        "pages"
        "navigator"
        "bridge"
        "presenters"
        "port"
        "themes"
        "fonts"
        "assets"
    REQUIRES lvgl_lib logger dtbs
    PRIV_REQUIRES esp_timer heap freertos
)
