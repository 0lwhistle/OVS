# LVGL UI 应用层组件
# 六层架构: ui → widgets → pages → navigator → bridge → presenters

set(LVGL_UI_SRCS
    # 应用入口
    lvgl_app.c
    
    # 控件层 (ui)
    # TODO: ui/base/*.c
    # TODO: ui/controls/*.c
    # TODO: ui/indicators/*.c
    # TODO: ui/containers/*.c
    
    # 窗口层 (widgets)
    # TODO: widgets/dialog/*.c
    # TODO: widgets/input/*.c
    # TODO: widgets/keyboard/*.c
    # TODO: widgets/panel/*.c
    
    # 页面层 (pages)
    # TODO: pages/home/*.c
    # TODO: pages/settings/*.c
    # TODO: pages/lora/*.c
    # TODO: pages/audio/*.c
    # TODO: pages/about/*.c
    
    # 导航层 (navigator)
    # TODO: navigator/*.c
    
    # 桥接层 (bridge)
    # TODO: bridge/*.c
    
    # 展示器层 (presenters)
    # TODO: presenters/pages/*.c
    # TODO: presenters/widgets/*.c
    # TODO: presenters/controls/*.c
    
    # 主题
    # TODO: themes/*.c
)

idf_component_register(
    SRCS ${LVGL_UI_SRCS}
    INCLUDE_DIRS 
        "."
        "ui"
        "ui/base"
        "ui/controls"
        "ui/indicators"
        "ui/containers"
        "widgets"
        "pages"
        "navigator"
        "bridge"
        "presenters"
        "themes"
    REQUIRES lvgl logger dtbs
)
