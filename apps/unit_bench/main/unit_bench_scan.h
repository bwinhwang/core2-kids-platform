// unit_bench_scan —— PORT.A I2C 扫描 + Chain 探测 + 已知单元热插拔挂载状态(model 层)
//
// 与 UI 层解耦:UI 只查询 ub_scan_* 的只读状态,实际的探测/挂载 I2C·UART 事务全在这里,
// 方便脱离 LVGL 单独测试/复用。挂载策略(CLAUDE.md §10 热插拔通用形态):
//   - unit_probe_scan 是纯地址探测(zero-length write 判 ACK),无副作用,每轮 rescan 都做;
//   - 已挂载(attached=true)的已知单元**不会**被重新 init——重灌配置/清计数会打断正在
//     进行的读数流(如 8Encoder init 会清空 Increment 累计寄存器)。只有"当前未挂载"的
//     单元才会尝试 init 接管;真正的掉线判定交给调用方(详情页读失败连续 ~20 帧后显式
//     调用 ub_scan_mark_lost()),下一轮 rescan 才会再次尝试 init——这样"用户正在读数"
//     和"后台重新探测"不会互相打架。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "chain_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UB_SCAN_MAX_ROWS 8   // PORT.A 扫描结果最多展示的行数(见 README「已知限制」)

typedef enum {
    UB_KIND_EMPTY = 0,
    UB_KIND_DLIGHT,
    UB_KIND_ULTRASONIC,
    UB_KIND_GESTURE,
    UB_KIND_8ENCODER,
    UB_KIND_UNKNOWN,      // 探到应答但不是已知单元地址
} ub_kind_t;

typedef struct {
    ub_kind_t kind;
    uint8_t   addr;
} ub_scan_row_t;

typedef struct {
    bool              bus_stuck;   // PORT.A 总线被拉死(错误显式呈现用,CLAUDE.md §2 原则 2)
    int               row_count;   // rows[0..row_count-1] 有效
    ub_scan_row_t     rows[UB_SCAN_MAX_ROWS];
    bool              chain_present;
    chain_dev_type_t  chain_type;
} ub_scan_result_t;

/**
 * @brief 做一轮扫描:PORT.A 地址探测(对未挂载的已知单元尝试 init 接管)+ Chain 节点探测。
 *
 * 阻塞时间:PORT.A 扫描 <~20ms(112 地址,正常 NACK 很快,只有真拉死才会各吃满 50ms 超时,
 * 但那种情况本函数会先被 core2_board_port_a_stuck() 短路掉);Chain 探测阻塞至多
 * chain_timeout_ms(节点不在位时会等满超时)。调用方按调用场景权衡:后台周期扫描可放宽
 * 超时,用户手动点 Rescan 时这点阻塞是可接受的一次性停顿(核心目的是不静默——错误要显式
 * 呈现,不能因为怕阻塞就跳过探测)。
 *
 * @param out              扫描结果输出。
 * @param chain_timeout_ms Chain 探测超时(建议 200~500ms)。
 */
void ub_scan_run(ub_scan_result_t *out, int chain_timeout_ms);

/** @brief 查询某已知单元当前是否挂载(详情页轮询前先查这个,决定要不要继续读)。 */
bool ub_scan_attached(ub_kind_t kind);

/** @brief 详情页判定"读失败连续 N 帧,判拔线"后调用:清挂载状态,下一轮 rescan 会重试 init。 */
void ub_scan_mark_lost(ub_kind_t kind);

/** @brief Chain 节点当前是否在位(与最近一次 ub_scan_run 的探测结果一致)。 */
bool ub_scan_chain_present(void);

/** @brief Chain 节点类型(chain_present=false 时恒为 CHAIN_DEV_UNKNOWN)。 */
chain_dev_type_t ub_scan_chain_type(void);

/** @brief 详情页判定 Chain 节点读失败连续 N 帧后调用:清在位状态,下一轮 rescan 重新 probe。 */
void ub_scan_mark_chain_lost(void);

#ifdef __cplusplus
}
#endif
