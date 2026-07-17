#include "unit_bench_scan.h"

#include <string.h>
#include "esp_log.h"

#include "core2_board.h"
#include "unit_probe.h"
#include "unit_dlight.h"
#include "unit_ultrasonic.h"
#include "unit_gesture.h"
#include "unit_8encoder.h"

static const char *TAG = "ub_scan";

// 挂载状态 = "驱动已成功 init 过、认为可以直接读"。索引用 ub_kind_t;EMPTY/UNKNOWN 不使用。
static bool s_attached[UB_KIND_UNKNOWN + 1];

static bool s_chain_present;
static chain_dev_type_t s_chain_type = CHAIN_DEV_UNKNOWN;

static ub_kind_t kind_of_addr(uint8_t addr)
{
    switch (addr) {
        case UNIT_DLIGHT_ADDR_DEFAULT:     return UB_KIND_DLIGHT;
        case UNIT_ULTRASONIC_ADDR_DEFAULT: return UB_KIND_ULTRASONIC;
        case UNIT_GESTURE_ADDR_DEFAULT:    return UB_KIND_GESTURE;
        case UNIT_8ENCODER_ADDR_DEFAULT:   return UB_KIND_8ENCODER;
        default:                          return UB_KIND_UNKNOWN;
    }
}

static bool try_attach(ub_kind_t kind, i2c_master_bus_handle_t bus)
{
    esp_err_t err;
    switch (kind) {
        case UB_KIND_DLIGHT:     err = unit_dlight_init(bus, 0);     break;
        case UB_KIND_ULTRASONIC: err = unit_ultrasonic_init(bus, 0); break;
        case UB_KIND_GESTURE:    err = unit_gesture_init(bus, 0);    break;
        case UB_KIND_8ENCODER:   err = unit_8encoder_init(bus, 0);   break;
        default: return false;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "单元 kind=%d 挂载失败(%s)", (int)kind, esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "单元 kind=%d 已挂载", (int)kind);
    return true;
}

void ub_scan_run(ub_scan_result_t *out, int chain_timeout_ms)
{
    memset(out, 0, sizeof(*out));

    i2c_master_bus_handle_t bus = core2_board_port_a();
    if (bus == NULL || core2_board_port_a_stuck()) {
        out->bus_stuck = true;
        // 总线不可用:全部已知单元判丢失(总线恢复后,下一轮 rescan 会自然重新 init 接管)
        for (int k = UB_KIND_DLIGHT; k <= UB_KIND_8ENCODER; k++) s_attached[k] = false;
    } else {
        unit_probe_result_t results[UB_SCAN_MAX_ROWS * 2];
        int n = unit_probe_scan(bus, results, UB_SCAN_MAX_ROWS * 2);

        bool seen[UB_KIND_UNKNOWN + 1] = {0};
        int row = 0;
        for (int i = 0; i < n && row < UB_SCAN_MAX_ROWS; i++) {
            uint8_t addr = results[i].addr;
            ub_kind_t kind = kind_of_addr(addr);
            out->rows[row].kind = kind;
            out->rows[row].addr = addr;
            row++;

            if (kind >= UB_KIND_DLIGHT && kind <= UB_KIND_8ENCODER) {
                seen[kind] = true;
                if (!s_attached[kind]) s_attached[kind] = try_attach(kind, bus);
            }
        }
        out->row_count = row;

        // 本轮扫描没探到地址的已知单元 = 判丢失(比详情页的连续失败计数更快发现拔线,
        // 覆盖"用户还没进详情页、单元已经被拔走"的场景)
        for (int k = UB_KIND_DLIGHT; k <= UB_KIND_8ENCODER; k++) {
            if (!seen[k]) s_attached[k] = false;
        }
    }

    // Chain 节点探测(单节点直连场景,id=1;超时 = 没接)
    chain_dev_type_t type;
    esp_err_t cerr = chain_bus_get_device_type(1, &type, chain_timeout_ms);
    s_chain_present = (cerr == ESP_OK);
    s_chain_type    = s_chain_present ? type : CHAIN_DEV_UNKNOWN;
    out->chain_present = s_chain_present;
    out->chain_type    = s_chain_type;
}

bool ub_scan_attached(ub_kind_t kind)
{
    if (kind < UB_KIND_DLIGHT || kind > UB_KIND_8ENCODER) return false;
    return s_attached[kind];
}

void ub_scan_mark_lost(ub_kind_t kind)
{
    if (kind < UB_KIND_DLIGHT || kind > UB_KIND_8ENCODER) return;
    s_attached[kind] = false;
}

bool ub_scan_chain_present(void) { return s_chain_present; }

chain_dev_type_t ub_scan_chain_type(void) { return s_chain_type; }

void ub_scan_mark_chain_lost(void)
{
    s_chain_present = false;
    s_chain_type = CHAIN_DEV_UNKNOWN;
}
