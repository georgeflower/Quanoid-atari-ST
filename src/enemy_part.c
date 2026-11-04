/* ---- enemies ----
 * Rules from the web game: a cube from level 1, a sphere from level 3 (50 %),
 * a pyramid from level 6 (30 %, slow and 3 hits) and a star on levels
 * 3/6/9/12/16/19 (15 %). One spawns every 30 s, 5 s less per level down to a
 * floor of 15 s, and each spawn is 30 % faster than the last, up to double,
 * counted over the first 5. None during boss levels. They don't drop
 * power-ups here.
 */

#define MAX_ENEMIES 3
#define ENEMY_SIZE 12
#ifndef ENEMY_COL
#define ENEMY_COL COL_WHITE
#endif

typedef struct {
    s32 x, y, vx, vy;
    u8 active, shape, hits;
    s16 angle, fire;
} Enemy;

/* enemy shots. Web rules: the pyramid fires a bullet within +-80 degrees of
   down, everything else drops a bomb straight down, on a random timer of
   7-12 s at level 1, falling 0.5 s per level to 4-8 s, then 3-7 s from 9. */
#define MAX_BOMBS 6
#define BOMB_SIZE 3
typedef struct { s32 x, y, vx, vy; u8 active; } Bomb;
static Bomb bombs[MAX_BOMBS];
static int enemy_hit_paddle;

static Enemy enemies[MAX_ENEMIES];
static int enemy_timer, enemy_spawns;
#ifdef TEST
static int enemy_shot_in;
static void save_shot(u8 *buf, const char *what);
#endif
static const u8 star_levels[6] = { 3, 6, 9, 12, 16, 19 };

static int fire_delay(void)
{
    int lvl = level_idx + 1, lo, hi;
    if (lvl <= 8) {
        lo = 350 - (lvl - 1) * 25; if (lo < 200) lo = 200;      /* 7 s -> 4 s */
        hi = 600 - (lvl - 1) * 25; if (hi < 400) hi = 400;      /* 12 s -> 8 s */
    } else {
        lo = 150; hi = 350;                                     /* 3-7 s */
    }
    return lo + (int)(rnd() % (u16)(hi - lo + 1));
}

static void enemies_reset(void)
{
    int i;
    for (i = 0; i < MAX_ENEMIES; i++) enemies[i].active = 0;
    for (i = 0; i < MAX_BOMBS; i++) bombs[i].active = 0;
    enemy_hit_paddle = 0;
    enemy_spawns = 0;
#ifdef ENEMYDEBUG
    enemy_timer = 100;
#elif defined(TEST) && !defined(REALSPAWN)
    enemy_timer = 150;                           /* test builds spawn sooner */
#else
    enemy_timer = 30 * 50;                       /* first one after 30 s */
#endif
}

static int enemy_interval(void)
{
    int secs = 30 - (level_idx) * 5;             /* level_idx is 0-based */
    if (secs < 15) secs = 15;
    return secs * 50;
}

static void enemy_spawn(void)
{
    int i, lvl = level_idx + 1, shape = 0, speed, k, star = 0;
    Enemy *e = 0;
    for (i = 0; i < MAX_ENEMIES; i++)
        if (!enemies[i].active) { e = &enemies[i]; break; }
    if (!e) return;
    for (k = 0; k < 6; k++) if (star_levels[k] == lvl) star = 1;
    if (star && (rnd() % 100) < 15) shape = 4;
    else if (lvl >= 6 && (rnd() % 100) < 30) shape = 2;
    else if (lvl >= 3 && (rnd() % 100) < 50) shape = 1;
    else shape = 0;

    /* speed grows 30 % per spawn, counted over the first five */
    k = enemy_spawns < 5 ? enemy_spawns : 5;
    speed = (shape == 2 ? 60 : 110) * (10 + k * 3) / 10;
    enemy_spawns++;

    e->shape = (u8)shape;
    e->hits = (u8)(shape == 2 ? 3 : 1);
    e->x = (s32)(PF_X + 4 + (int)(rnd() % (PF_X2 - PF_X - ENEMY_SIZE - 8))) << 8;
    e->y = (s32)(PF_Y + 8 + (int)(rnd() % 120)) << 8;      /* anywhere in the field */
    e->vx = (rnd() & 1) ? speed : -speed;
    e->vy = speed / 2;
    e->angle = 0;
    e->fire = (s16)fire_delay();
    e->active = 1;
    sfx_play(SFX_ENEMYSPAWN);
#ifdef TEST
    enemy_shot_in = 400;   /* shot after the enemy has had time to fire */
    { int n = 0, k; for (k = 0; k < MAX_ENEMIES; k++) n += enemies[k].active;
      log_kv(" live=", (u32)n); }
    { char b[12]; log_str("ENEMY shape="); log_str(utoa((u32)shape, b + 11)); log_kv(" x=", (u32)(e->x >> 8)); log_str("\n"); }
#endif
}

