#include "maze.h"
#include "tuning.h"
#include <math.h>

// ── 关卡库(§19,4 关一关一世界,难度=拐弯数递增 + 换风景)─────────────
static const level_t k_levels[] = {
    {   // L1 · 草地(Tier 0,1 弯,引导 ON)
        .id = 1, .world = WORLD_MEADOW, .tier = 0, .guide = GUIDE_ON,
        .grid = {
            "########",
            "######H#",
            "######.#",
            "######.#",
            "#S.....#",
            "########",
        },
        .start = {1, 4}, .home = {6, 1}, .n_stars = 0,
    },
    {   // L2 · 海边(Tier 1,2 弯,引导淡)
        .id = 2, .world = WORLD_SEASIDE, .tier = 1, .guide = GUIDE_FAINT,
        .grid = {
            "########",
            "#H....##",
            "#####.##",
            "#####.##",
            "#S....##",
            "########",
        },
        .start = {1, 4}, .home = {1, 1}, .n_stars = 0,
    },
    {   // L3 · 星空(Tier 2,3 弯,1 星,无引导)
        .id = 3, .world = WORLD_STARRY, .tier = 2, .guide = GUIDE_OFF,
        .grid = {
            "########",
            "#S..#H##",
            "###.#.##",
            "###.#.##",
            "###.*.##",
            "########",
        },
        .start = {1, 1}, .home = {5, 1}, .stars = {{4, 4}}, .n_stars = 1,
    },
    {   // L4 · 糖果(奖励关,3 弯·路最长,1 星,无引导)
        .id = 4, .world = WORLD_CANDY, .tier = 3, .guide = GUIDE_OFF,
        .grid = {
            "########",
            "#S.....#",
            "######.#",
            "#...*..#",
            "#H######",
            "########",
        },
        .start = {1, 1}, .home = {1, 4}, .stars = {{4, 3}}, .n_stars = 1,
    },
};

#define N_LEVELS ((int)(sizeof(k_levels) / sizeof(k_levels[0])))

int maze_level_count(void) { return N_LEVELS; }

const level_t *maze_get_level(int idx)
{
    if (idx < 0) idx = 0;
    return &k_levels[idx % N_LEVELS];
}

bool maze_is_wall(const level_t *lvl, int col, int row)
{
    if (col < 0 || col >= MAZE_COLS || row < 0 || row >= MAZE_ROWS) return true;
    return lvl->grid[row][col] == '#';
}

vec2_t maze_cell_center(cell_t c)
{
    return (vec2_t){ c.col * MAZE_CELL + MAZE_CELL / 2, c.row * MAZE_CELL + MAZE_CELL / 2 };
}

// ── BFS 可解性校验(§4.1)─────────────────────────────────────────────
bool maze_is_solvable(const level_t *lvl)
{
    bool vis[MAZE_ROWS][MAZE_COLS] = {0};
    cell_t q[MAZE_ROWS * MAZE_COLS];
    int head = 0, tail = 0;

    q[tail++] = lvl->start;
    vis[lvl->start.row][lvl->start.col] = true;

    const int dc[4] = {1, -1, 0, 0};
    const int dr[4] = {0, 0, 1, -1};

    while (head < tail) {
        cell_t c = q[head++];
        if (c.col == lvl->home.col && c.row == lvl->home.row) return true;
        for (int i = 0; i < 4; i++) {
            int nc = c.col + dc[i], nr = c.row + dr[i];
            if (nc < 0 || nc >= MAZE_COLS || nr < 0 || nr >= MAZE_ROWS) continue;
            if (vis[nr][nc]) continue;
            if (maze_is_wall(lvl, nc, nr)) continue;
            vis[nr][nc] = true;
            q[tail++] = (cell_t){ nc, nr };
        }
    }
    return false;
}

// ── 圆 vs 墙格 滑行式碰撞(§4.4)──────────────────────────────────────
static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

maze_collision_t maze_resolve_collision(const level_t *lvl, vec2_t *pos, vec2_t *vel, float r)
{
    maze_collision_t out = { .hit = false, .speed = 0 };

    // 多迭代几次,消除拐角处残余穿透,让滑行更稳
    for (int iter = 0; iter < 3; iter++) {
        bool any = false;

        int col0 = (int)floorf((pos->x - r) / MAZE_CELL);
        int col1 = (int)floorf((pos->x + r) / MAZE_CELL);
        int row0 = (int)floorf((pos->y - r) / MAZE_CELL);
        int row1 = (int)floorf((pos->y + r) / MAZE_CELL);

        for (int row = row0; row <= row1; row++) {
            for (int col = col0; col <= col1; col++) {
                if (!maze_is_wall(lvl, col, row)) continue;

                float left = col * MAZE_CELL, top = row * MAZE_CELL;
                float right = left + MAZE_CELL, bottom = top + MAZE_CELL;

                // 墙格 AABB 上离球心最近的点
                float qx = clampf(pos->x, left, right);
                float qy = clampf(pos->y, top, bottom);
                float dx = pos->x - qx, dy = pos->y - qy;
                float d2 = dx * dx + dy * dy;
                if (d2 >= r * r) continue;   // 没碰到

                float nx, ny, pen;
                float d = sqrtf(d2);
                if (d > 0.0001f) {
                    nx = dx / d; ny = dy / d; pen = r - d;
                } else {
                    // 球心落在墙格内部:沿最小穿透面推出
                    float pl = pos->x - left, pr = right - pos->x;
                    float pt = pos->y - top, pb = bottom - pos->y;
                    float mh = fminf(pl, pr), mv = fminf(pt, pb);
                    if (mh < mv) { nx = (pl < pr) ? -1 : 1; ny = 0; pen = mh + r; }
                    else         { nx = 0; ny = (pt < pb) ? -1 : 1; pen = mv + r; }
                }

                // 推出墙体
                pos->x += nx * pen;
                pos->y += ny * pen;

                // 抵消法向速度(保留切向=滑行),法向带微回弹
                float vn = vel->x * nx + vel->y * ny;
                if (vn < 0) {
                    out.hit = true;
                    if (-vn > out.speed) out.speed = -vn;
                    vel->x -= (1.0f + WALL_RESTITUTION) * vn * nx;
                    vel->y -= (1.0f + WALL_RESTITUTION) * vn * ny;
                }
                any = true;
            }
        }
        if (!any) break;
    }
    return out;
}

bool maze_reached_home(const level_t *lvl, vec2_t pos)
{
    vec2_t h = maze_cell_center(lvl->home);
    float dx = pos.x - h.x, dy = pos.y - h.y;
    return (dx * dx + dy * dy) < (GOAL_R * GOAL_R);
}
