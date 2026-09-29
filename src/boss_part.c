/* ---- bosses (levels 5, 10, 15, 20) ----
 * Ported from the web game's bossConfig.ts / megaBossConfig.ts: sizes, health,
 * the 9 positions, move speeds per phase, attack intervals and attack weights.
 * Times in ms become frames at 50 Hz, distances scale to the 224 px playfield.
 * The shapes are drawn as rotating wireframes, which is what the ST is good at.
 */

#define BOSS_MAX_SHOTS 8
#define BOSS_HIT_COOLDOWN 8

/* Boss levels have no bricks, so the brick palette entries are free: the
   wireframe uses one plane (colour 8) and the attacks another (colour 2),
   which halves the cost of every pixel. Flashes recolour the palette. */
#define BOSS_PLANE 3
#define BOSS_PLANE_COL 8
#define BOSS_SHOT_COL 2
#define BOSS_CROSS_COL 4

enum { A_SHOT, A_LASER, A_SUPER, A_SPIRAL, A_CROSS, A_COUNT };

typedef struct {
    u8 w, h;            /* hit box */
    u8 hp;              /* health per phase */
    u8 phases;
    u8 shape;           /* 0 cube, 1 sphere, 2 pyramid, 3 hexagon */
    u16 speed[3];       /* 8.8 px/frame, per phase */
    u16 interval[3];    /* frames between attacks, per phase */
    u16 weight[A_COUNT];/* cumulative, per mille */
    u32 points;
} BossDef;

/* web px/frame at 60 Hz -> ST px/frame at 50 Hz is about x0.32 */
static const BossDef boss_def[4] = {
    /* cube, level 5: 10 hp, 2.5 speed, 3500 ms, shot .72 super .28 */
    { 22, 22, 10, 1, 0, { 202, 202, 202 }, { 175, 175, 175 },
      { 720, 720, 1000, 1000, 1000 }, 5000 },
    /* sphere, level 10: 2 phases, 2.0/3.5 speed, 2500 ms, shot .46 laser .36 super .18 */
    { 22, 22, 10, 2, 1, { 161, 283, 283 }, { 125, 125, 125 },
      { 460, 820, 1000, 1000, 1000 }, 10000 },
    /* pyramid, level 15: 2 phases, 1.8/3.0 speed, 1800 ms,
       shot .25 laser .18 super .29 spiral .10 cross .18 */
    { 24, 22, 10, 2, 2, { 145, 242, 363 }, { 90, 90, 90 },
      { 250, 430, 720, 820, 1000 }, 20000 },
    /* mega boss, level 20: 3 phases, 2.0/3.0/5.5 speed, 3000/2500/2000 ms */
    { 26, 24, 10, 3, 3, { 161, 242, 444 }, { 150, 125, 100 },
      { 300, 500, 700, 800, 1000 }, 100000 },
};

/* the 9 boss positions, as fractions of the playfield (8.8) */
static const u8 boss_pos[9][2] = {
    { 38, 51 }, { 128, 38 }, { 217, 51 },
    { 51, 102 }, { 128, 90 }, { 205, 102 },
    { 38, 154 }, { 128, 141 }, { 217, 154 }
};

typedef struct { s32 x, y, vx, vy; u8 active, size, kind, timer; } Shot;

static const BossDef *bd;
static int boss_active, boss_shape, boss_phase, boss_hp, boss_hp_max;
static int boss_x, boss_y;            /* 8.8, top-left of the box */
static int boss_tx, boss_ty;          /* target position */
static int boss_timer, boss_warn, boss_warn_kind, boss_hitcool, boss_flash;
static int boss_res_left;             /* pyramid resurrections */
static int boss_angle, boss_angle2;
static int laser_x, laser_timer, laser_warn, laser_y0;
static int spiral_left, spiral_dir, spiral_timer;
static Shot bshot[BOSS_MAX_SHOTS];
static int boss_dead_timer;

/* 8.8 sine, 64 steps per turn */
static const s16 sintab[64] = {
    0,25,50,74,98,121,142,162,181,198,213,226,237,245,251,255,256,255,251,245,
    237,226,213,198,181,162,142,121,98,74,50,25,0,-25,-50,-74,-98,-121,-142,-162,
    -181,-198,-213,-226,-237,-245,-251,-255,-256,-255,-251,-245,-237,-226,-213,-198,
    -181,-162,-142,-121,-98,-74,-50,-25
};
#define SIN(a) sintab[(a) & 63]
#define COS(a) sintab[((a) + 16) & 63]

