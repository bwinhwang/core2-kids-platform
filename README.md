# 倾斜迷宫(Tilt Maze for Toddlers)—— Core2 v1.0 + M5GO Bottom2

给 3~4 岁幼儿的滚珠迷宫:**双手端着 Core2 左右前后倾斜,小球随重力滚动,滚进「家」触发庆祝**,
四关(草地/海边/星空/糖果)循环,零失败、无文字、多通道反馈(屏 / 声 / 震动 / 底座灯带)。

- **应用规格 + 竣工记录**:`CLAUDE.md`(§20 是 as-built 事实,§17 坑位速查)
- **迁移 / 环境 / 进度**:`HANDOFF.md`
- **AI 协作规范(MCP 铁律)**:`AGENTS.md`
- **板级硬件事实**:`docs/platform/Core2_v1_0.md`、`docs/platform/M5GO_Bottom2.md`

> 2026-07-02 由拷贝工程 `~/esp/maze` 整体迁入本仓库;原「幼儿应用平台模板」demo 及
> `bsp_core2`/`app_core` 等自写组件已废弃删除(git 历史可找回)。现应用基于官方
> `espressif/m5stack_core_2` BSP(AXP192/LCD/触摸/LVGL/喇叭)+ 自写组件
> `imu_mpu6886` / `maze_audio` / `ledstrip_fx` / `haptics`。

## 硬件(硬依赖,缺一不可)

**Core2 初代 v1.0 + M5GO Bottom2 底座。** 本机 Core2 缺背部扩展模块,
**IMU(MPU6886 @0x68)与电池都来自底座**,不接底座游戏跑不了。灯带 10×SK6812 @ G25。

## 构建 / 烧录

AI 协作时编译/刷写走 **esp-idf MCP**(`AGENTS.md` §0 铁律);人工命令行:

```bash
source ~/.espressif/tools/activate_idf_v6.0.sh && source $IDF_PATH/export.sh   # 本机 IDF v6.0
idf.py build
idf.py -p /dev/ttyUSB0 flash
python3 tools/serial_capture.py /dev/ttyUSB0 60   # 非交互抓日志(先硬复位到运行态)
```

改 `sdkconfig.defaults` 后:`rm -f sdkconfig` → fullclean → build。

## 目录

| 路径 | 作用 |
|---|---|
| `main/` | app_main(bring-up)· game_state(60Hz+状态机+两级省电)· physics · maze(4 关+BFS 校验)· render(三层渲染)· feedback(四通道编排)· parent_menu · power(AXP192 EXTEN/DCDC3)· tuning.h(全部调参) |
| `components/` | `imu_mpu6886` · `maze_audio` · `ledstrip_fx` · `haptics` |
| `docs/platform/` | Core2 / Bottom2 板级真值(勿改) |
| `docs/units/` | 各 UNIT 外设硬件真值(本应用未用,留作扩展) |
| `tools/` | `serial_capture.py` 非交互串口抓取 |