static void enemy_kill(Enemy *e)
{
    e->active = 0;
    enemy_timer = enemy_interval();          /* next one starts its wait now */
#ifdef TEST
    { int n = 0, k; for (k = 0; k < MAX_ENEMIES; k++) n += enemies[k].active;
      log_kv("ENEMY killed, left=", (u32)n); log_str("\n"); }
#endif
    score += 500;
    sfx_play(SFX_ENEMYDIE);
}

static void enemies_update(void)
{
    int i;
    if (boss_active) return;                     /* not during boss levels */
    {   /* at most MAX_ENEMIES alive; the countdown for the next one only runs
           when there is a free slot, and restarts whenever one is killed */
        int n = 0;
        for (i = 0; i < MAX_ENEMIES; i++) n += enemies[i].active;
        if (n < MAX_ENEMIES && --enemy_timer <= 0) {
#ifdef ENEMYDEBUG
            enemy_timer = 100;
#else
            enemy_timer = enemy_interval();
#endif
            enemy_spawn();
        }
    }
#ifdef TEST
    if (enemy_shot_in > 0 && --enemy_shot_in == 0) save_shot(scr[1 - back], "enemy");
#endif
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &enemies[i];
        int x, y;
        if (!e->active) continue;
        e->x += e->vx;
        e->y += e->vy;
        x = (int)(e->x >> 8); y = (int)(e->y >> 8);
        if (x < PF_X) { e->x = (s32)PF_X << 8; e->vx = -e->vx; }
        if (x > PF_X2 - ENEMY_SIZE) { e->x = (s32)(PF_X2 - ENEMY_SIZE) << 8; e->vx = -e->vx; }
        if (y < PF_Y) { e->y = (s32)PF_Y << 8; e->vy = -e->vy; }
        if (y > PAD_Y - ENEMY_SIZE - 8) { e->y = (s32)(PAD_Y - ENEMY_SIZE - 8) << 8; e->vy = -e->vy; }
        e->angle = (s16)((e->angle + 1) & 63);
        if (--e->fire <= 0) {
            int b;
            e->fire = (s16)fire_delay();
            for (b = 0; b < MAX_BOMBS; b++) {
                if (bombs[b].active) continue;
                bombs[b].x = (s32)(x + ENEMY_SIZE / 2 - BOMB_SIZE / 2) << 8;
                bombs[b].y = (s32)(y + ENEMY_SIZE) << 8;
                if (e->shape == 2) {                 /* pyramid: angled bullet */
                    int a = (int)(rnd() % 29) - 14;  /* about +-80 degrees */
                    bombs[b].vx = (SIN(a) * 307) >> 8;
                    bombs[b].vy = (COS(a) * 307) >> 8;
                    sfx_play(SFX_BOSSSHOT);
                } else {                             /* bomb straight down */
                    bombs[b].vx = 0;
                    bombs[b].vy = 230;
                    sfx_play(SFX_BOMBDROP);
                }
                bombs[b].active = 1;
                break;
            }
        }
    }
    for (i = 0; i < MAX_BOMBS; i++) {
        Bomb *b = &bombs[i];
        int x, y;
        if (!b->active) continue;
        b->x += b->vx;
        b->y += b->vy;
        x = (int)(b->x >> 8); y = (int)(b->y >> 8);
        if (x < PF_X || x > PF_X2 - BOMB_SIZE || y > 200 - BOMB_SIZE) { b->active = 0; continue; }
        if (y + BOMB_SIZE >= PAD_Y && y <= PAD_Y + PAD_H &&
            x + BOMB_SIZE > paddle_x && x < paddle_x + paddle_w) {
            b->active = 0;
            if (has_shield) { has_shield = 0; sfx_play(SFX_BOUNCE); }
            else enemy_hit_paddle = 1;
        }
    }
}

/* ball against enemies: bounces and damages, like a brick */
static int enemy_ball_hit(Ball *b)
{
    int i, x = (int)(b->x >> 8), y = (int)(b->y >> 8);
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &enemies[i];
        int ex, ey;
        if (!e->active) continue;
        ex = (int)(e->x >> 8); ey = (int)(e->y >> 8);
        if (x + BALL_S <= ex || x >= ex + ENEMY_SIZE || y + BALL_S <= ey || y >= ey + ENEMY_SIZE) continue;
        if (--e->hits == 0) enemy_kill(e);
        else sfx_play(SFX_ENEMYHIT);
        return 1;
    }
    return 0;
}

static int enemy_bullet_hit(int bx, int by)
{
    int i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &enemies[i];
        int ex, ey;
        if (!e->active) continue;
        ex = (int)(e->x >> 8); ey = (int)(e->y >> 8);
        if (bx + 2 <= ex || bx >= ex + ENEMY_SIZE || by + 4 <= ey || by >= ey + ENEMY_SIZE) continue;
        if (--e->hits == 0) enemy_kill(e);
        else sfx_play(SFX_ENEMYHIT);
        return 1;
    }
    return 0;
}

