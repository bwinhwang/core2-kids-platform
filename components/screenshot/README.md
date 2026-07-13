# screenshot —— 串口触发屏幕截图导出(开发调试组件)

让主机上的 AI 协作者(Claude Code)能"看见"设备当前画面:

```
主机: python3 tools/screenshot.py [/dev/ttyUSB0] [out.png]   # 发 "SHOT\n"
设备: LVGL 活动屏 lv_snapshot → PSRAM(RGB565)→ RLE → Base64 → 日志串口
主机: 解码校验 CRC32 → 存 PNG → 打印绝对路径 → Claude Code 直接 Read
```

## 接入

`core2_board_init()` 末尾已代调 `screenshot_init()`,**所有 app / launcher 重编即自带**,
app 代码零改动。程序化触发另有 `screenshot_dump_now()`。

依赖 `CONFIG_LV_USE_SNAPSHOT=y`(已进 `sdkconfig.platform`)。老工程第一次重编会撞上
本组件的编译期 `#error`,照提示 `rm -f sdkconfig && idf.py fullclean && build` 即可
(= AGENTS.md §3 改平台 sdkconfig 的既有仪式)。

## 线上协议(115200,与日志同一条 UART0)

- 触发:主机发一行 `SHOT`(收线任务只装 UART 驱动收 RX,不碰控制台 TX 路径——
  Confirmed via espressif-docs:ESP-FAQ 明确 UART0 日志输出与主机输入可并存)。
- 应答:
  ```
  <<<SHOT w=320 h=240 fmt=rgb565le enc=rle16 raw=153600 rle=NNN crc32=XXXXXXXX>>>
  $<Base64 76 字符/行,内容为 RLE 流>
  …
  <<<SHOT-END>>>
  ```
- `rle16`:`(u8 游程 1..255, u16le RGB565 像素)` 重复;幼儿风大色块典型压 5~15×,
  一张图 2~5s 吐完(最坏 ~30s)。
- 导出期间 `esp_log_set_vprintf` 静默其它任务日志,防 Base64 行被拦腰截断;
  哨兵行 + `$` 前缀让主机侧天然免疫残余杂音。

## 设计取舍 / 坑

- **抓帧走 `lv_snapshot_take_to_draw_buf` 重渲染**(持 LVGL 锁 ~50ms,一帧顿),
  不截 esp_lcd flush——SPI 屏不可回读,LVGL 又是部分缓冲,没有整屏 framebuffer 可偷。
- 缓冲全在 PSRAM(抓帧 ~190KB + RLE 最坏 ~230KB,瞬时,用完即还)。
- 只截 `lv_screen_active()` 对象树,`lv_layer_top/sys` 浮层不入镜(平台各 app 均画在
  活动屏上,目前无此场景;真需要时对 top layer 再 snapshot 一次叠加)。
- **主机脚本绝不能复位芯片**:pyserial 默认拉 DTR/RTS 会把 ESP32 带进复位/下载态,
  `screenshot.py` 先建对象置低 DTR/RTS 再 open(与 `serial_capture.py` 的"故意复位"相反)。
- 触发字符串走裸 `uart_read_bytes`,无 vfs 行尾翻译:`\r` `\n` 都当行终止。
- 与 `idf.py monitor` 互斥(串口独占),抓图前先退 monitor。
