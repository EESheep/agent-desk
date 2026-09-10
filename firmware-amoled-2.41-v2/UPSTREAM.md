# Hardware and font sources

The V2 `components/lcd_bsp` and `components/main_config` files originate from
Waveshare's [ESP32-S3-Touch-AMOLED-2.41-V2](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.41-V2),
commit `0eaf16cfae3440d445f2b6caa71e94065094551f`,
`02_Example/ESP-IDF/09_LVGL_V9_Test/components`.

Local changes: 600×450 landscape (RM690B0 command 0x36=0x30), V2 Arduino example's
touch mapping (swap XY, mirror raw Y), 20-row refresh buffers to coexist with Wi-Fi,
and esp_lvgl_adapter 0.5.2 for IDF 6.1.
The official example uses the `esp_lcd_sh8601` component with custom RM690B0
initialization commands; this is intentional and does not identify the panel as SH8601.
Original declarations/comments are retained; the copied files contain no explicit
license header. Upstream ownership is not replaced by the project's original-code license.

`main/font_cjk_28.c` is generated from
[NotoSansCJKsc-Regular.otf](https://github.com/notofonts/noto-cjk/blob/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf),
licensed under SIL Open Font License 1.1; see `main/FONT-LICENSE.txt`.
Font rasterizer: `lv_font_conv@1.5.3`, 28 px, 2 bpp, no kerning, no compression,
Unicode ranges 0020–00FF, 2000–206F, 3000–303F, 4E00–9FFF, FF00–FFEF.
The font descriptor uses 36 px line height and 7 px baseline for compact CJK layout.
This covers basic CJK; emoji and supplementary-plane characters are not included.

Reproduce from the repository root after putting the OTF under `local/`:

```powershell
npm.cmd install --prefix local/font-tools --cache local/npm-cache lv_font_conv@1.5.3 --no-audit --no-fund
node local/font-tools/node_modules/lv_font_conv/lv_font_conv.js --font local/NotoSansCJKsc-Regular.otf --size 28 --bpp 2 --format lvgl --range 0x20-0xff,0x2000-0x206f,0x3000-0x303f,0x4e00-0x9fff,0xff00-0xffef --no-kerning --no-compress --lv-font-name font_cjk_28 -o firmware-amoled-2.41-v2/main/font_cjk_28.c
```

Then set the generated descriptor's `line_height` to 36 and `base_line` to 7.
The generated C file is checked in so normal builds do not require Node or font tools.
