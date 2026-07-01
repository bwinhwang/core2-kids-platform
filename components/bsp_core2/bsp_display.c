// bsp_display.c —— ILI9342C 屏能力(Core2, SPI)
//
// 只提供“画区块”能力;雨纹/UI 属应用逻辑。面板方向/反色/BGR 是 Core2 面板固定
// 事实(来源 esp-bsp m5stack_core_2):init 后必须 invert_color(true),横屏 swap_xy。
#include "bsp.h"
#include "bsp_priv.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "bsp_display";

// Core2 ILI9342C 面板固定属性
#define PANEL_INVERT    true
#define PANEL_SWAP_XY   true
#define PANEL_MIRROR_X  false
#define PANEL_MIRROR_Y  false
#define PANEL_BGR       1        // M5 面板多为 BGR:颜色 R/B 互换就翻这里
#define FILL_BAND_H     40       // 整屏填充分条高度

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static uint16_t *s_fill_buf;

static inline int clampi(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

uint16_t bsp_rgb565(int r, int g, int b)
{
    r = clampi(r); g = clampi(g); b = clampi(b);
#if PANEL_BGR
    int t = r; r = b; b = t;
#endif
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return __builtin_bswap16(c);   // SPI 先发高字节
}

esp_err_t bsp_display_init(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num     = BSP_PIN_LCD_MOSI,
        .sclk_io_num     = BSP_PIN_LCD_SCLK,
        .miso_io_num     = -1,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = BSP_LCD_W * FILL_BAND_H * (int)sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BSP_LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO),
                        TAG, "spi bus");

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num       = BSP_PIN_LCD_DC,
        .cs_gpio_num       = BSP_PIN_LCD_CS,
        .pclk_hz           = BSP_LCD_PCLK_HZ,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .spi_mode          = 0,
        .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_HOST,
                        &io_cfg, &s_io), TAG, "panel io");

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,                          // RST 走 AXP192 IO4(已复位)
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,   // BGR 在 bsp_rgb565() 里处理
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(s_io, &panel_cfg, &s_panel), TAG, "ili9341");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    esp_lcd_panel_invert_color(s_panel, PANEL_INVERT);
    esp_lcd_panel_swap_xy(s_panel, PANEL_SWAP_XY);
    esp_lcd_panel_mirror(s_panel, PANEL_MIRROR_X, PANEL_MIRROR_Y);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp on");

    s_fill_buf = heap_caps_malloc(BSP_LCD_W * FILL_BAND_H * sizeof(uint16_t), MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(s_fill_buf, ESP_ERR_NO_MEM, TAG, "fill buf");

    ESP_LOGI(TAG, "屏就绪(ILI9342C %dx%d @ SPI%d)", BSP_LCD_W, BSP_LCD_H, BSP_LCD_SPI_HOST + 1);
    return ESP_OK;
}

esp_err_t bsp_display_draw(int x, int y, int w, int h, const uint16_t *pixels)
{
    return esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, pixels);
}

esp_err_t bsp_display_fill(uint16_t color)
{
    // 只读缓冲反复提交:内容不变,多条在途传输读同一 buffer 安全
    for (int i = 0; i < BSP_LCD_W * FILL_BAND_H; i++) s_fill_buf[i] = color;
    for (int y = 0; y < BSP_LCD_H; y += FILL_BAND_H) {
        int h = (BSP_LCD_H - y > FILL_BAND_H) ? FILL_BAND_H : (BSP_LCD_H - y);
        ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(s_panel, 0, y, BSP_LCD_W, y + h, s_fill_buf),
                            TAG, "fill");
    }
    return ESP_OK;
}
