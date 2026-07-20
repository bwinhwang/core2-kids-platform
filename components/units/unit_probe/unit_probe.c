#include "unit_probe.h"

#include <stddef.h>

// 已知地址表,与 core2_board.c 的 core2_board_port_a_scan() 日志分支同源
// (docs/units/、CLAUDE.md §10)。0x54 是 8Encoder 上电瞬间总线被拉低时困在 bootloader
// 的状态(core2_board_port_a_recover() 可救回 0x41)。
static const char *known_name(uint8_t addr)
{
    switch (addr) {
        case 0x23: return "DLight (BH1750)";
        case 0x41: return "8Encoder";
        case 0x54: return "8Encoder (bootloader)";
        case 0x57: return "Ultrasonic (RCWL)";
        case 0x62: return "CO2L (SCD41)";
        case 0x73: return "Gesture (PAJ7620U2)";
        default:   return NULL;
    }
}

int unit_probe_scan(i2c_master_bus_handle_t bus, unit_probe_result_t *out, int cap)
{
    if (!bus) return 0;
    int found = 0;
    for (uint16_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_master_probe(bus, addr, 50) == ESP_OK) {
            if (out && found < cap) {
                out[found].addr = (uint8_t)addr;
                out[found].known_name = known_name((uint8_t)addr);
            }
            found++;
        }
    }
    return found;
}
