# chain_lab — Chain Encoder + Joystick 验证台(as-built)

> 本文件是 **chain_lab 应用的竣工记录**(原单-app CLAUDE.md §20.17,2026-07 迁出)。
> 跨应用平台事实(桌面玩法省电坑)见根平台手册 `CLAUDE.md`(§10 做新 App 指南、§7 电源·休眠、§11 坑位)。
> **头一次接 Chain 系列外设**——不是 Grove I2C 而是 **UART 115200 8N1 菊花链**(Core2 PORT.C 直接作 Chain host)。
> **占 ota_4(0x990000)**。烧录:`tools/flash_one.sh chain_lab`(= esptool write-flash 0x990000)。

产出:两个设备驱动 + 一个共享 UART 传输层 + 一个上板验证 App。守零失败/即时因果/多通道冗余/渲染红线。

## 协议来源(逐字节核实)

官方 PDF 只给链接不含正文;帧格式**从官方 Arduino 库 `github m5stack/M5Chain` 源码逐字节核实**(AGENTS.md §1 的"MCP 缺板级细节 → 厂商 GitHub raw"退路)。已回填进 `docs/units/Chain_Encoder.md`/`Chain_Joystick.md` §4。

帧 = `AA55 | lenLo lenHi | id | cmd | data | crc | 55AA`,`len=3+dataLen`(小端)、`crc=(id+cmd+data)&0xFF`、`id`=链上位置(1 起);应答载荷小端。节点主动发心跳(0xFD~1/s)/枚举(0xFC)/按键上报(0xE0),主机轮询按 (id,cmd) 匹配、其余丢弃。

## 平台新增(可复用)

- ①`components/units/chain_bus` —— Chain UART 传输层:装 UART2(G14 TX/G13 RX)、`chain_bus_request(id,cmd,data..)` 一次请求/应答(发前 flush 清心跳积压,收时逐帧 CRC+匹配),通用命令(设备类型/固件版本/RGB/亮度),`chain_bus_sniff(ms)` 抓原始字节做诊断;
- ②`components/units/unit_chain_encoder`(U207,GET_VALUE=0x10 绝对计数 int16 + BUTTON 0xE1);
- ③`components/units/unit_chain_joystick`(U205,GET_16ADC=0x30 原始 ADC + BUTTON 0xE1,应用侧软件归中)。
- 三者都是"每值一次事务"的窄驱动,支持热插拔重 probe。

## 验证台玩法

左圆环表盘(指针角=编码器绝对计数)+ 中心数值;右方框光点(位置=摇杆归一化 X/Y)+ 原始 ADC 数值;按键 → 光点/指针变绿 + **节点板载 RGB 闪白** + 轻震;平时编码器灯随计数走彩虹、摇杆灯随方位换色 —— 同时验证**读(RX)与写节点 RGB(TX)**两条路。开机扫链位 1..4 自动认 encoder/joystick(单节点=1,级联=1&2);没插=无字提示卡 + 2s 重扫。

## ⚠️ Core2 直连 Chain host 未经官方背书

硬件文档标注未验证,官方范式要挂独立 Chain 主控(如 DualKey)。本 App 即验证手段:不通时 `chain_bus_sniff` 抓 PORT.C 原始字节——有心跳=链路通只是没认到,一字节都无=没供电/接反/直连不成立。

## 🔴 供电与省电

- **PORT.C 供电 = M-Bus 5V(EXTEN)**,与 PORT.A 单元同源(`core2_board_init(enable_leds=true)` 已代开);深度省电切 5V → 节点断电复位,唤醒后重扫接管。
- **桌面玩法省电坑**照旧:机身不动≠没人玩,转/推/按都 `core2_sleep_kick`。

## 待实机标定(tuning.h)

`JOY_HALF_SPAN`(归一化满量程)、`JOY_DEADZONE/JOY_MOVE_KICK`、`JOY_INVERT_X/Y/SWAP_XY`(摇杆轴向,同 tilt_maze TILT_INVERT_*;**实测左右反已默认 X 取反**)、`ENC_DEG_PER_STEP`(表盘度/格,纯视觉)、`NODE_RGB_BRIGHTNESS`;若旋钮方向反,应用侧取反或用 SET_AB_STATUS。

## launcher 图标

launcher 加 chain_lab 专属图标(旋钮+摇杆方框,**要重刷 launcher 才显示**;不刷也能玩、显示通用笑脸)。

## 状态

✅ **build 通过**(chain_lab app 0x9ea90 ≈ 650KB,槽内余 59%;launcher 重编过);
⏳ **待烧录实机**:确认 Core2 直连 host 成立、转编码器/推摇杆/按键的四通道即时反馈、节点 RGB 换色、摇杆归中标定、打盹-操作唤醒、深度省电-拿起唤醒-重扫、拔线提示卡、电源键回 launcher。
