# app_slot —— 多 App 分区自举

把 16MB flash 变成"评估卡带机":factory 分区常驻 **launcher**,ota_0~5 六个槽各放一个
独立评估 app bin(见根目录 `partitions.csv`,唯一真源)。**没有任何网络/OTA 下载成分**,
只是借用 IDF 的 otadata 启动选择机制;评估 app bin 用数据线烧进各自槽位
(偏移速查 `tools/flash_map.md`)。

## 启动流转

```
上电 ──► launcher(factory)          otadata 平时是空的
           │ app_slot_launch(n)      = 校验镜像 + 写 otadata + esp_restart
           ▼
        ota_n 评估 app
           │ app_main 第一行 app_slot_return_to_factory() = 擦 otadata
           ▼
        之后任何 复位/崩溃/断电 ──► 回 launcher(防变砖)
```

## 评估 app 侧接法(一行)

```c
void app_main(void) {
    app_slot_return_to_factory();      // 第一行:任何外设初始化之前
    // ... core2_board_init 等 ...
}
```

**电源键触发的回 launcher 已于 2026-07-09 整体取消**(`app_slot_enable_button_exit()` /
`core2_power_pek_pressed()` 均已删除)——电源键唯一剩下的行为是 AXP192 硬件本身按住 ≥4s
强制断电(纯硬件,不经软件)。评估台目前**没有统一的回 launcher 软件入口**(2026-07-17
平台转向后不再有幼儿掌机时期的家长菜单概念);若某 app 需要页内"返回"按钮,自行在 UI 里加
一个调 `app_slot_return_to_factory()` + `esp_restart()` 的按钮即可,不是平台强制项。

## 数据驱动 launcher(`app_slot_info`)

`app_slot_info(idx, &info)` 读回该槽 app 编译进去的 `esp_app_desc_t`(`project_name` /
`version` / `date` / `time`),供 launcher 直接渲染卡片文字。**加一个新评估 app 不需要改
launcher 代码、不需要重刷 launcher**——这是 2026-07-17 平台转向(受众改为识字评估者)带来的
架构简化,详见 `launcher/README.md`。

## 坑位

- **评估 app 工程严禁 `idf.py flash`**——会把 app 烧到 factory 偏移,**覆盖 launcher**;
  必须 `esptool.py write_flash <ota_x 偏移> build/<app>.bin`(见 `tools/flash_map.md`)。
- `app_slot_present()` 只读 app 描述符(轻量,适合选择页判断槽是否有效);`app_slot_info()`
  多拷贝几个描述符字段供数据驱动渲染用,同样轻量。完整镜像校验发生在 `app_slot_launch()`
  里(空槽/坏镜像返回错误,launcher 应温柔提示而非重启)。
