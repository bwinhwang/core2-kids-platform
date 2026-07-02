#include "maze.h"
#include "tuning.h"
#include <math.h>

// ── 关卡库(2026-07-02 三写:16×12 网格,带分叉/死胡同/环路的真迷宫)──────────
// 幼儿已掌握基础控球(前两版无岔路地图毕业),难度来源升级为:
//   ①分叉与死胡同(走错要折返,但零失败——死胡同只是"还没到")
//   ②环路(两条都通,给"选路"而非"对错")  ③后期 1 格窄走廊(20px)要求精细控制
// L1~L4 保持 2 格宽走廊(40px 旧手感):L1/L2 无岔路(Easy 档),L4 引入第一个岔路。
// L5 起 1 格宽;星不再必在最短路上——放进支路/死胡同,引诱探索(星本就非过关必需,§4.5)。
static const level_t k_levels[] = {
    {   // L1 · 草地(Tier 0,宽 L 形,无岔路,引导 ON)—— 开局固定关
        .id = 1, .world = WORLD_MEADOW, .tier = 0, .guide = GUIDE_ON,
        .grid = {
            "################",
            "#.............H#",
            "#..............#",
            "#..#############",
            "#..#############",
            "#..#############",
            "#..#############",
            "#..#############",
            "#..#############",
            "#..#############",
            "#S.#############",
            "################",
        },
        .start = {1, 10}, .home = {14, 1}, .n_stars = 0,
    },
    {   // L2 · 海边(Tier 1,宽 U 形,无岔路)—— Easy 档第二关
        .id = 2, .world = WORLD_SEASIDE, .tier = 1, .guide = GUIDE_FAINT,
        .grid = {
            "################",
            "#S.###########H#",
            "#..##########..#",
            "#..##########..#",
            "#..##########..#",
            "#..##########..#",
            "#..##########..#",
            "#..##########..#",
            "#..##########..#",
            "#..............#",
            "#..............#",
            "################",
        },
        .start = {1, 1}, .home = {14, 1}, .n_stars = 0,
    },
    {   // L3 · 星空(Tier 1,宽蛇形 4 弯,1 星在途中)
        .id = 3, .world = WORLD_STARRY, .tier = 1, .guide = GUIDE_FAINT,
        .grid = {
            "################",
            "#S.............#",
            "#..............#",
            "#############..#",
            "#############..#",
            "#......*.......#",
            "#..............#",
            "#..#############",
            "#..#############",
            "#..............#",
            "#.............H#",
            "################",
        },
        .start = {1, 1}, .home = {14, 10}, .stars = {{7, 5}}, .n_stars = 1,
    },
    {   // L4 · 糖果(Tier 1,宽 L 形 + 第一个岔路:垂下的死胡同支路,星在支路底)
        .id = 4, .world = WORLD_CANDY, .tier = 1, .guide = GUIDE_FAINT,
        .grid = {
            "################",
            "#.............H#",
            "#..............#",
            "#..####..#######",
            "#..####..#######",
            "#..####..#######",
            "#..####.*#######",
            "#..#############",
            "#..#############",
            "#..#############",
            "#S.#############",
            "################",
        },
        .start = {1, 10}, .home = {14, 1}, .stars = {{8, 6}}, .n_stars = 1,
    },
    {   // L5 · 草地(Tier 2,首个 1 格窄走廊:6 弯 + 3 条短死胡同,星在主路)
        .id = 5, .world = WORLD_MEADOW, .tier = 2, .guide = GUIDE_OFF,
        .grid = {
            "################",
            "#.....###.....##",
            "#.#.#.###.###.##",
            "#.#.#.###.###H##",
            "#.###.###.######",
            "#.###..*..######",
            "#.###.###.######",
            "#.###.###.######",
            "#.##############",
            "#S##############",
            "################",
            "################",
        },
        .start = {1, 9}, .home = {13, 3}, .stars = {{7, 5}}, .n_stars = 1,
    },
    {   // L6 · 海边(Tier 2,6 弯 + 长假路(通到左下死角)+ 星在顶部支路引诱绕远)
        .id = 6, .world = WORLD_SEASIDE, .tier = 2, .guide = GUIDE_OFF,
        .grid = {
            "################",
            "#H###########*##",
            "#.###########.##",
            "#.#.......###.##",
            "#.###.###.###.##",
            "#.###.###.....##",
            "#.###.###.###.##",
            "#.....###.###.##",
            "#########.###.##",
            "#####.....###S##",
            "################",
            "################",
        },
        .start = {13, 9}, .home = {1, 1}, .stars = {{13, 1}}, .n_stars = 1,
    },
    {   // L7 · 星空(Tier 2,梳齿迷宫:上下走廊 + 5 根假齿,唯一通道在最右,星在最深齿底)
        .id = 7, .world = WORLD_STARRY, .tier = 2, .guide = GUIDE_OFF,
        .grid = {
            "################",
            "#S............##",
            "###.###.###.#.##",
            "###.###.###.#.##",
            "###.###.#####.##",
            "###.###.#.###.##",
            "#######.#.###.##",
            "#####.#*#.###.##",
            "#####.###.###.##",
            "#H............##",
            "################",
            "################",
        },
        .start = {1, 1}, .home = {1, 9}, .stars = {{7, 7}}, .n_stars = 1,
    },
    {   // L8 · 糖果(Tier 2,回字环路:上短下长两条通路,星在长弧上;2 个小死胡同)
        .id = 8, .world = WORLD_CANDY, .tier = 2, .guide = GUIDE_OFF,
        .grid = {
            "################",
            "#######.###..H##",
            "#######.###.####",
            "###.........####",
            "###.#######.####",
            "#...#######.####",
            "#.#.#######.####",
            "#.#....*....####",
            "#.#######.######",
            "#S#######.######",
            "################",
            "################",
        },
        .start = {1, 9}, .home = {13, 1}, .stars = {{7, 7}}, .n_stars = 1,
    },
    {   // L9 · 草地(Tier 3,真迷宫:8 弯主路 + 星星支路成环 + 3 条死胡同尾巴)
        .id = 9, .world = WORLD_MEADOW, .tier = 3, .guide = GUIDE_OFF,
        .grid = {
            "################",
            "#S......#.....##",
            "#.#####.#.###.##",
            "#.###*..#.#.#.##",
            "#.###.###.#.#.##",
            "#...#.#...#.#.##",
            "#.#.#.#.###.#.##",
            "#.#.#.#.#.#.#.##",
            "#.#.#.#.#.#.#.##",
            "#.#.........#H##",
            "################",
            "################",
        },
        .start = {1, 1}, .home = {13, 9}, .stars = {{5, 3}}, .n_stars = 1,
    },
    {   // L10 · 海边(Tier 3,回旋滑雪:5 间全宽房间 + 1 格窄门交错,星在死角要专程去拿)
        .id = 10, .world = WORLD_SEASIDE, .tier = 3, .guide = GUIDE_OFF,
        .grid = {
            "################",
            "#S.............#",
            "###########.####",
            "#.............*#",
            "####.###########",
            "#..............#",
            "###########.####",
            "#*.............#",
            "####.###########",
            "#.............H#",
            "################",
            "################",
        },
        .start = {1, 1}, .home = {14, 9}, .stars = {{14, 3}, {1, 7}}, .n_stars = 2,
    },
    {   // L11 · 星空(Tier 3,双环迷宫:左右两条大环 + 死胡同尾,两星都藏在支路里)
        .id = 11, .world = WORLD_STARRY, .tier = 3, .guide = GUIDE_OFF,
        .grid = {
            "################",
            "#.........###S##",
            "#.#####.#.###.##",
            "#*..#...#...#.##",
            "###.#.###.#.#.##",
            "#...#...#.#...##",
            "#.###.#.#.###.##",
            "#.###.#...#.#.##",
            "#.###.#####.#.##",
            "#H....#......*##",
            "################",
            "################",
        },
        .start = {13, 1}, .home = {1, 9}, .stars = {{1, 3}, {13, 9}}, .n_stars = 2,
    },
    {   // L12 · 糖果(Tier 3,终章:11 弯回旋主路 + 中心星死胡同 + 深井星支路 + 右侧环)
        .id = 12, .world = WORLD_CANDY, .tier = 3, .guide = GUIDE_OFF,
        .grid = {
            "################",
            "#...........#H##",
            "#.###.#.#.#.#.##",
            "#.#...#.#.#.#.##",
            "#.#.###.#.#.#.##",
            "#.#.###*#...#.##",
            "#.#.#######.#.##",
            "#*#...#####...##",
            "#####.#####.####",
            "#S....#.......##",
            "################",
            "################",
        },
        .start = {1, 9}, .home = {13, 1}, .stars = {{7, 5}, {1, 7}}, .n_stars = 2,
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