/* wireframe models: signed char coordinates, edges as vertex index pairs */
static const signed char cube_v[8][3] = {
    {-32,-32,-32},{32,-32,-32},{32,32,-32},{-32,32,-32},
    {-32,-32,32},{32,-32,32},{32,32,32},{-32,32,32}
};
static const u8 cube_e[12][2] = {
    {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
};
static const signed char pyr_v[5][3] = {
    {0,-40,0},{-34,30,-34},{34,30,-34},{34,30,34},{-34,30,34}
};
static const u8 pyr_e[8][2] = { {0,1},{0,2},{0,3},{0,4},{1,2},{2,3},{3,4},{4,1} };
static const signed char hex_v[12][3] = {
    {36,-18,0},{18,-18,31},{-18,-18,31},{-36,-18,0},{-18,-18,-31},{18,-18,-31},
    {36,18,0},{18,18,31},{-18,18,31},{-36,18,0},{-18,18,-31},{18,18,-31}
};
static const u8 hex_e[12][2] = {
    {0,1},{1,2},{2,3},{3,4},{4,5},{5,0},
    {6,7},{7,8},{8,9},{9,10},{10,11},{11,6}
};
static const signed char star_v[7][3] = {
    {0,0,0},{0,-40,0},{34,-20,0},{34,20,0},{0,40,0},{-34,20,0},{-34,-20,0}
};
static const u8 star_e[6][2] = { {0,1},{0,2},{0,3},{0,4},{0,5},{0,6} };

/* sphere: three rings of 8 points */
static signed char sph_v[12][3];
static u8 sph_e[12][2];
static int sph_built;
static const u8 model_r[5] = { 32, 36, 40, 36, 40 };   /* largest model coordinate */
static s16 wf_x0, wf_y0, wf_x1, wf_y1;             /* drawn bounding box */

static void build_sphere(void)
{
    int r, i;
    for (r = 0; r < 2; r++)
        for (i = 0; i < 6; i++) {
            int a = i * 10, c = (COS(a) * 36) >> 8, s = (SIN(a) * 36) >> 8;
            signed char *v = sph_v[r * 6 + i];
            if (r == 0) { v[0] = (signed char)c; v[1] = (signed char)s; v[2] = 0; }
            else if (r == 1) { v[0] = (signed char)c; v[1] = 0; v[2] = (signed char)s; }
            else { v[0] = 0; v[1] = (signed char)c; v[2] = (signed char)s; }
            sph_e[r * 6 + i][0] = (u8)(r * 6 + i);
            sph_e[r * 6 + i][1] = (u8)(r * 6 + (i + 1) % 6);
        }
    sph_built = 1;
}

/* The boss area is restored from the background (colour 0) every frame, so the
   wireframe only needs to OR its colour bits in - no read-modify-write of the
   other planes, which is what made this too slow for 50 Hz. */
static inline void pset(u8 *buf, int x, int y, int col)
{
    u16 *p;
    u16 m;
    if (x < PF_X || x >= PF_X2 || y < PF_Y || y >= 200) return;
    p = (u16 *)(buf + y * 160) + (x >> 4) * 4;
    m = (u16)(0x8000 >> (x & 15));
    if (col & 1) p[0] |= m;
    if (col & 2) p[1] |= m;
    if (col & 4) p[2] |= m;
    if (col & 8) p[3] |= m;
}

/* Bresenham that walks the bitplane pointer directly. When the whole line is
   inside the playfield it skips the per-pixel clipping entirely. */
static void line(u8 *buf, int x0, int y0, int x1, int y1, int col)
{
    int dx = x1 - x0, dy = y1 - y0, sx = 1, sy = 1, err, i;
    if (x0 < PF_X || x0 >= PF_X2 || x1 < PF_X || x1 >= PF_X2 ||
        y0 < PF_Y || y0 >= 200 || y1 < PF_Y || y1 >= 200) {
        /* slow, clipped version */
        if (dx < 0) { dx = -dx; sx = -1; }
        if (dy < 0) { dy = -dy; sy = -1; }
        if (dx >= dy) {
            err = dx >> 1;
            for (i = 0; i <= dx; i++) {
                pset(buf, x0, y0, col);
                err -= dy;
                if (err < 0) { err += dx; y0 += sy; }
                x0 += sx;
            }
        } else {
            err = dy >> 1;
            for (i = 0; i <= dy; i++) {
                pset(buf, x0, y0, col);
                err -= dx;
                if (err < 0) { err += dy; x0 += sx; }
                y0 += sy;
            }
        }
        return;
    }
    if (col == BOSS_PLANE_COL) line_plane(buf, x0, y0, x1, y1, BOSS_PLANE);
    else line_asm(buf, x0, y0, x1, y1, col);
}

static void draw_shape(u8 *buf, int cx, int cy, int scale, int shape, int ang, int ang2, int col)
{
    const signed char (*vp)[3];
    const u8 (*ep)[2];
    int nv, ne, i, sc;
    s16 px[16], py[16];
    int sa = SIN(ang), ca = COS(ang);
    int sb = SIN(ang2), cb = COS(ang2);

    switch (shape) {
    case 0: vp = cube_v; ep = cube_e; nv = 8; ne = 12; break;
    case 1: if (!sph_built) build_sphere(); vp = (const signed char (*)[3])sph_v; ep = sph_e; nv = 12; ne = 12; break;
    case 2: vp = pyr_v; ep = pyr_e; nv = 5; ne = 8; break;
    case 4: vp = star_v; ep = star_e; nv = 7; ne = 6; break;
    default: vp = hex_v; ep = hex_e; nv = 12; ne = 12; break;
    }
    /* scale so that the drawn shape is about as wide as the hit box */
    sc = (scale * 32) / model_r[shape];
    wf_x0 = wf_y0 = 32767;
    wf_x1 = wf_y1 = -32768;
    for (i = 0; i < nv; i++) {
        s16 x = vp[i][0], y = vp[i][1], z = vp[i][2], x2, z2, y2;
        x2 = (s16)(((s16)(x * (s16)ca) - (s16)(z * (s16)sa)) >> 8);   /* yaw */
        z2 = (s16)(((s16)(x * (s16)sa) + (s16)(z * (s16)ca)) >> 8);
        y2 = (s16)(((s16)(y * (s16)cb) - (s16)(z2 * (s16)sb)) >> 8);  /* pitch */
        px[i] = (s16)(cx + (((s16)(x2 * (s16)sc)) >> 6));
        py[i] = (s16)(cy + (((s16)(y2 * (s16)sc)) >> 6));
        if (px[i] < wf_x0) wf_x0 = px[i];
        if (px[i] > wf_x1) wf_x1 = px[i];
        if (py[i] < wf_y0) wf_y0 = py[i];
        if (py[i] > wf_y1) wf_y1 = py[i];
    }
    for (i = 0; i < ne; i++)
        line(buf, px[ep[i][0]], py[ep[i][0]], px[ep[i][1]], py[ep[i][1]], col);
}

/* projectiles sit on restored background, so OR-only filling is enough */
static void box_or(u8 *buf, int x, int y, int w, int col)
{
    int x2, gx, ge, g, yy;
    if (x < PF_X || x + w > PF_X2 || y < PF_Y || y + w > 200) return;   /* never write outside */
    x2 = x + w - 1; gx = x >> 4; ge = x2 >> 4;
    u16 mf = (u16)(0xffff >> (x & 15));
    u16 ml = (u16)(0xffff << (15 - (x2 & 15)));
    for (yy = y; yy < y + w; yy++) {
        u16 *row = (u16 *)(buf + yy * 160);
        for (g = gx; g <= ge; g++) {
            u16 m = 0xffff, *p = row + g * 4;
            if (g == gx) m &= mf;
            if (g == ge) m &= ml;
            if (col & 1) p[0] |= m;
            if (col & 2) p[1] |= m;
            if (col & 4) p[2] |= m;
            if (col & 8) p[3] |= m;
        }
    }
}

static int boss_free_shot(void)
{
    int i;
    for (i = 0; i < BOSS_MAX_SHOTS; i++) if (!bshot[i].active) return i;
    return -1;
}

static void boss_fire(int cx, int cy, int ang, int speed, int size, int kind)
{
    int i = boss_free_shot();
    if (i < 0) return;
    if (cx < PF_X) cx = PF_X;
    if (cx > PF_X2 - size) cx = PF_X2 - size;
    if (cy < PF_Y) cy = PF_Y;
    if (cy > 200 - size) cy = 200 - size;
    bshot[i].x = (s32)cx << 8;
    bshot[i].y = (s32)cy << 8;
    bshot[i].vx = (SIN(ang) * speed) >> 8;
    bshot[i].vy = (COS(ang) * speed) >> 8;
    bshot[i].active = 1;
    bshot[i].size = (u8)size;
    bshot[i].kind = (u8)kind;
    bshot[i].timer = 0;
}

static int boss_cx(void) { return (boss_x >> 8) + bd->w / 2; }
static int boss_cy(void) { return (boss_y >> 8) + bd->h / 2; }

static void boss_pick_target(void)
{
    int p = rnd() % 9;
    boss_tx = (s32)(PF_X + (((int)boss_pos[p][0] * (PF_X2 - PF_X)) >> 8) - bd->w / 2) << 8;
    boss_ty = (s32)(PF_Y + (((int)boss_pos[p][1] * 150) >> 8)) << 8;
}

static void boss_start(int level)
{
    int idx = level == 5 ? 0 : level == 10 ? 1 : level == 15 ? 2 : 3;
    int i;
    bd = &boss_def[idx];
    boss_shape = bd->shape;
    boss_phase = 0;
    boss_hp = boss_hp_max = bd->hp;
    boss_res_left = idx == 2 ? 3 : 0;      /* pyramid resurrections */
    boss_active = 1;
    boss_dead_timer = 0;
    palette[BOSS_PLANE_COL] = 0x777;
    palette[BOSS_SHOT_COL] = 0x700;
    palette[BOSS_CROSS_COL] = 0x750;
    set_palette();
    boss_x = (s32)(PF_X + (PF_X2 - PF_X - bd->w) / 2) << 8;
    boss_y = (s32)(PF_Y + 20) << 8;
    boss_pick_target();
    boss_timer = bd->interval[0];
    boss_warn = boss_warn_kind = boss_hitcool = boss_flash = 0;
    laser_timer = laser_warn = 0;
    spiral_left = 0;
    for (i = 0; i < BOSS_MAX_SHOTS; i++) bshot[i].active = 0;
    sfx_play(SFX_BOSSINTRO);
}

static int pick_attack(void)
{
    u16 r = (u16)(rnd() % 1000);
    int i;
    for (i = 0; i < A_COUNT; i++) if (r < bd->weight[i]) return i;
    return A_SHOT;
}

static int aim_angle(int cx, int cy)
{
    /* coarse aim at the paddle: 64-step angle, 0 = straight down */
    int dx = (paddle_x + paddle_w / 2) - cx, dy = PAD_Y - cy, a, best = 0;
    s32 bestd = 0x7fffffffL;
    if (dy < 1) dy = 1;
    for (a = -16; a <= 16; a++) {
        s32 d = (s32)SIN(a) * dy - (s32)COS(a) * dx;
        if (d < 0) d = -d;
        if (d < bestd) { bestd = d; best = a; }
    }
    return best;
}

static void boss_attack(void)
{
    int kind = pick_attack(), cx = boss_cx(), cy = boss_cy() + bd->h / 2;
    switch (kind) {
    case A_SHOT:
        boss_fire(cx, cy, aim_angle(cx, cy), 168, 4, 0);      /* was 128: too easy to crawl to a near-90 deg angle and creep */
        sfx_play(SFX_BOSSSHOT);
        break;
    case A_LASER:
        laser_warn = 40;                                      /* 800 ms warning */
        laser_x = cx;
        sfx_play(SFX_LASERCHARGE);
        break;
    case A_SUPER:
        boss_warn = 30;                                       /* 600 ms warning */
        boss_warn_kind = A_SUPER;
        sfx_play(SFX_SUPERCHARGE);
        break;
    case A_SPIRAL:
        spiral_left = 10;      /* web has 16; fewer keeps the ST at 50 Hz */
        spiral_dir = (int)(rnd() & 63);
        spiral_timer = 0;
        break;
    case A_CROSS: {
        int i, base = aim_angle(cx, cy);
        for (i = -1; i <= 1; i++) boss_fire(cx, cy, base + i * 5, 144, 6, 1);
        sfx_play(SFX_BOSSSHOT);
        break;
    }
    }
}

static void boss_damage(int n)
{
    if (boss_hitcool || !boss_active || boss_dead_timer) return;
    boss_hitcool = BOSS_HIT_COOLDOWN;
    boss_flash = 4;
    boss_hp -= n;
    if (boss_hp > 0) { sfx_play(SFX_BOSSHIT); return; }
    if (boss_phase + 1 < bd->phases) {                 /* next phase: angrier */
        boss_phase++;
        boss_hp = boss_hp_max;
        sfx_play(SFX_BOSSPHASE);
        return;
    }
    if (boss_res_left > 0) {                           /* pyramid resurrects smaller */
        boss_res_left--;
        boss_hp = boss_hp_max = 5;
        score += 3000;
        sfx_play(SFX_BOSSPHASE);
        return;
    }
    boss_hp = 0;
    boss_dead_timer = 100;
    score += bd->points;
    sfx_play(SFX_BOSSDEFEAT);
}

/* returns 1 when the player loses the ball to a boss attack */
static int boss_update(void)
{
    int cx, cy, i, sp = bd->speed[boss_phase], hit_paddle = 0;
    if (boss_dead_timer) {
        if (--boss_dead_timer == 0) { boss_active = 0; bricks_left = 0; }
        return 0;
    }
    /* move toward the target position, pick a new one on arrival */
    {
        int dx = boss_tx - boss_x, dy = boss_ty - boss_y;
        int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        if (ax < sp && ay < sp) { boss_x = boss_tx; boss_y = boss_ty; boss_pick_target(); }
        else {
            if (ax > sp) boss_x += dx > 0 ? sp : -sp; else boss_x = boss_tx;
            if (ay > sp) boss_y += dy > 0 ? sp : -sp; else boss_y = boss_ty;
        }
    }
    boss_angle = (boss_angle + 1) & 63;
    if ((frame_no & 1) == 0) boss_angle2 = (boss_angle2 + 1) & 63;
    if (boss_hitcool) boss_hitcool--;
    if (boss_flash) boss_flash--;
    cx = boss_cx(); cy = boss_cy() + bd->h / 2;

    /* attack timing */
    if (boss_warn) {
        if (--boss_warn == 0 && boss_warn_kind == A_SUPER) {
            for (i = 0; i < 8; i++) boss_fire(cx - bd->h / 2, cy - bd->h / 2, i * 8, 150, 4, 0);
            sfx_play(SFX_BOSSSHOT);
        }
    } else if (spiral_left) {
        if (++spiral_timer >= 2) {
            spiral_timer = 0;
            boss_fire(cx, cy, spiral_dir, 130, 4, 0);
            spiral_dir = (spiral_dir + 6) & 63;
            if (--spiral_left == 0) sfx_play(SFX_BOSSSHOT);
        }
    } else if (--boss_timer <= 0) {
        boss_timer = bd->interval[boss_phase];
        boss_attack();
    }

    /* laser */
    if (laser_warn) {
        if (--laser_warn == 0) {
            laser_timer = 25;
            laser_y0 = boss_cy() + bd->h / 2;
            sfx_play(SFX_LASERFIRE);
        }
    } else if (laser_timer) {
        if (--laser_timer == 0)                 /* erase the beam in both buffers */
            dirty_both(laser_x - 2, laser_y0, 5, 200 - laser_y0);
        if (paddle_x < laser_x + 2 && paddle_x + paddle_w > laser_x - 2) hit_paddle = 1;
    }

    /* projectiles */
    for (i = 0; i < BOSS_MAX_SHOTS; i++) {
        Shot *s = &bshot[i];
        int x, y;
        if (!s->active) continue;
        if (s->kind == 1) {                        /* cross: changes course, pauses */
            if (++s->timer > 40) {
                s->timer = 0;
                if (rnd() & 1) { s->vx = -s->vx; }
                else { s->vx = (s->vx * 3) >> 2; s->vy = (s->vy * 3) >> 2; }
            }
        }
        s->x += s->vx;
        s->y += s->vy;
        x = (int)(s->x >> 8); y = (int)(s->y >> 8);
        if (x < PF_X || x > PF_X2 - s->size || y < PF_Y || y > 200 - s->size) { s->active = 0; continue; }
        if (y + s->size >= PAD_Y && y <= PAD_Y + PAD_H &&
            x + s->size > paddle_x && x < paddle_x + paddle_w) {
            s->active = 0;
            if (has_shield) { has_shield = 0; sfx_play(SFX_BOUNCE); }
            else hit_paddle = 1;
        }
    }
    return hit_paddle;
}

/* ball against the boss box */
static int boss_ball_hit(Ball *b)
{
    int x = (int)(b->x >> 8), y = (int)(b->y >> 8);
    int bx = boss_x >> 8, by = boss_y >> 8;
    if (!boss_active || boss_dead_timer) return 0;
    if (x + BALL_S <= bx || x >= bx + bd->w || y + BALL_S <= by || y >= by + bd->h) return 0;
    boss_damage(1);
    return 1;
}

static void boss_draw(u8 *buf)
{
    int bx = boss_x >> 8, by = boss_y >> 8, y;
    int col = BOSS_PLANE_COL;
    int i;
    if (!boss_active) return;
    {   /* flashes are palette changes, not colour changes */
        u16 c = boss_phase == 0 ? 0x777 : boss_phase == 1 ? 0x766 : 0x755;
        if (boss_flash) c = 0x700;
        if (boss_dead_timer) c = (boss_dead_timer & 4) ? 0x700 : 0x777;
        else if (boss_warn && (frame_no & 2)) c = 0x740;
        ((u16 *)0xff8240)[BOSS_PLANE_COL] = c;
    }
    draw_shape(buf, bx + bd->w / 2, by + bd->h / 2, bd->w, boss_shape, boss_angle, boss_angle2, col);
    add_dirty(back, wf_x0 - 1, wf_y0 - 1, wf_x1 - wf_x0 + 3, wf_y1 - wf_y0 + 3);

    if (laser_warn) {                            /* warning: dotted beam */
        for (y = by + bd->h; y < 200; y += 4) pset(buf, laser_x, y, BOSS_SHOT_COL);
        add_dirty(back, laser_x - 1, by, 3, 200 - by);
    } else if (laser_timer) {
        /* the beam is static while it burns: draw it into each buffer without
           adding a dirty rect, and erase both buffers once when it ends */
        int x = laser_x - 1;
        u16 m0, m1;
        u16 *p;
        if (x < PF_X) x = PF_X;
        if (x > PF_X2 - 3) x = PF_X2 - 3;
        p = (u16 *)(buf + laser_y0 * 160) + (x >> 4) * 4;
        m0 = (u16)(0xe000 >> (x & 15));
        m1 = (x & 15) > 13 ? (u16)(0xe000 << (16 - (x & 15))) : 0;
        for (y = laser_y0; y < 200; y++, p += 80) {
            p[1] |= m0;                         /* colour 2 = plane 1 */
            if (m1) p[5] |= m1;
        }
    }
    for (i = 0; i < BOSS_MAX_SHOTS; i++) {
        Shot *s = &bshot[i];
        int x, y;
        if (!s->active) continue;
        x = (int)(s->x >> 8); y = (int)(s->y >> 8);
        if (x < PF_X || x + s->size > PF_X2 || y < PF_Y || y + s->size > 200) continue;
        box_or(buf, x, y, s->size, s->kind ? BOSS_CROSS_COL : BOSS_SHOT_COL);
        add_dirty(back, x, y, s->size, s->size);
    }
}

static int boss_hp_shown = -1;

static void draw_boss_hud(void)
{
    int n;
    if (!boss_active) {
        if (boss_hp_shown >= 0) {
            fill_rect(bg, PANEL_X, 52, 96, 16, COL_DARK);
            dirty_both(PANEL_X, 52, 96, 16);
            boss_hp_shown = -1;
        }
        return;
    }
    n = boss_hp * 10 / (boss_hp_max ? boss_hp_max : 1);
    if (n == boss_hp_shown) return;
    boss_hp_shown = n;
    fill_rect(bg, PANEL_X, 52, 96, 16, COL_DARK);
    draw_text(bg, PANEL_X + 8, 52, "BOSS", COL_EXPL, COL_DARK);
    fill_rect(bg, PANEL_X + 8, 62, 80, 4, COL_METAL);
    if (n > 0) fill_rect(bg, PANEL_X + 8, 62, n * 8, 4, COL_EXPL);
    dirty_both(PANEL_X, 52, 96, 16);
}
