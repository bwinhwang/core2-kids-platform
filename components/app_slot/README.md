# app_slot —— 多 App 分区自举

把 16MB flash 变成"游戏卡带机":factory 分区常驻 **launcher**,ota_0~5 六个槽各放一个
独立游戏 bin(见根目录 `partitions.csv`,唯一真源)。**没有任何网络/OTA 下载成分**,
只是借用 IDF 的 otadata 启动选择机制;游戏 bin 用数据线烧进各自槽位
(偏移速查 `tools/flash_map.md`)。

## 启动流转

```
上电 ──► launcher(factory)          otadata 平时是空的
           │ app_slot_launch(n)      = 校验镜像 + 写 otadata + esp_restart
           ▼
        ota_n 游戏
           │ app_main 第一行 app_slot_return_to_factory() = 擦 otadata
           ▼
        之后任何 复位/崩溃/断电/电源键退出 ──► 回 launcher(防变砖)
```

## 游戏侧接法(两行)

```c
void app_main(void) {
    app_slot_return_to_factory();      // 第一行:任何外设初始化之前
    // ... core2_board_init 等 ...
    app_slot_enable_button_exit();     // bring-up 完成后:电源键短按=回主菜单
}
```

## 坑位

- **游戏工程严禁 `idf.py flash`**——会把 app 烧到 factory 偏移,**覆盖 launcher**;
  必须 `esptool.py write_flash <ota_x 偏移> build/<app>.bin`(见 `tools/flash_map.md`)。
- **电源键"短按"要把长按标志也算上**:BSP 写 AXP192 REG 0x36=0x4C → 按住 ≥1s 只报
  "长按"(bit0)、不报短按(bit1),≥4s AXP 硬断电。只认 bit1 会时灵时不灵
  (按得稍久就没反应,按到断电也不退出——实机踩坑)。`core2_power_pek_pressed()`
  已两者都算;轮询 150ms,赶在 4s 硬断电前消费。
- 电源键标志开机会有残留(按键开机本身就是一次按压),
  `app_slot_enable_button_exit()` 内部已先丢弃一次;若自行轮询
  `core2_power_pek_pressed()`,记得同样先清。
- `app_slot_present()` 只读 app 描述符(轻量,适合选择页);完整镜像校验发生在
  `app_slot_launch()` 里(空槽/坏镜像返回错误,launcher 应温柔提示而非重启)。
