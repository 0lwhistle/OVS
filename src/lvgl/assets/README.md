# Assets 目录

此目录用于存放 LVGL UI 资源文件：

## 文件类型

- **图片**: `.png`, `.jpg`, `.bmp` (需要转换为 C 数组)
- **字体**: `.ttf`, `.otf` (需要使用 lv_font_conv 转换)
- **图标**: `.svg` (需要转换)

## 转换工具

### 图片转换
```bash
# 使用在线工具或 LVGL 官方工具
# https://lvgl.io/tools/imageconverter
```

### 字体转换
```bash
# 使用 lv_font_conv 工具
npm install -g lv_font_conv

# 示例：转换中文字符
lv_font_conv --font SourceHanSansCN-Regular.otf \
    --range 0x20-0x7F \
    --symbols 你好世界欢迎使用 \
    --size 14 \
    --format lvgl \
    --bpp 4 \
    -o my_font_14.c
```

## 命名规范

- 图片: `img_<名称>.c` (如 `img_logo.c`)
- 字体: `font_<名称>_<大小>.c` (如 `font_cn_14.c`)
- 图标: `icon_<名称>.c` (如 `icon_wifi.c`)
