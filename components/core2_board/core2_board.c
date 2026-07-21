#include "core2_board.h"

#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "bsp/m5stack_core_2.h"

#include "core2_power.h"
#include "ledstrip_fx.h"
#include "audio_fx.h"
#include "haptics.h"
#include "imu_mpu6886.h"
#include "screenshot.h"
#include "touch_btns.h"

static const char *TAG = "core2_board";

static lv_display_t *s_disp;
static i2c_master_bus_handle_t s_i2c;

bool core2_board_i2c_scan(void)
{
    ESP_LOGI(TAG, "扫描内部 I2C 总线 (G21/G22)…");
    bool found_imu = false;
    for (uint16_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_master_probe(s_i2c, addr, 50) == ESP_OK) {
            const char *name = "?";
            switch (addr) {
                case 0x34: name = "AXP192 (电源)";   break;
                case 0x38: name = "FT6336U (触摸)";  break;
                case 0x51: name = "BM8563 (RTC)";    break;
                case 0x68: name = "MPU6886 (IMU)"; found_imu = true; break;
            }
            ESP_LOGI(TAG, "  发现 0x%02X  %s", addr, name);
        }
    }
    return found_imu;
}

esp_err_t core2_board_init(const core2_board_cfg_t *cfg)
{
    core2_board_cfg_t c = cfg ? *cfg : CORE2_BOARD_CFG_KIDS_DEFAULT;

    // 1) 内部 I2C(AXP192/触摸/RTC/IMU 都在这条总线)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "bsp_i2c_init 失败");
    s_i2c = bsp_i2c_get_handle();

    // 2) 屏 + LVGL(BSP 同时做 AXP192 屏电/背光基础配置)
    s_disp = bsp_display_start();
    ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG,
                        "bsp_display_start 失败(检查 CONFIG_BSP_PMU_AXP192)");
    ESP_RETURN_ON_ERROR(bsp_display_brightness_set(c.brightness_pct), TAG, "设背光失败");

    // 3) I2C 自检(顺带确认底座 IMU 在位)
    bool imu_present = core2_board_i2c_scan();

    // 4) AXP192 直控绑定(EXTEN/DCDC3)。必须在 bsp_display_start 之后:
    //    BSP 初始化会重写 REG 0x12,先绑先开会被清掉。
    ESP_RETURN_ON_ERROR(core2_power_init(s_i2c), TAG, "core2_power_init 失败");

    // 5) PORT.A 待机上拉(须在开 5V 之前):8Encoder 这类 STM32 单元的 bootloader
    //    在上电瞬间检测 I2C 两线,双低=留在引导态(0x54)不进应用(内部固件源码核实,
    //    2026-07-03)。先拉高 G32/33 再给 5V,保证单元"睁眼"看到空闲总线;
    //    之后 i2c 驱动接管这两个脚时会正常重配,这里的上拉无副作用。
    gpio_pullup_en(GPIO_NUM_32);
    gpio_pullup_en(GPIO_NUM_33);

    //    灯带:先供电(M-Bus 5V/EXTEN)再起驱动,顺序反了就是"数据在跑灯全黑"
    if (c.enable_leds) {
        ESP_RETURN_ON_ERROR(core2_power_bus_5v(true), TAG, "开 M-Bus 5V 失败");
        ESP_RETURN_ON_ERROR(ledstrip_fx_init(), TAG, "灯带初始化失败");
        ledstrip_fx_set_max_brightness(c.led_max_brightness);
    }

    // 6) 喇叭(整局保持 open,防爆音)
    if (c.enable_audio) {
        ESP_RETURN_ON_ERROR(audio_fx_init(), TAG, "音频初始化失败");
    }

    // 7) 震动(LDO3 供电由 BSP feature 管)
    if (c.enable_haptics) {
        ESP_RETURN_ON_ERROR(haptics_init(), TAG, "震动初始化失败");
    }

    // 8) IMU(MPU6886 @0x68,复用内部 I2C 总线,勿自建总线)
    if (c.enable_imu) {
        if (!imu_present) {
            ESP_LOGE(TAG, "✗ 没扫到 0x68:Bottom2 底座没接?(IMU 和电池都来自底座)");
            return ESP_ERR_NOT_FOUND;
        }
        ESP_RETURN_ON_ERROR(imu_mpu6886_init(s_i2c), TAG,
                            "IMU 初始化失败(WHO_AM_I 应为 0x19)");
    }

    // 9) 串口截图(调试设施,失败不拦平台起机)
    if (screenshot_init() != ESP_OK) {
        ESP_LOGW(TAG, "screenshot 初始化失败,串口截图不可用(游戏不受影响)");
    }

    // 10) 屏下三键(BtnA长按=回launcher / BtnB短按=截屏 / BtnC长按=关机;全局固定,
    //     所有 app 白拿,失败不拦起机)
    if (touch_btns_init(s_i2c) != ESP_OK) {
        ESP_LOGW(TAG, "touch_btns 初始化失败,屏下三键不可用");
    }

    ESP_LOGI(TAG, "平台就绪:屏%s 灯带%s 音频%s 震动%s IMU%s",
             "✓", c.enable_leds ? "✓" : "-", c.enable_audio ? "✓" : "-",
             c.enable_haptics ? "✓" : "-", c.enable_imu ? "✓" : "-");
    return ESP_OK;
}

lv_display_t *core2_board_display(void) { return s_disp; }

