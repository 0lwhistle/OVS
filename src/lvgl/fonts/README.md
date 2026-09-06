# Fonts 目录

此目录用于存放 LVGL 字体文件。

## 添加中文字体

1. 准备字体文件 (如 SourceHanSansCN-Regular.otf)
2. 使用 lv_font_conv 转换
3. 生成的 .c 文件放在此目录
4. 在 lv_conf.h 中启用字体

## 示例命令

```bash
# 转换常用中文字符
lv_font_conv --font SourceHanSansCN-Regular.otf \
    --range 0x20-0x7F \
    --symbols 你好世界欢迎使用智能助手设置菜单返回确认取消 \
    --size 14 \
    --format lvgl \
    --bpp 4 \
    -o lv_font_cn_14.c
```

## LVGL 内置字体

LVGL 自带以下字体 (在 lv_conf.h 中启用):
- lv_font_montserrat_12
- lv_font_montserrat_14
- lv_font_montserrat_16
- lv_font_montserrat_18
- lv_font_montserrat_20
- lv_font_montserrat_22
- lv_font_montserrat_24
- lv_font_montserrat_26
- lv_font_montserrat_28
- lv_font_montserrat_30
- lv_font_montserrat_32
- lv_font_montserrat_34
- lv_font_montserrat_36
- lv_font_montserrat_38
- lv_font_montserrat_40
- lv_font_montserrat_42
- lv_font_montserrat_44
- lv_font_montserrat_46
- lv_font_montserrat_48

## 命名规范

- 中文字体: `lv_font_cn_<大小>.c`
- 英文字体: `lv_font_en_<大小>.c`
- 图标字体: `lv_font_icon.c`
