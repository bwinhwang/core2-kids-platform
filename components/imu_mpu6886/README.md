# imu_mpu6886 —— MPU6886 最小驱动(读三轴加速度)

Core2 v1.0 的 IMU 驱动。**本平台 IMU 是 MPU6886(WHO_AM_I=0x19),不是 BMI270**,
寄存器图/轴向完全不同,勿照搬 BMI270 代码(`docs/platform/Core2_v1_0.md` §7)。

## 关键事实

- 内部 I2C(G21/G22)@ **0x68**;本机 Core2 缺背板,这颗 IMU **物理上来自 Bottom2 底座**
  ——同芯片同地址同总线,对软件透明,但**不接底座就读不到**。
- **复用 BSP 已初始化的总线句柄**(`bsp_i2c_get_handle()`),不要自己再 init 一条 I2C
  (会和 AXP192/触摸抢总线)。
- 量程 ±2g(16384 LSB/g),输出单位 g;纯重力倾斜应用不需要陀螺仪。

## 用法

```c
imu_mpu6886_init(bsp_i2c_get_handle());   // ESP_ERR_NOT_FOUND = WHO_AM_I 不符/没接底座
imu_accel_t a;
imu_mpu6886_read_accel(&a);               // a.x/y/z 单位 g
```

## 应用层配套(不在本驱动内,按需取用)

- 屏幕轴 ↔ IMU 轴映射**必须实测标定**(提供 invert_x/y、swap_xy 三开关;
  本机定案 `TILT_INVERT_X=1, Y=0, SWAP=0`,见 `main/tuning.h`)。
- 倾斜参考系:**推荐绝对零点(免校准)**——x/y 重力分量即倾斜,"平"=与地面平,
  规则跨会话一致;前提是板偏置 < 应用死区(本机实测 x=-0.023/y=-0.007g,死区 0.06g)。
  姿态校准方案曾踩"零点被开始手势污染"坑,见 `CLAUDE.md` §20.8/§20.9。
- 打盹/唤醒动作检测:用 `motion_detect` 组件(帧间差 + 去抖,阈值已实测定案)。