i2c_master_bus_handle_t core2_board_i2c(void) { return s_i2c; }

// ── PORT.A 外接 I2C(G32/G33,懒加载)────────────────────────────────
// 内部总线占 I2C_NUM_1(CONFIG_BSP_I2C_NUM=1),PORT.A 用 I2C_NUM_0。
// 5V 供电依赖 M-Bus 5V/EXTEN,坑位说明见头文件。
#define PORT_A_SDA  GPIO_NUM_32
#define PORT_A_SCL  GPIO_NUM_33

static i2c_master_bus_handle_t s_port_a;

i2c_master_bus_handle_t core2_board_port_a(void)
{
    if (!s_port_a) {
        i2c_master_bus_config_t cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = PORT_A_SDA,
            .scl_io_num = PORT_A_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        esp_err_t err = i2c_new_master_bus(&cfg, &s_port_a);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "PORT.A I2C 创建失败: %s", esp_err_to_name(err));
            s_port_a = NULL;
        }
    }
    return s_port_a;
}

bool core2_board_port_a_scan(void)
{
    i2c_master_bus_handle_t bus = core2_board_port_a();
    if (!bus) return false;

    // 先看线电平(I2C 驱动开漏配置带输入使能,可直读):空闲总线应双双为 1。
    // 有线为 0 = 总线被拉死,扫描无意义——典型是单元没供电:死掉的 3.3V 轨让单元
    // 板载上拉变成"下拉",反把线拽低(症状:所有地址 probe timeout,而非快速 NACK)。
    int sda = gpio_get_level(PORT_A_SDA);
    int scl = gpio_get_level(PORT_A_SCL);
    if (sda == 0 || scl == 0) {
        ESP_LOGW(TAG, "PORT.A 总线被拉死(SDA=%d SCL=%d,空闲应=1/1),跳过扫描。"
                      "排查:①单元 5V 供电(底座灯带亮吗?同一路 5V/EXTEN)"
                      "②Grove 线插紧/换线(线内短路同症状)③单元是否插在红色 PORT.A",
                 sda, scl);
        return false;
    }

    ESP_LOGI(TAG, "扫描 PORT.A I2C (G32/G33,线电平 SDA=%d SCL=%d)…", sda, scl);
    // 补充:若下方扫到 0x54 = 8Encoder 困在 bootloader,用 core2_board_port_a_recover() 断电重启它
    bool found_any = false;
    for (uint16_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_master_probe(bus, addr, 50) == ESP_OK) {
            const char *name = "?";
            switch (addr) {
                case 0x23: name = "DLight (BH1750)";     break;
                case 0x41: name = "8Encoder";            break;
                case 0x54: name = "8Encoder 引导态(bootloader)!"
                                  "单元上电时总线被拉低所致,断电重启单元可回 0x41"; break;
                case 0x57: name = "Ultrasonic (RCWL)";   break;
                case 0x73: name = "Gesture (PAJ7620U2)"; break;
            }
            ESP_LOGI(TAG, "  发现 0x%02X  %s", addr, name);
            found_any = true;
        }
    }
    if (!found_any) {
        ESP_LOGW(TAG, "  PORT.A 线电平正常但无器件应答(空总线):①插错口?红色 I2C 口在"
                      " Core2 机身侧面,底座黑口(PORT.B)/蓝口(PORT.C)不是 I2C"
                      " ②线缆数据线断/接触不良");
    }
    return found_any;
}

bool core2_board_port_a_stuck(void)
{
    if (!s_port_a) return false;   // 总线还没建,读不出有意义的电平
    return gpio_get_level(PORT_A_SDA) == 0 || gpio_get_level(PORT_A_SCL) == 0;
}

esp_err_t core2_board_port_a_recover(void)
{
    ESP_LOGW(TAG, "PORT.A 单元断电重启:切 M-Bus 5V 一个来回(底座灯带会闪一下)…");
    ESP_RETURN_ON_ERROR(core2_power_bus_5v(false), TAG, "关 M-Bus 5V 失败");
    vTaskDelay(pdMS_TO_TICKS(100));

    // 🔴 断电窗口内必须复位主机 I2C 控制器:之前的失败事务会让 FSM 卡死、
    // 开漏输出拽住 SDA/SCL——单元重新上电"睁眼"看到双低,又会困进 bootloader
    // (首版恢复少了这步,实测单元复电后落在 0x54)。此刻单元没电,
    // 复位附带的 clear-bus 脉冲无害。
    if (s_port_a) i2c_master_bus_reset(s_port_a);
    gpio_pullup_en(PORT_A_SDA);
    gpio_pullup_en(PORT_A_SCL);

    vTaskDelay(pdMS_TO_TICKS(300));   // 让单元电容放净,真断电
    // 此刻线平仅供参考:单元板载上拉挂在断电轨上仍会拽低,复电瞬间即回高
    ESP_LOGI(TAG, "复电前线平 SDA=%d SCL=%d(主机侧已释放)",
             gpio_get_level(PORT_A_SDA), gpio_get_level(PORT_A_SCL));
    ESP_RETURN_ON_ERROR(core2_power_bus_5v(true), TAG, "开 M-Bus 5V 失败");
    vTaskDelay(pdMS_TO_TICKS(150));   // 单元 DC-DC 起 + STM32 boot(bootloader 见双高跳应用)
    return ESP_OK;
}
