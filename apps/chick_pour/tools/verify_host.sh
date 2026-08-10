#!/usr/bin/env bash
# 主机跑 main/layout.c + main/flock.c 的图纸校验与重散布点回归(见 verify_host.c 文件头)。
# 退出码 0 = 全过。改 tuning.h / layout.h 的几何常量、改图纸表、动 layout_verify 或布点 ——
# 烧板之前先跑这个(WSL 烧不了板,每轮实机都要人插线)。
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="$(mktemp -d)/verify_host"
gcc -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
    -I main -I tools/hoststub -I ../../components/imu_mpu6886/include \
    -o "$OUT" tools/verify_host.c main/layout.c main/flock.c -lm
"$OUT"
