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
        之后任何 复位/崩溃/断电 ──► 回 launcher(防变砖)
```

## 游戏侧接法(一行)

```c
void app_main(void) {
    app_slot_return_to_factory();      // 第一行:任何外设初始化之前
    // ... core2_board_init 等 ...
}
```

回 launcher 的软件入口目前只有各游戏自己的家长菜单 Home(见 `apps/tilt_maze/main/parent_menu.c`
范例;目前仅 tilt_maze 已接,其余卡带待补)。**电源键触发的回 launcher 已于 2026-07-09 整体取消**
(`app_slot_enable_button_exit()` / `core2_power_pek_pressed()` 均已删除)——电源键唯一剩下的行为
是 AXP192 硬件本身按住 ≥4s 强制断电(纯硬件,不经软件)。

## 坑位

- **游戏工程严禁 `idf.py flash`**——会把 app 烧到 factory 偏移,**覆盖 launcher**;
  必须 `esptool.py write_flash <ota_x 偏移> build/<app>.bin`(见 `tools/flash_map.md`)。
- `app_slot_present()` 只读 app 描述符(轻量,适合选择页);完整镜像校验发生在
  `app_slot_launch()` 里(空槽/坏镜像返回错误,launcher 应温柔提示而非重启)。