/* 12x12 shaded, dithered rotation sprites (gfxd_enemy in sprites.h, 8 frames
   per shape). Tones are outline, shadow, body, highlight; the table maps
   them to palette entries: cube red, sphere light blue, pyramid purple
   (3 hits purple, 2 hits grey, last hit red and blinking), star gold. */
static Sprite spr_enemy[5][8][3];

static void build_enemy_sprites(void)
{
    int t, f, v;
    build_boss_sprites();
    for (t = 0; t < 5; t++) {
        if (t == 3) continue;
        for (f = 0; f < 8; f++)
            for (v = 0; v < 3; v++) {
                Sprite *sp = &spr_enemy[t][f][v];
                sp->w = 12; sp->h = 12; sp->ng = 1; sp->cap = 60;
                sp->d = (u16 *)gfxd_enemy[t][f][v];
            }
    }
}

/* ---- pre-shifted blit tables: ball, fireball and every enemy sprite ----
 * Built once at startup from the sprites make_sprite() already produced.
 * The 16 x-alignments of each sprite are computed here, one time, instead
 * of being recomputed by barrel-shifting on every single draw call - see
 * blitfast.S / MICRO.LOG for the reasoning and the measured numbers. */
typedef struct { const u16 *data; u8 h; } PreShift;

/* ball(2x1280w)+enemy(30 variants x1280w)+capsule(9 types x1280w) = 51200
   words exactly; a margin is kept in case a future sprite is added here. */
static u16 pre_pool[12000];
static int pre_used;

static void build_preshift(PreShift *ps, const Sprite *src)
{
    int shift, row;
    u16 *base;
    if (src->ng != 1) return;          /* this fast path is single-group sprites only */
    ps->h = (u8)src->h;
    base = pre_pool + pre_used;
    ps->data = base;
    pre_used += 16 * src->h * 10;
#ifdef TEST
    if (pre_used > (int)(sizeof pre_pool / sizeof pre_pool[0])) log_str("PRE_POOL OVERFLOW\n");
#endif
    for (shift = 0; shift < 16; shift++) {
        for (row = 0; row < src->h; row++) {
            const u16 *sd = src->d + row * 5;   /* mask, p0, p1, p2, p3 */
            u16 *out = base + (shift * src->h + row) * 10;
            int i;
            for (i = 0; i < 5; i++) {
                u32 v = ((u32)sd[i] << 16) >> shift;
                out[i * 2] = (u16)(v >> 16);
                out[i * 2 + 1] = (u16)v;
            }
        }
    }
}

static PreShift ps_ballf[BALL_FR], ps_firef[BALL_FR], ps_ball, ps_fireball, ps_enemy[5][2][3] __attribute__((unused)), ps_cap[PU_COUNT] __attribute__((unused));

static void build_all_preshift(void)
{
    int t;
    build_preshift(&ps_ball, &spr_ball);
    build_preshift(&ps_fireball, &spr_fireball);
    for (t = 0; t < BALL_FR; t++) {
        build_preshift(&ps_ballf[t], &spr_ballf[t]);
        build_preshift(&ps_firef[t], &spr_firef[t]);
    }
}

/* draw a pre-shifted sprite: same position semantics as blit() */
static void blit_ps(u8 *buf, int x, int y, const PreShift *ps)
{
    int sh = x & 15, gx = x >> 4;
    blit_pre(buf, gx, y, ps->h, ps->data + sh * ps->h * 10);
}

static void enemies_draw(u8 *buf)
{
    int i;
    for (i = 0; i < MAX_BOMBS; i++) {
        Bomb *b = &bombs[i];
        int x, y;
        if (!b->active) continue;
        x = (int)(b->x >> 8); y = (int)(b->y >> 8);
        fill_rect(buf, x, y, BOMB_SIZE, BOMB_SIZE, COL_EXPL);
        add_dirty(back, x, y, BOMB_SIZE, BOMB_SIZE);
    }
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &enemies[i];
        int x, y;
        if (!e->active) continue;
        x = (int)(e->x >> 8); y = (int)(e->y >> 8);
        {   /* damage state: 3 hits -> 0, 2 -> 1, last hit -> 2 (blinks) */
            int v = e->hits >= 3 ? 0 : e->hits == 2 ? 1 : 2;
            if (v == 2 && e->shape == 2 && (frame_no & 8)) v = 1;   /* blink */
            blit(buf, x, y, &spr_enemy[e->shape][(e->angle >> 2) & 7][v]);
        }
        add_dirty(back, x, y, ENEMY_SIZE, ENEMY_SIZE);
    }
}
