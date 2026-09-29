/* ---- sound effects: YM2149, 3 voices, ticked from the TOS VBL queue ---- */
#include "sfx.h"

typedef struct { const u16 *p; s16 left; u8 prio; u16 pitch; } Voice;
static Voice voices[3];
#ifdef NOSOUND
static u8 sfx_enabled = 0;
#else
static u8 sfx_enabled = 1;
#endif
static u32 sfx_plays, sfx_ticks;
#ifdef SNDDIAG
static u8 last_vol[3];
#endif
#ifdef SFX_VBL
static long vbl_slot;
#endif
static u8 old_conterm;
static int combo;
static void sfx_draw_state(void);

static u8 * volatile ym_sel = (u8 *)0xff8800;
static u8 * volatile ym_dat = (u8 *)0xff8802;
static u8 * volatile conterm_p = (u8 *)0x484;

static void ym(int reg, int val)
{
    long sr = irq_off();          /* TOS's Timer C sound code also uses the PSG */
    *ym_sel = (u8)reg;
    *ym_dat = (u8)val;
    irq_restore(sr);
}

/* TOS runs its own sound list from a 200 Hz timer (keyclick, bell, XBIOS
   Dosound). Feeding it a terminating sequence stops it writing the PSG. */
static const char dosound_stop[] = { (char)0xff, 0x00 };

static void sfx_silence(void)
{
    ym(8, 0); ym(9, 0); ym(10, 0);
    ym(7, 0xff);          /* all tone/noise off, ports A/B stay outputs */
}

/* called 50 times per second from the VBL interrupt */
static void sfx_tick(void)
{
    int i, mix = 0x3f, noise = 0;
    sfx_ticks++;
#ifdef DOSOUND
    return;                                /* TOS is playing the sounds */
#endif
#ifndef DOSOUND
    if ((sfx_ticks & 63) == 0) dosound(dosound_stop);   /* keep TOS off the PSG */
#endif
    for (i = 0; i < 3; i++) {
        Voice *v = &voices[i];
        if (v->left > 0) {
            u16 w0 = v->p[0], w1 = v->p[1];
            u16 per = w0 & 0x0fff;
            if (v->pitch != 256) per = (u16)(((u32)per * v->pitch) >> 8);
            if (per < 1) per = 1;
            ym(i * 2, per & 0xff);
            ym(i * 2 + 1, per >> 8);
            ym(8 + i, sfx_enabled ? (w1 >> 8) : 0);
#ifdef SNDDIAG
            last_vol[i] = (u8)(w1 >> 8);
#endif
            if (w0 & 0x1000) mix &= ~(1 << i);
            if (w0 & 0x2000) { mix &= ~(8 << i); noise = w1 & 31; }
            v->p += 2;
            v->left--;
        } else {
            ym(8 + i, 0);
        }
    }
    if (noise) ym(6, noise);
    ym(7, mix | 0xc0);
}

/* pitch: period scale in 8.8 (256 = as designed, 128 = one octave up) */
static void sfx_play(int id);

static void sfx_play_pitch(int id, u16 pitch)
{
#ifdef DOSOUND
    (void)pitch;
    sfx_play(id);
    return;
#else
    long sr;
    int l, used = -1;
    if (!sfx_enabled) return;
    sr = irq_off();
    for (l = 0; l < sfx_nlayers[id]; l++) {
        const u16 *lay = sfx_layer[sfx_first[id] + l];
        int i, best = -1;
        for (i = 0; i < 3; i++)                 /* free voice first */
            if (i != used && voices[i].left <= 0) { best = i; break; }
        if (best < 0)                           /* else steal the lowest priority */
            for (i = 0; i < 3; i++) {
                if (i == used || voices[i].prio > sfx_prio[id]) continue;
                if (best < 0 || voices[i].prio < voices[best].prio ||
                    (voices[i].prio == voices[best].prio && voices[i].left < voices[best].left)) best = i;
            }
        if (best < 0) continue;
        voices[best].p = sfx_data + lay[0] * 2;
        voices[best].left = (s16)lay[1];
        voices[best].prio = sfx_prio[id];
        voices[best].pitch = pitch;
        used = best;
    }
    irq_restore(sr);
#endif
}

static void sfx_play(int id)
{
    sfx_plays++;
#ifdef DOSOUND
    dosound(sfx_ds + sfx_ds_off[id]);     /* let TOS play it from its own timer */
#else
    sfx_play_pitch(id, 256);
#endif
}

static void sfx_init(void)
{
    old_conterm = *conterm_p;
    *conterm_p = (u8)(old_conterm & ~1);     /* keyclick off while playing */
    *conterm_p = (u8)(old_conterm & ~5);     /* keyclick and bell off */
#ifndef DOSOUND
    dosound(dosound_stop);
    sfx_silence();
#endif
#ifdef SFX_VBL
    vbl_slot = vbl_install(sfx_tick);        /* optional: tick from the TOS VBL queue */
    if (!vbl_slot) log_str("no free VBL slot - no sound\n");
#endif
}

static void sfx_exit(void)
{
#ifdef SFX_VBL
    vbl_remove(vbl_slot);
#endif
    sfx_silence();
    *conterm_p = old_conterm;
}

/* ---- game state ---- */
enum { B_EMPTY = 0, B_NORMAL = 1, B_METAL = 2, B_EXPL = 3, B_CRACK = 4 };
static u8 cell_type[BROWS * BCOLS];
static u8 cell_hits[BROWS * BCOLS];
static u8 cell_max[BROWS * BCOLS];
static int bricks_left;           /* destructible */
static int bricks_start;
static int level_idx;             /* 0-based */
static int lives;
static u32 score;
static int frame_no;
static int bricks_drawn_frame, hud_drawn_frame;

#define BASE_SPEED 512    /* 2.0 px/frame */
#define MAX_SPEED  896    /* 3.5 px/frame */
#define BARRIER_Y  196

/* balls */
#define MAX_BALLS 3
typedef struct { s32 x, y, vx, vy; s16 speed; u8 active; } Ball;
static Ball balls[MAX_BALLS];
static int ball_stuck;
static int frames_since_hit;
static u16 speed_mult = 256;      /* slowdown multiplier, 8.8 (min 0.9) */

static int paddle_x, paddle_w;
static Sprite spr_ball, spr_fireball, spr_paddle, spr_nub, spr_bullet;

/* power-ups (rules from the web game's usePowerUps / powerUpWeights) */
enum { PU_MULTI, PU_TURRET, PU_FIRE, PU_EXTEND, PU_SHRINK, PU_SHIELD, PU_BARRIER, PU_LIFE, PU_EXIT, PU_COUNT };
static const u16 pu_base_weight[PU_COUNT] = { 150, 150, 150, 150, 80, 150, 150, 100, 0 };
/* up to two characters per capsule */
static const char pu_text[PU_COUNT][3] = { "M", "T", "F", "+", "-", "S", "B", "+1", "?" };
static const u8 pu_colour[PU_COUNT] = { 14, 14, 10, 14, 9, 14, 14, 10, 12 };
static Sprite spr_cap[PU_COUNT];
static u8 pu_drops[PU_COUNT];         /* per level, for weight decay */
static u16 life_groups_used;          /* bit per level group (level / 5) */
static int life_dropped_this_level;
static int bricks_since_drop;
static u32 pu_collected[PU_COUNT];

#define MAX_CAPS 6
#define CAP_W 14
#define CAP_H 8
typedef struct { s16 x; s32 y; u8 type, active; s8 pair; } Cap;
static Cap caps[MAX_CAPS];

static int exit_open, exit_offered, exit_wait, life_lost_level;
static int hud_exit_shown = -1;
#ifdef TEST
static int exit_caps_seen;
#endif
#ifdef TEST
static int exit_hud_shot_in;
#endif
static int fireball_timer;            /* frames */
static int turret_shots;
static int has_shield;
static int has_barrier;

#define MAX_BULLETS 4
typedef struct { s16 x, y; u8 active; } Bullet;
static Bullet bullets[MAX_BULLETS];

typedef struct { s16 cell; s16 at; } Pending;
static Pending pend[64];
static int npend;

/* ST scan codes for A-Z, used for typing initials */
static const u8 scan_letter[26] = {
    0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
    0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c
};

static u16 rng = 0xace1;
static u16 rnd(void)
{
    u16 r = rng;
    r ^= (u16)(r << 7);
    r ^= (u16)(r >> 9);
    r ^= (u16)(r << 8);
    rng = r ? r : 0xace1;
    return rng;
}

/* ---- high scores ---- */
#define NUM_HI 5
typedef struct { char name[4]; u32 score; } HiEntry;
static HiEntry hiscores[NUM_HI] = {
    { "QMR", 50000 }, { "QRK", 40000 }, { "STF", 30000 }, { "YM2", 20000 }, { "ACE", 10000 }
};
static const char hi_file[] = "QUAN.HI";

/* file layout: "VHI1" + NUM_HI * { name[4], u32 score } - every field word aligned,
   because the 68000 faults on word/long access at odd addresses */
static u32 hi_buf[1 + NUM_HI * 2];

static void hi_load(void)
{
    long h = f_open(hi_file, 0);
    if (h < 0) return;
    if (f_read(h, sizeof hi_buf, hi_buf) == (long)sizeof hi_buf && hi_buf[0] == 0x56484931UL) {
        memcpy(hiscores, hi_buf + 1, sizeof hiscores);
        {
            int i;
            for (i = 0; i < NUM_HI; i++) hiscores[i].name[3] = 0;
        }
    }
    f_close(h);
}

static void hi_save(void)
{
    long h;
    hi_buf[0] = 0x56484931UL;     /* "VHI1" */
    memcpy(hi_buf + 1, hiscores, sizeof hiscores);
    h = f_create(hi_file);
    if (h < 0) { log_str("hiscore save failed\n"); return; }   /* e.g. write-protected disk */
    f_write(h, sizeof hi_buf, hi_buf);
    f_close(h);
}

static int hi_rank(u32 sc)
{
    int i;
    for (i = 0; i < NUM_HI; i++) if (sc > hiscores[i].score) return i;
    return -1;
}

/* ---- drawing of static things ---- */
static const u8 hits_table(int level, int row)
{
    if (level < 3) return 1;
    if (level < 5) return row < 2 ? 2 : 1;
    if (level < 8) return row < 3 ? 2 : 1;
    if (level < 12) return row < 2 ? 3 : row < 4 ? 2 : 1;
    if (level < 18) return row < 3 ? 3 : row < 5 ? 2 : 1;
    if (level < 25) return row < 2 ? 4 : row < 4 ? 3 : row < 6 ? 2 : 1;
    return 1;
}

static void draw_brick(int i)
{
    int c = i % BCOLS, r = i / BCOLS;
    int x = PF_X + c * 16, y = BR_Y + r * 8;
    int t = cell_type[i];
    bricks_drawn_frame++;
    fill_rect(bg, x, y, 16, 8, COL_BG);
    if (t == B_EMPTY) { dirty_both(x, y, 16, 8); return; }
    if (t == B_METAL) {
        fill_rect(bg, x, y, 16, 8, COL_METAL);
        fill_rect(bg, x, y, 16, 1, COL_LIGHT);
        plot(bg, x + 3, y + 3, COL_LIGHT);
        plot(bg, x + 12, y + 3, COL_LIGHT);
    } else {
        int col = t == B_EXPL ? COL_EXPL : t == B_CRACK ? COL_CRACK : 1 + r % 8;
        int dmg = cell_max[i] - cell_hits[i], k;
        fill_rect(bg, x, y, 15, 7, col);
        fill_rect(bg, x, y, 15, 1, COL_WHITE);
        fill_rect(bg, x, y + 6, 15, 1, COL_DARK);
        if (t == B_EXPL) {
            fill_rect(bg, x + 6, y + 1, 3, 3, COL_WHITE);
            if (cell_max[i] > 1)                   /* dots = hits still needed */
                for (k = 0; k < cell_hits[i]; k++)
                    fill_rect(bg, x + 2 + k * 4, y + 4, 2, 2, COL_WHITE);
        } else if (t == B_CRACK) {
            if (dmg >= 1) { plot(bg, x + 4, y + 2, COL_DARK); plot(bg, x + 5, y + 3, COL_DARK); plot(bg, x + 5, y + 4, COL_DARK); }
            if (dmg >= 2) { plot(bg, x + 10, y + 1, COL_DARK); plot(bg, x + 9, y + 2, COL_DARK); plot(bg, x + 10, y + 3, COL_DARK); plot(bg, x + 11, y + 4, COL_DARK); }
        } else if (cell_max[i] > 1) {
            for (k = 0; k < cell_hits[i]; k++) fill_rect(bg, x + 2 + k * 4, y + 3, 2, 2, COL_WHITE);
            if (dmg) fill_rect(bg, x + 1, y + 5, 13, 1, COL_DARK);
        }
    }
    dirty_both(x, y, 16, 8);
}

static void draw_number(int x, int y, u32 v, int digits)
{
    char b[12], *s = utoa(v, b + 11);
    while (b + 11 - s < digits) *--s = '0';
    draw_text(bg, x, y, s, COL_WHITE, COL_DARK);
    dirty_both(x, y, digits * 8, 8);
}

static void draw_text_at(u8 *buf, int x, int y, const char *s, int col)
{
    draw_text(buf, x, y, s, col, COL_DARK);
}

/* chunky 3x letters for the pause screen */
static void draw_text_big(u8 *buf, int x, int y, const char *s, int col)
{
    for (; *s; s++, x += 24) {
        int ch = (u8)*s, r, c;
        if (ch < 32 || ch > 127) ch = 32;
        for (r = 0; r < 8; r++)
            for (c = 0; c < 8; c++)
                if (font8[ch - 32][r] & (0x80 >> c)) {
                    fill_rect(buf, x + c * 3 + 1, y + r * 3 + 1, 3, 3, COL_METAL);
                    fill_rect(buf, x + c * 3, y + r * 3, 3, 3, col);
                }
    }
}

static void draw_text_2x(int x, int y, const char *s, int col)
{
    for (; *s; s++, x += 16) {
        int ch = (u8)*s, r, c;
        if (ch < 32 || ch > 127) ch = 32;
        for (r = 0; r < 8; r++)
            for (c = 0; c < 8; c++)
                if (font8[ch - 32][r] & (0x80 >> c)) {
                    fill_rect(bg, x + c * 2 + 1, y + r * 2 + 1, 2, 2, COL_DARK);   /* shadow */
                    fill_rect(bg, x + c * 2, y + r * 2, 2, 2, col);
                }
    }
}


static char hud_score_prev[9];

/* redraw only the score digits that changed */
static void draw_score(void)
{
    char b[12], *s = utoa(score, b + 11);
    int i;
    while (b + 11 - s < 8) *--s = '0';
    for (i = 0; i < 8; i++) {
        if (s[i] != hud_score_prev[i]) {
            char one[2];
            one[0] = s[i]; one[1] = 0;
            draw_text(bg, PANEL_X + 16 + i * 8, 40, one, COL_WHITE, COL_DARK);
            dirty_both(PANEL_X + 16 + i * 8, 40, 8, 8);
            hud_score_prev[i] = s[i];
        }
    }
    hud_drawn_frame++;
}

static void draw_hud(void)
{
    int i;
    for (i = 0; i < 8; i++) hud_score_prev[i] = 0;
    draw_score();
    draw_number(PANEL_X + 48, 80, (u32)level_idx + 1, 2);
    draw_number(PANEL_X + 48, 112, (u32)lives, 2);
}

/* power-up status lines; each line is redrawn only when its own value changes */
static int hud_fire = -1, hud_turret = -1, hud_barrier = -1, hud_shield = -1;

static void power_line(int y)
{
    fill_rect(bg, PANEL_X, y, 96, 8, COL_DARK);
    dirty_both(PANEL_X, y, 96, 8);
    hud_drawn_frame++;
}

static void draw_power_hud(void)
{
    int f = fireball_timer ? (fireball_timer + 24) / 25 : 0;
    if (f != hud_fire) {
        hud_fire = f;
        power_line(128);
        if (f) {
            draw_text(bg, PANEL_X + 8, 128, "FIRE", COL_EXPL, COL_DARK);
            fill_rect(bg, PANEL_X + 48, 130, f * 4, 4, COL_EXPL);
        }
    }
    if (turret_shots != hud_turret) {
        char b[12];
        if (turret_shots && hud_turret > 0) {          /* number only */
            fill_rect(bg, PANEL_X + 64, 138, 16, 8, COL_DARK);
            dirty_both(PANEL_X + 64, 138, 16, 8);
        } else {
            power_line(138);
            if (turret_shots) draw_text(bg, PANEL_X + 8, 138, "TURRET", COL_LIGHT, COL_DARK);
        }
        if (turret_shots) draw_text(bg, PANEL_X + 64, 138, utoa((u32)turret_shots, b + 11), COL_WHITE, COL_DARK);
        hud_turret = turret_shots;
    }
    if (has_barrier != hud_barrier) {
        hud_barrier = has_barrier;
        power_line(148);
        if (has_barrier) draw_text(bg, PANEL_X + 8, 148, "BARRIER", COL_LIGHT, COL_DARK);
    }
    if (has_shield != hud_shield) {
        hud_shield = has_shield;
        power_line(158);
        if (has_shield) draw_text(bg, PANEL_X + 8, 158, "SHIELD", COL_LIGHT, COL_DARK);
    }
}

static void draw_panel_labels(void)
{
    fill_rect(bg, PANEL_X, 0, 320 - PANEL_X, 200, COL_DARK);
    draw_text(bg, PANEL_X + 20, 12, "QUANOID", COL_EXPL, COL_DARK);
    draw_text(bg, PANEL_X + 16, 30, "SCORE", COL_LIGHT, COL_DARK);
    draw_text(bg, PANEL_X + 16, 70, "LEVEL", COL_LIGHT, COL_DARK);
    draw_text(bg, PANEL_X + 16, 102, "LIVES", COL_LIGHT, COL_DARK);
    draw_text(bg, PANEL_X + 8, 180, "ESC=PAUSE", COL_METAL, COL_DARK);
    draw_text(bg, PANEL_X + 8, 190, VERSION, COL_METAL, COL_DARK);
    draw_text(bg, PANEL_X + 8, 170, sfx_enabled ? "S=SFX ON " : "S=SFX OFF", COL_METAL, COL_DARK);
    hud_fire = hud_turret = hud_barrier = hud_shield = -1;
}

static void draw_walls(void)
{
    int i;
    fill_rect(bg, 0, 0, 320, 200, COL_BG);
    fill_rect(bg, 0, 0, PF_X2 + 8, PF_Y, COL_LIGHT);
    fill_rect(bg, 0, 0, PF_X, 200, COL_LIGHT);
    fill_rect(bg, PF_X2, 0, 8, 200, COL_LIGHT);
    for (i = 0; i < 200; i += 4) {
        plot(bg, 3, i + 1, COL_METAL);
        plot(bg, PF_X2 + 4, i + 3, COL_METAL);
    }
    for (i = 0; i < PF_X2 + 8; i += 4) plot(bg, i + 1, 3, COL_METAL);
}

static void draw_barrier(void)
{
    fill_rect(bg, PF_X, BARRIER_Y, PF_X2 - PF_X, 2, has_barrier ? COL_LIGHT : COL_BG);
    if (has_barrier) {
        int x;
        for (x = PF_X; x < PF_X2; x += 4) plot(bg, x, BARRIER_Y, COL_WHITE);
    }
    dirty_both(PF_X, BARRIER_Y, PF_X2 - PF_X, 2);
}

/* ---- sprites ---- */
static int paddle_shielded;
#ifdef PIXCHECK
static u32 ball_bad, draw_on_visible, late_flip;
#endif

static void build_paddle(int w)
{
    static char pix[PAD_H * 64];
    int c, r;
    char body = paddle_shielded ? 'c' : 'e';   /* white while the shield holds */
    for (r = 0; r < PAD_H; r++)
        for (c = 0; c < w; c++) {
            char ch = body;
            if (c < 4 || c >= w - 4) ch = 'a';
            if (r == 0) ch = paddle_shielded ? 'e' : 'c';
            if (r == PAD_H - 1) ch = 'f';
            if ((r == 0 || r == PAD_H - 1) && (c == 0 || c == w - 1)) ch = ' ';
            if (c == 4 || c == w - 5) ch = paddle_shielded ? 'c' : 'f';
            pix[r * w + c] = ch;
        }
    paddle_w = w;
    make_sprite(&spr_paddle, pix, w, PAD_H);
}

/* Capsule letters get their own small glyph set, sized to actually fit the
   6 usable rows inside a capsule (row 0 and row 7 of 8 are the border) -
   the shared 7-row game font was designed for plain text on a flat
   background and is one row too tall for that, which is what let the "T"
   punch through the border and, once cropped to fix that, cut the legs
   off the "M". Each row here is a 5-bit pattern, MSB-first. */
static const u8 cap_glyph[][6] = {
    /* M */ { 0x11, 0x1b, 0x15, 0x11, 0x11, 0x11 },
    /* T */ { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04 },
    /* F */ { 0x1f, 0x10, 0x1e, 0x10, 0x10, 0x10 },
    /* W */ { 0x11, 0x11, 0x15, 0x15, 0x0a, 0x0a },
    /* N */ { 0x19, 0x19, 0x15, 0x13, 0x13, 0x11 },
    /* S */ { 0x0f, 0x10, 0x0e, 0x01, 0x01, 0x1e },
    /* B */ { 0x1e, 0x11, 0x1e, 0x11, 0x11, 0x1e },
    /* + */ { 0x00, 0x04, 0x04, 0x1f, 0x04, 0x04 },
    /* - */ { 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00 },
    /* 1 */ { 0x0c, 0x04, 0x04, 0x04, 0x04, 0x04 },
    /* ? */ { 0x0e, 0x11, 0x02, 0x04, 0x00, 0x04 },
};
static const char cap_glyph_chars[] = "MTFWNSB+-1?";

static const u8 *cap_glyph_for(char ch)
{
    const char *p;
    for (p = cap_glyph_chars; *p; p++)
        if (*p == ch) return cap_glyph[p - cap_glyph_chars];
    return cap_glyph[0];
}

static void build_sprites(void)
{
    static const char ball_pix[] = " cc cccccdcc dd ";
    static const char fire_pix[] = " aa acca accaaaa";
    static const char nub_pix[] = "ee" "ee" "ff";
    static const char bullet_pix[] = "cc" "cc" "cc" "aa" "aa" "a " " a";
    static char cap_pix[CAP_W * CAP_H];
    int t, r, c;
    make_sprite(&spr_ball, ball_pix, 4, 4);
    make_sprite(&spr_fireball, fire_pix, 4, 4);
    make_sprite(&spr_nub, nub_pix, 2, 3);
    make_sprite(&spr_bullet, bullet_pix, 2, 7);
    for (t = 0; t < PU_COUNT; t++) {
        char body = (char)(pu_colour[t] < 10 ? '0' + pu_colour[t] : 'a' + pu_colour[t] - 10);
        int n = pu_text[t][1] ? 2 : 1;             /* one or two characters */
        int x0 = n == 2 ? 1 : 4;                   /* two glyphs start further left */
        for (r = 0; r < CAP_H; r++)
            for (c = 0; c < CAP_W; c++) {
                char ch = body;
                int g;
                if ((r == 0 || r == CAP_H - 1) && (c < 2 || c > CAP_W - 3)) ch = ' ';
                else if ((c == 0 || c == CAP_W - 1) && (r == 1 || r == CAP_H - 2)) ch = ' ';
                else if (r == CAP_H - 1 || c == CAP_W - 1) ch = 'f';
                else if (r == 0) ch = 'c';
                for (g = 0; g < n; g++) {
                    int gx = x0 + g * 6, bit = c - gx;   /* glyphs are 5 px wide */
                    if (bit >= 0 && bit < 5 && r >= 1 && r <= 6 &&
                        (cap_glyph_for(pu_text[t][g])[r - 1] & (0x10 >> bit)))
                        /* dark letter on light bodies (paddle-blue or white), light
                           letter on everything else - otherwise a white body (the
                           exit capsule) draws its own "?" in invisible white-on-white */
                        ch = (pu_colour[t] == 13 || pu_colour[t] == 12) ? 'f' : 'c';
                }
                cap_pix[r * CAP_W + c] = ch;
            }
        make_sprite(&spr_cap[t], cap_pix, CAP_W, CAP_H);
    }
}

/*#INCLUDE boss_part.c*/
/*#INCLUDE enemy_part.c*/

/* ---- the exit door: a way out when a level drags on ---- */
#define EXIT_Y (PAD_Y - 2)
#define EXIT_H (PAD_H + 4)

/* small countdown in the panel, from the moment the exit timer starts until
   the capsule appears (or the level ends) - so it reads as a feature, not a
   surprise. Shares the boss-health-bar row, since a level never has both. */
static void draw_exit_hud(void)
{
    int secs = (exit_open || exit_offered || exit_wait < 0) ? -1 : (exit_wait + 49) / 50;
    if (secs == hud_exit_shown) return;
    hud_exit_shown = secs;
    fill_rect(bg, PANEL_X, 52, 96, 16, COL_DARK);
    if (secs >= 0) {
        draw_text(bg, PANEL_X + 8, 52, "EXIT IN", COL_LIGHT, COL_DARK);
        draw_number(PANEL_X + 72, 52, (u32)secs, 2);
    }
    dirty_both(PANEL_X, 52, 96, 16);
}

static void draw_exit_door(void)
{
    if (exit_open) {
        fill_rect(bg, PF_X2, EXIT_Y, 8, EXIT_H, COL_BG);
        fill_rect(bg, PF_X2, EXIT_Y, 1, EXIT_H, COL_EXPL);
        fill_rect(bg, PF_X2, EXIT_Y + EXIT_H - 1, 8, 1, COL_EXPL);
        fill_rect(bg, PF_X2, EXIT_Y, 8, 1, COL_EXPL);
    } else {
        int i;
        fill_rect(bg, PF_X2, EXIT_Y, 8, EXIT_H, COL_LIGHT);
        for (i = 0; i < 200; i += 4) plot(bg, PF_X2 + 4, i + 3, COL_METAL);
    }
    dirty_both(PF_X2, EXIT_Y, 8, EXIT_H);
}

/* ---- level ---- */
static void sync_pages(void)
{
    memcpy(scr[0], bg, 32000);
    memcpy(scr[1], bg, 32000);
    ndirty[0] = ndirty[1] = 0;
}

static void reset_ball(void)
{
    int i;
    ball_stuck = 1;
    for (i = 0; i < MAX_BALLS; i++) balls[i].active = 0;
    balls[0].active = 1;
    balls[0].speed = (s16)((BASE_SPEED * (s32)speed_mult) >> 8);
    balls[0].vx = balls[0].vy = 0;
    frames_since_hit = 0;
    fireball_timer = 0;
}

static void clear_objects(void)
{
    int i;
    for (i = 0; i < MAX_CAPS; i++) caps[i].active = 0;
    for (i = 0; i < MAX_BULLETS; i++) bullets[i].active = 0;
}

static void load_level(int idx)
{
    int i, w;
    level_idx = idx;
    w = idx / 5;
    for (i = 0; i < 8; i++) palette[1 + i] = world_pal[w][i];
    set_palette();
    draw_walls();
    draw_panel_labels();
    bricks_left = 0;
    npend = 0;
    for (i = 0; i < PU_COUNT; i++) pu_drops[i] = 0;
    life_dropped_this_level = 0;
    bricks_since_drop = 0;
    for (i = 0; i < BROWS * BCOLS; i++) {
        int t = level_data[idx][i];
        cell_type[i] = (u8)t;
        cell_max[i] = t == B_CRACK ? 3 : t == B_NORMAL || t == B_EXPL ? hits_table(idx + 1, i / BCOLS) : 1;
        cell_hits[i] = cell_max[i];
        if (t && t != B_METAL) bricks_left++;
        draw_brick(i);
    }
    boss_active = 0;
    boss_hp_shown = -1;
    exit_open = 0;
    exit_offered = 0;
    exit_wait = -1;
    hud_exit_shown = -1;
    life_lost_level = 0;
    if (level_is_boss[idx]) {
        boss_start(idx + 1);
        bricks_left = 1;            /* the level ends when the boss dies */
    }
    bricks_start = bricks_left;
    speed_mult = 256;
    clear_objects();
    enemies_reset();
    draw_exit_door();
    draw_barrier();
    draw_hud();
    draw_power_hud();
    draw_boss_hud();
    draw_exit_hud();
    reset_ball();
    {
        char b[12];
        log_str("LEVEL ");
        log_str(utoa((u32)idx + 1, b + 11));
        log_kv(" bricks=", (u32)bricks_left);
        log_str("\n");
    }
}

/* ---- power-ups ---- */
static int pick_type(int exclude)
{
    u16 wt[PU_COUNT];
    u32 total = 0, r;
    int t, k;
    for (t = 0; t < PU_COUNT; t++) {
        u16 w = pu_base_weight[t];
        for (k = 0; k < pu_drops[t] && w > 20; k++) w = (u16)(w * 3 / 5);
        if (t == PU_EXIT) {
            w = 0;   /* never a random brick drop - only the exit-door timer spawns this */
        } else if (w < 20) {
            w = 20;
        }
        if (t == PU_LIFE && ((life_groups_used >> ((level_idx + 1) / 5)) & 1 || life_dropped_this_level)) w = 0;
        if (t == exclude) w = 0;
        wt[t] = w;
        total += w;
    }
    r = (u32)rnd() % total;
    for (t = 0; t < PU_COUNT; t++) {
        if (r < wt[t]) return t;
        r -= wt[t];
    }
    return PU_MULTI;
}

static int free_cap(void)
{
    int i;
    for (i = 0; i < MAX_CAPS; i++) if (!caps[i].active) return i;
    return -1;
}

static void spawn_cap(int cx, int y, int type, int pair)
{
    int i = free_cap();
    int x = cx - CAP_W / 2;
    if (i < 0) return;
    if (x < PF_X) x = PF_X;
    if (x > PF_X2 - CAP_W) x = PF_X2 - CAP_W;
    caps[i].x = (s16)x;
    caps[i].y = (s32)y << 8;
    caps[i].type = (u8)type;
    caps[i].pair = (s8)pair;
    caps[i].active = 1;
    pu_drops[type]++;
    if (type == PU_LIFE) life_dropped_this_level = 1;
}

/* drop chance ramps 3% -> 16% over the level, plus pity 0.4%/brick (max 12%) */
static void maybe_drop(int cell)
{
    u32 progress, chance;
    int cx = PF_X + (cell % BCOLS) * 16 + 8, y = BR_Y + (cell / BCOLS) * 8;
    if (bricks_start <= 0) return;
    progress = (u32)(bricks_start - bricks_left) * 1000 / bricks_start;       /* 0..1000 */
    chance = 30 + (130 * progress) / 1000;                                    /* per mille */
    chance += bricks_since_drop * 4 > 120 ? 120 : bricks_since_drop * 4;
    if ((u32)(rnd() % 1000) >= chance) { bricks_since_drop++; return; }
    bricks_since_drop = 0;
    {
        int a = pick_type(-1);
        if ((rnd() % 100) < 15 && free_cap() >= 0) {             /* dual choice */
            int b = pick_type(a), ia, ib, gap = 24;
            int l = cx - gap, r = cx + gap;
            if (l - CAP_W / 2 < PF_X) { l = PF_X + CAP_W / 2; r = l + 2 * gap; }
            if (r + CAP_W / 2 > PF_X2) { r = PF_X2 - CAP_W / 2; l = r - 2 * gap; }
            ia = free_cap();
            spawn_cap(l, y, a, -1);
            ib = free_cap();
            if (ib >= 0) {
                spawn_cap(r, y, b, (s8)ia);
                caps[ia].pair = (s8)ib;
            }
        } else {
            spawn_cap(cx, y, a, -1);
        }
    }
#ifdef TEST
    {
        int k, n;
        for (k = 0, n = 0; k < MAX_CAPS; k++) if (caps[k].active && caps[k].type == PU_EXIT) n++;
        if (n > exit_caps_seen) {
            exit_caps_seen = n;
            log_kv("EXIT CAP VIA RANDOM DROP! bricks_left=", (u32)bricks_left);
            log_kv(" exit_wait=", (u32)exit_wait);
            log_kv(" exit_offered=", (u32)exit_offered);
            log_str("\n");
        }
    }
#endif
}

static void set_ball_dir(Ball *b, int idx)
{
    if (idx < 0) idx = 0;
    if (idx > 32) idx = 32;
    b->vx = (s32)((s16)b->speed * ang_sin[idx]) >> 8;
    b->vy = -((s32)((s16)b->speed * ang_cos[idx]) >> 8);
}

/* closest table index for the ball's current direction (upward-normalised) */
static int ball_dir_index(const Ball *b)
{
    int i, best = 16;
    s32 bestd = 0x7fffffffL;
    s32 vx = b->vx;
    for (i = 0; i <= 32; i++) {
        s32 d = ((s32)b->speed * ang_sin[i] >> 8) - vx;
        if (d < 0) d = -d;
        if (d < bestd) { bestd = d; best = i; }
    }
    return best;
}

static const u8 pu_sound[PU_COUNT] = {
    SFX_PU_MULTI, SFX_PU_TURRET, SFX_PU_FIRE, SFX_PU_EXTEND,
    SFX_PU_SHRINK, SFX_PU_SHIELD, SFX_PU_BARRIER, SFX_PU_LIFE, SFX_PU_MULTI
};

static void apply_power(int t)
{
    int i;
    pu_collected[t]++;
    sfx_play(pu_sound[t]);
    switch (t) {
    case PU_MULTI: {
        Ball *src = 0;
        int dir, sign;
        for (i = 0; i < MAX_BALLS; i++) if (balls[i].active) { src = &balls[i]; break; }
        if (!src || ball_stuck) break;
        dir = ball_dir_index(src);
        sign = src->vy > 0 ? 1 : -1;
        for (i = 0; i < MAX_BALLS; i++) {
            static const signed char offs[2] = { -6, 6 };
            int k, used = 0;
            for (k = 0; k < MAX_BALLS; k++) used += balls[k].active;
            if (used >= MAX_BALLS) break;
            if (balls[i].active) continue;
            balls[i] = *src;
            set_ball_dir(&balls[i], dir + offs[(used - 1) & 1]);
            if (sign > 0) balls[i].vy = -balls[i].vy;
            balls[i].active = 1;
        }
        break;
    }
    case PU_TURRET:
        turret_shots = turret_shots ? (turret_shots + 30 > 45 ? 45 : turret_shots + 30) : 30;
        break;
    case PU_FIRE:
        fireball_timer = 250;              /* 5 s */
        break;
    case PU_EXTEND: {
        int nw = paddle_w + 8 > 52 ? 52 : paddle_w + 8;
        paddle_x -= (nw - paddle_w) / 2;
        build_paddle(nw);
        break;
    }
    case PU_SHRINK: {
        int nw = paddle_w - 8 < 16 ? 16 : paddle_w - 8;
        paddle_x += (paddle_w - nw) / 2;
        build_paddle(nw);
        break;
    }
    case PU_SHIELD:
        has_shield = 1;
#ifdef TEST
#endif                    /* protects against boss attacks (milestone 6) */
        break;
    case PU_BARRIER:
        has_barrier = 1;
        draw_barrier();
        break;
    case PU_EXIT:
        exit_open = 1;
        draw_exit_door();
        break;
    case PU_LIFE:
        if (!((life_groups_used >> ((level_idx + 1) / 5)) & 1)) {
            lives++;
            life_groups_used |= (u16)(1 << ((level_idx + 1) / 5));
        }
        break;
    }
    if (paddle_x < PF_X) paddle_x = PF_X;
    if (paddle_x > PF_X2 - paddle_w) paddle_x = PF_X2 - paddle_w;
}

static void update_caps(void)
{
    int i;
    for (i = 0; i < MAX_CAPS; i++) {
        Cap *c = &caps[i];
        int y;
        if (!c->active) continue;
        c->y += 192;                       /* 0.75 px/frame */
        y = (int)(c->y >> 8);
        if (y >= 200 - CAP_H) { c->active = 0; continue; }
        if (y + CAP_H > PAD_Y && y < PAD_Y + PAD_H &&
            c->x + CAP_W > paddle_x && c->x < paddle_x + paddle_w) {
            c->active = 0;
            if (c->pair >= 0) caps[(int)c->pair].active = 0;
            apply_power(c->type);
        }
    }
}

/* ---- bricks ---- */
static u32 brick_points(int i)
{
    if (cell_type[i] == B_METAL) return 0;
    return (u32)(BROWS - i / BCOLS) * 10 * cell_max[i];
}

static void speed_up(Ball *b)
{
    int thr = bricks_start * 12 / 100;
    s16 ns, add;
    if (thr < 6) thr = 6;
    if (bricks_left <= thr) {              /* endgame: ~2.7% .. ~5.1% per brick */
        add = (s16)((b->speed * (7 + 6 * (thr - bricks_left) / thr)) >> 8);
    } else {
        add = (s16)((b->speed * 4) >> 8);  /* ~1.6% */
    }
    if (add < 1) add = 1;
    ns = (s16)(b->speed + add);
    if (ns > MAX_SPEED) ns = MAX_SPEED;
    if (ns != b->speed) {
        b->vx = b->vx * ns / b->speed;
        b->vy = b->vy * ns / b->speed;
        b->speed = ns;
    }
}

static void schedule(int i)
{
    int k;
    for (k = 0; k < npend; k++) if (pend[k].cell == i) return;
    if (npend < 64) {
        pend[npend].cell = (s16)i;
        pend[npend].at = (s16)(frame_no + 10);     /* 200 ms */
        npend++;
    }
}

static void destroy(int i)
{
    int t = cell_type[i];
    if (!t) return;
    score += brick_points(i);
    if (t != B_METAL) bricks_left--;
    cell_type[i] = B_EMPTY;
    draw_brick(i);
    if (t != B_METAL) maybe_drop(i);
}

static const signed char blast[][2] = { {0,-1},{0,1},{0,-2},{0,2},{-1,0},{1,0},{-1,-1},{1,-1},{-1,1},{1,1} };

static void detonate(int i)
{
    int c = i % BCOLS, r = i / BCOLS, k;
    sfx_play(SFX_EXPLODE);
    destroy(i);
    for (k = 0; k < 10; k++) {
        int cc = c + blast[k][0], rr = r + blast[k][1], j;
        if (cc < 0 || cc >= BCOLS || rr < 0 || rr >= BROWS) continue;
        j = rr * BCOLS + cc;
        if (cell_type[j] == B_EXPL) schedule(j);
        else if (cell_type[j]) destroy(j);
    }
}

/* returns 1 if the brick was removed */
static int damage_brick(int i, Ball *b, int fire)
{
    if (cell_type[i] == B_METAL) { sfx_play(SFX_BOUNCE); return 0; }
    if (cell_type[i] == B_CRACK && !fire) {
        sfx_play(cell_hits[i] >= 3 ? SFX_CRACK3 : cell_hits[i] == 2 ? SFX_CRACK2 : SFX_CRACK1);
    } else if (cell_type[i] != B_EXPL || cell_hits[i] > 1) {
        /* combo pitch: +4.5 % per consecutive brick, capped at 2x */
        u16 pitch = (u16)(65536L / (256 + (combo > 22 ? 22 : combo) * 12));
        sfx_play_pitch(SFX_BRICK, pitch);
    }
    if (b) combo++;
    if (fire) cell_hits[i] = 1;
    if (--cell_hits[i] == 0) {
        if (b) speed_up(b);
        if (cell_type[i] == B_EXPL) detonate(i);
        else destroy(i);
        return 1;
    }
    draw_brick(i);
    return 0;
}

static int cell_at(int px, int py)
{
    int c, r;
    if (px < PF_X || px >= PF_X + BCOLS * 16 || py < BR_Y || py >= BR_Y + BROWS * 8) return -1;
    c = (px - PF_X) >> 4;
    r = (py - BR_Y) >> 3;
    return cell_type[r * BCOLS + c] ? r * BCOLS + c : -1;
}

static int box_hit(int x, int y)
{
    int h;
    if ((h = cell_at(x, y)) >= 0) return h;
    if ((h = cell_at(x + BALL_S - 1, y)) >= 0) return h;
    if ((h = cell_at(x, y + BALL_S - 1)) >= 0) return h;
    return cell_at(x + BALL_S - 1, y + BALL_S - 1);
}

static void paddle_bounce(Ball *b)
{
    sfx_play(SFX_BOUNCE);
    combo = 0;
    int cx = (int)(b->x >> 8) + BALL_S / 2;
    set_ball_dir(b, ((cx - paddle_x) * 32) / paddle_w);
    frames_since_hit = 0;
}

/* returns 1 if the ball fell out */
static int move_ball(Ball *b)
{
    s32 ax = b->vx < 0 ? -b->vx : b->vx;
    s32 ay = b->vy < 0 ? -b->vy : b->vy;
    int steps = (int)(((ax > ay ? ax : ay) >> 8) + 1), s;
    s32 sx = b->vx / steps, sy = b->vy / steps;
    int fire = fireball_timer > 0;

    for (s = 0; s < steps; s++) {
        int x, y, h;
        b->x += sx;
        x = (int)(b->x >> 8); y = (int)(b->y >> 8);
        if (boss_ball_hit(b) || enemy_ball_hit(b)) {
            b->x -= sx; sx = -sx; b->vx = -b->vx;
            frames_since_hit = 0;
        } else if (x < PF_X || x + BALL_S > PF_X2) {
            b->x -= sx; sx = -sx; b->vx = -b->vx;
            frames_since_hit = 0;
            sfx_play(SFX_BOUNCE);
        } else if ((h = box_hit(x, y)) >= 0) {
            frames_since_hit = 0;
            if (!(fire && cell_type[h] != B_METAL)) {
                b->x -= sx; sx = -sx; b->vx = -b->vx;
            }
            damage_brick(h, b, fire);
        }
        b->y += sy;
        x = (int)(b->x >> 8); y = (int)(b->y >> 8);
        if (boss_ball_hit(b) || enemy_ball_hit(b)) {
            b->y -= sy; sy = -sy; b->vy = -b->vy;
            frames_since_hit = 0;
        } else if (y < PF_Y) {
            b->y -= sy; sy = -sy; b->vy = -b->vy;
            frames_since_hit = 0;
            sfx_play(SFX_BOUNCE);
        } else if ((h = box_hit(x, y)) >= 0) {
            frames_since_hit = 0;
            if (!(fire && cell_type[h] != B_METAL)) {
                b->y -= sy; sy = -sy; b->vy = -b->vy;
            }
            damage_brick(h, b, fire);
        } else if (sy > 0 && y + BALL_S >= PAD_Y && y + BALL_S <= PAD_Y + 3 &&
                   x + BALL_S > paddle_x && x < paddle_x + paddle_w) {
            b->y = (s32)(PAD_Y - BALL_S) << 8;
            paddle_bounce(b);
            sx = b->vx / steps; sy = b->vy / steps;
        } else if (sy > 0 && has_barrier && y + BALL_S >= BARRIER_Y) {
            b->y = (s32)(BARRIER_Y - BALL_S) << 8;
            sy = -sy; b->vy = -b->vy;
            has_barrier = 0;
            draw_barrier();
            sfx_play(SFX_BARRIERSAVE);
        } else if (y >= 200 - BALL_S) {
            return 1;
        }
    }
    return 0;
}

static void process_pending(void)
{
    int k = 0;
    while (k < npend) {
        if (pend[k].at <= frame_no) {
            int c = pend[k].cell;
            pend[k] = pend[--npend];
            if (cell_type[c] == B_EXPL) detonate(c);
        } else k++;
    }
}

static void fire_turrets(void)
{
    int i, n = 0;
    for (i = 0; i < MAX_BULLETS; i++) n += bullets[i].active;
    if (!turret_shots || n > MAX_BULLETS - 2) return;
    for (i = 0, n = 0; i < MAX_BULLETS && n < 2; i++) {
        if (bullets[i].active) continue;
        bullets[i].x = (s16)(n == 0 ? paddle_x : paddle_x + paddle_w - 2);
        bullets[i].y = (s16)(PAD_Y - 9);
        bullets[i].active = 1;
        n++;
    }
    turret_shots--;
    sfx_play(SFX_SHOOT);
#ifdef DIAG
    log_kv("FIRED shots_left=", (u32)turret_shots); log_str("\n");
#endif
}

static void update_bullets(void)
{
    int i, s;
    for (i = 0; i < MAX_BULLETS; i++) {
        Bullet *u = &bullets[i];
        if (!u->active) continue;
        for (s = 0; s < 3 && u->active; s++) {        /* 3 px/frame, checked per pixel */
            int h;
            u->y--;
            if (u->y < PF_Y) { u->active = 0; break; }
            if (boss_active && !boss_dead_timer) {
                int bx = boss_x >> 8, by = boss_y >> 8;
                if (u->x + 2 > bx && u->x < bx + bd->w && u->y < by + bd->h && u->y + 4 > by) {
                    boss_damage(1);
                    u->active = 0;
                    break;
                }
            }
            if (enemy_bullet_hit(u->x, u->y)) { u->active = 0; break; }
            h = cell_at(u->x, u->y);
            if (h < 0) h = cell_at(u->x + 1, u->y);
            if (h >= 0) {
                damage_brick(h, 0, 0);
                u->active = 0;
            }
        }
    }
}

#if defined(TEST) || defined(DIAG)
static int shots_taken;
static void save_shot(u8 *buf, const char *what)
{
    char name[16] = "C:\\SHOT00.PI1";
    long h;
    u16 hdr[17];
    int i;
    if (shots_taken >= 40) return;
    name[7] = (char)('0' + shots_taken / 10);
    name[8] = (char)('0' + shots_taken % 10);
    shots_taken++;
    h = f_create(name);
    if (h < 0) { log_str("shot failed\n"); return; }
    hdr[0] = 0;
    for (i = 0; i < 16; i++) hdr[1 + i] = palette[i];
    f_write(h, 34, hdr);
    f_write(h, 32000, buf);
    f_close(h);
    log_str("SHOT "); log_str(name + 3); log_str(" "); log_str(what); log_str("\n");
}
#endif

/* ---- screens ---- */
enum { ST_TITLE, ST_PLAY, ST_OVER, ST_ENTRY, ST_PAUSE, ST_CLEAR, ST_QUIT };
static int state;
static int state_timer;
static char entry_name[4];
static int entry_pos, entry_rank, entry_acc;

static void show_title(void)
{
    int i;
    for (i = 0; i < 8; i++) palette[1 + i] = world_pal[0][i];
    set_palette();
    fill_rect(bg, 0, 0, 320, 200, COL_DARK);
    for (i = 0; i < 8; i++) fill_rect(bg, 24 + i * 34, 6, 32, 4, 1 + i);
    for (i = 0; i < 8; i++) fill_rect(bg, 24 + i * 34, 190, 32, 4, 8 - i);
    draw_text_2x(104, 26, "QUANOID", COL_EXPL);
    draw_text(bg, 116, 66, "HIGH SCORES", COL_WHITE, COL_DARK);
    for (i = 0; i < NUM_HI; i++) {
        char b[12], *n = utoa(hiscores[i].score, b + 11);
        static char rank[4] = "1.";
        rank[0] = (char)('1' + i);
        draw_text(bg, 92, 82 + i * 12, rank, COL_LIGHT, COL_DARK);
        draw_text(bg, 116, 82 + i * 12, hiscores[i].name, 1 + i, COL_DARK);
        while (b + 11 - n < 8) *--n = '0';
        draw_text(bg, 164, 82 + i * 12, n, COL_WHITE, COL_DARK);
    }
    draw_text(bg, 68, 154, "CLICK OR FIRE TO START", COL_WHITE, COL_DARK);
    draw_text(bg, 100, 168, "ESC TO QUIT", COL_METAL, COL_DARK);
    draw_text(bg, 4, 180, VERSION, COL_METAL, COL_DARK);
    state = ST_TITLE;
    sfx_draw_state();
    sync_pages();
    state_timer = 0;
}

static void start_game(void)
{
#ifdef TEST
    lives = 99;                 /* the autopilot should reach every level */
#else
    lives = 3;
#endif
    score = 0;
    life_groups_used = 0;
    turret_shots = 0;
    has_shield = 0;
    has_barrier = 0;
    build_paddle(28);
    paddle_x = (PF_X + PF_X2 - paddle_w) / 2;
#ifdef TEST_START
    load_level(TEST_START);
#else
    load_level(0);
#endif
#ifdef DIAG
    turret_shots = 30;
#endif
    sync_pages();
    state = ST_PLAY;
}

static void show_clear(void)
{
    int b;
    for (b = 0; b < 2; b++) {
        u8 *p = scr[b];
        fill_rect(p, 8, 56, 208, 84, COL_DARK);
        fill_rect(p, 8, 56, 208, 2, COL_EXPL);
        fill_rect(p, 8, 138, 208, 2, COL_EXPL);
        draw_text_big(p, 44, 66, "LEVEL", COL_WHITE);
        draw_text_big(p, 16, 98, "COMPLETE", COL_EXPL);
    }
    sfx_play(SFX_LEVELCLEAR);
    state = ST_CLEAR;
    state_timer = 0;
}

static void show_pause(void)
{
    int b;
    for (b = 0; b < 2; b++) {
        u8 *p = scr[b];
        fill_rect(p, 16, 60, 192, 76, COL_DARK);
        fill_rect(p, 16, 60, 192, 2, COL_LIGHT);
        fill_rect(p, 16, 134, 192, 2, COL_LIGHT);
        draw_text_big(p, 56, 70, "PAUSED", COL_EXPL);
        draw_text_at(p, 28, 104, "ESC = END GAME", COL_WHITE);
        draw_text_at(p, 28, 118, "SPACE = CONTINUE", COL_WHITE);
    }
    state = ST_PAUSE;
    state_timer = 0;
}

static void draw_entry(void)
{
    int i;
    for (i = 0; i < 3; i++) {
        int x = 112 + i * 32;
        char c[2];
        c[0] = entry_name[i]; c[1] = 0;
        fill_rect(bg, x - 2, 98, 20, 22, i == entry_pos ? COL_METAL : COL_DARK);
        draw_text_2x(x, 101, c, i == entry_pos ? COL_WHITE : COL_LIGHT);
    }
    dirty_both(108, 96, 100, 26);
}

static void show_entry(int rank)
{
    fill_rect(bg, 0, 0, 320, 200, COL_DARK);
    draw_text_2x(48, 30, "NEW HIGH SCORE", COL_EXPL);
    {
        char b[12], *n = utoa(score, b + 11);
        while (b + 11 - n < 8) *--n = '0';
        draw_text(bg, 128, 60, n, COL_WHITE, COL_DARK);
    }
    draw_text(bg, 84, 80, "ENTER YOUR INITIALS", COL_LIGHT, COL_DARK);
    draw_text(bg, 20, 140, "MOVE MOUSE OR TYPE - CLICK TO MOVE", COL_METAL, COL_DARK);
    draw_text(bg, 60, 152, "RETURN TO SUBMIT", COL_LIGHT, COL_DARK);
    entry_name[0] = entry_name[1] = entry_name[2] = 'A';
    entry_name[3] = 0;
    entry_pos = 0;
    entry_rank = rank;
    entry_acc = 0;
    draw_entry();
    sync_pages();
    state = ST_ENTRY;
    state_timer = 0;
}

static void finish_entry(void)
{
    int i;
    for (i = NUM_HI - 1; i > entry_rank; i--) hiscores[i] = hiscores[i - 1];
    hiscores[entry_rank].score = score;
    for (i = 0; i < 4; i++) hiscores[entry_rank].name[i] = entry_name[i];
    hi_save();
    show_title();
}

static void game_over(void)
{
    log_kv("GAME OVER score=", score);
    log_str("\n");
    fill_rect(bg, 24, 84, 176, 32, COL_DARK);
    fill_rect(bg, 24, 84, 176, 1, COL_LIGHT);
    fill_rect(bg, 24, 115, 176, 1, COL_LIGHT);
    draw_text_2x(40, 92, "GAME OVER", COL_EXPL);
    dirty_both(24, 84, 176, 32);
    clear_objects();
    state = ST_OVER;
    state_timer = 0;
}

static void sfx_draw_state(void)
{
    const char *t = sfx_enabled ? "S=SFX ON " : "S=SFX OFF";
    if (state == ST_PLAY || state == ST_OVER) {
        draw_text(bg, PANEL_X + 8, 170, t, COL_METAL, COL_DARK);
        dirty_both(PANEL_X + 8, 170, 72, 8);
    } else if (state == ST_TITLE) {
        draw_text(bg, 244, 180, t, COL_METAL, COL_DARK);
        dirty_both(244, 180, 72, 8);
    }
}


#ifdef MICROBENCH
/* Cycles-per-call micro-benchmark, in the spirit of a "MICRO.PRG": run N
   calls of a routine back to back and time them on the 200 Hz timer, which
   ticks every 5 ms - cycles/call = ticks * (CPU_HZ/200) / N. Timing many
   calls in a row, rather than one, averages out the 5 ms granularity. */
#define MB_N 400
#define MB_CYC_PER_TICK 40000UL   /* 8 MHz / 200 */

static long mb_log_h = -1;

static long mb_strlen(const char *s) { const char *p = s; while (*p) p++; return p - s; }

static void mb_log(const char *s)
{
    log_str(s);
    if (mb_log_h >= 0) f_write(mb_log_h, mb_strlen(s), (void *)s);
}

static void mb_report(const char *name, u32 ticks, int pixels)
{
    char line[80], b[12];
    int i = 0, j;
    const char *p;
    u32 cyc = ticks * MB_CYC_PER_TICK / MB_N;
    for (p = name; *p; p++) line[i++] = *p;
    while (i < 26) line[i++] = ' ';
    p = utoa(cyc, b + 11);
    for (j = 0; j < 11 - (b + 11 - p); j++) line[i++] = ' ';
    for (; *p; p++) line[i++] = *p;
    line[i++] = ' '; line[i++] = 'c'; line[i++] = '/'; line[i++] = 'c'; line[i++] = 'a'; line[i++] = 'l'; line[i++] = 'l';
    if (pixels) {
        line[i++] = ' '; line[i++] = '('; 
        p = utoa(cyc / (u32)pixels, b + 11);
        for (; *p; p++) line[i++] = *p;
        line[i++] = '/'; line[i++] = 'p'; line[i++] = 'x'; line[i++] = ')';
    }
    line[i++] = '\r'; line[i++] = '\n'; line[i] = 0;
    mb_log(line);
}

#define MB_TIME(stmt, name, px) do { \
    u32 t0, t1; int k; \
    t0 = HZ200; \
    for (k = 0; k < MB_N; k++) { stmt; } \
    t1 = HZ200; \
    mb_report(name, t1 - t0, px); \
} while (0)

static void run_microbench(void)
{
    u8 *buf = scr[0];
    mb_log_h = f_create("MICRO.LOG");
    mb_log("QUANOID micro-benchmark, N=400 calls, 8 MHz assumed\r\n");
    memcpy(buf, bg, 32000);

    MB_TIME(blit(buf, 100, 80, &spr_ball), "blit ball 4x4 (old)", 16);
    MB_TIME(blit_ps(buf, 100, 80, &ps_ball), "blit ball 4x4 (preshift)", 16);
    MB_TIME(blit(buf, 100, 80, &spr_enemy[0][0][0]), "blit enemy 8x8 (old)", 64);
    MB_TIME(blit_ps(buf, 100, 80, &ps_enemy[0][0][0]), "blit enemy 8x8 (preshift)", 64);
    MB_TIME(blit(buf, 100, 80, &spr_cap[0]), "blit capsule 14x8 (old)", 112);
    MB_TIME(blit_ps(buf, 100, 80, &ps_cap[0]), "blit capsule 14x8 (preshift)", 112);
    MB_TIME(blit(buf, 100, 80, &spr_paddle), "blit paddle 28x6 (old)", 168);
    MB_TIME(blit_ps(buf, 101, 80, &ps_ball), "blit ball unaligned x=101", 16);

    /* worst-case frame: 3 balls, 3 enemies, 2 capsules, paddle - old vs new */
    MB_TIME(
        (blit(buf, 60, 80, &spr_ball), blit(buf, 90, 60, &spr_ball), blit(buf, 130, 100, &spr_ball),
         blit(buf, 40, 40, &spr_enemy[0][0][0]), blit(buf, 150, 50, &spr_enemy[1][0][0]), blit(buf, 180, 70, &spr_enemy[2][1][1]),
         blit(buf, 70, 30, &spr_cap[0]), blit(buf, 160, 90, &spr_cap[3]),
         blit(buf, 100, 184, &spr_paddle)),
        "WORST FRAME - all old", 0);
    MB_TIME(
        (blit_ps(buf, 60, 80, &ps_ball), blit_ps(buf, 90, 60, &ps_ball), blit_ps(buf, 130, 100, &ps_ball),
         blit_ps(buf, 40, 40, &ps_enemy[0][0][0]), blit_ps(buf, 150, 50, &ps_enemy[1][0][0]), blit_ps(buf, 180, 70, &ps_enemy[2][1][1]),
         blit_ps(buf, 70, 30, &ps_cap[0]), blit_ps(buf, 160, 90, &ps_cap[3]),
         blit(buf, 100, 184, &spr_paddle)),
        "WORST FRAME - new (paddle old)", 0);
    MB_TIME(restore_rect(buf, 100, 80, 4, 4), "restore 4x4 (old)", 16);
    MB_TIME(restore_rect(buf, 100, 80, 8, 8), "restore 8x8 (old)", 64);
    MB_TIME(restore_rect(buf, 100, 80, 28, 6), "restore paddle (old)", 168);
    MB_TIME(box_or(buf, 100, 80, 3, COL_EXPL), "box_or bomb 3x3 (old)", 9);

    mb_log("done\r\n");
    if (mb_log_h >= 0) f_close(mb_log_h);
    log_str("MICROBENCH complete, see MICRO.LOG\n");
}
#endif

int main(void)
{
    long ssp = sup_on();
    int i;
    u32 last_clock, missed = 0, level_missed = 0;
    u32 last_score = ~0UL;
    int last_lives = -1, last_level = -1;
    long rez = get_rez();
#ifdef TEST
    int level_frames = 0, level_shots = 0;
#endif

    nf_init();
    log_kv("START sr=", (u32)get_sr());
#ifdef TEST
    { extern void _start(void); log_kv("text=", (u32)_start); }
#endif
    log_str("\n");
    old_rez = rez;
    old_phys = phys_base();
    old_log = log_base();
    for (i = 0; i < 16; i++) old_palette[i] = ((u16 *)0xff8240)[i];

    scr[0] = alloc_screen();
    scr[1] = alloc_screen();
    bg = alloc_screen();
    if (!scr[0] || !scr[1] || !bg) {
        cconws("Not enough memory\r\n");
        sup_off(ssp);
        return 1;
    }
    if (rez != 0) set_screen(-1, -1, 0);
    { static u32 * volatile hz200 = (u32 *)0x4ba; rng = (u16)*hz200 | 1; }

    palette[COL_BG] = 0x002;
    palette[COL_METAL] = 0x333;
    palette[COL_EXPL] = COL_EXPL_RGB;
    palette[COL_CRACK] = COL_CRACK_RGB;
    palette[COL_WHITE] = 0x777;
    palette[COL_PADDLE] = 0x367;     /* light blue, used by the sphere enemies */
    palette[COL_LIGHT] = 0x555;
    palette[COL_DARK] = 0x000;

    build_sprites();
    build_enemy_sprites();
    build_all_preshift();
    build_paddle(28);
    hi_load();

    {   /* relative mouse and joystick reporting, then take over the
           keyboard interrupt and the frame timer ourselves */
        /* joystick command first: per the IKBD protocol, a joystick command
           stops port 0 being scanned as a mouse until the next mouse command */
        static const char ikbd_on[] = { 0x14, 0x08 };
        ikbdws(2, ikbd_on);
    }
    ikbd_install();
    timer_install();
    vbl_install();

#ifndef NOSOUND
    sfx_init();
#endif
#ifdef MICROBENCH
    run_microbench();
    goto shutdown;
#endif

#ifdef SFXDEMO
    {   /* play every effect once, 1.2 s apart, then quit (for recording) */
        int id, w;
        for (w = 0; w < 50; w++) { u32 c = FRCLOCK; while (FRCLOCK == c) continue; sfx_tick(); }
        for (id = 0; id < NUM_SFX; id++) {
            char b[12];
            log_str("SFX "); log_str(utoa((u32)id, b + 11)); log_str(" "); log_str(sfx_name[id]); log_str("\n");
            sfx_play(id);
            for (w = 0; w < 60; w++) { u32 c = FRCLOCK; while (FRCLOCK == c) continue; sfx_tick(); }
        }
        state = ST_QUIT;
    }
#endif
    show_title();
    show_buffer(scr[0]);
    set_palette();

    last_clock = vbl_count;
    while (state != ST_QUIT) {
        u8 *buf = scr[back];
        int dx, fire_edge, key_ascii = 0, key_esc = 0, key_enter = 0, key_space = 0;
        frame_no++;
        state_timer++;
        bricks_drawn_frame = hud_drawn_frame = 0;

        /* ---- input ---- */
        dx = (int)take_mouse_dx();
        fire_edge = 0;
        {
            long sc;
            while ((sc = take_key()) != 0) {
                int scan = (int)sc;
                if (scan == 0x01) key_esc = 1;
                else if (scan == 0x4b) dx -= 8;                    /* left */
                else if (scan == 0x4d) dx += 8;                    /* right */
                else if (scan == 0x39) { fire_edge = 1; key_space = 1; }
                else if (scan == 0x1c || scan == 0x72) key_enter = 1;
                else if (scan == 0x0e) key_ascii = 8;              /* backspace */
                else if (scan == 0x1f) { sfx_enabled ^= 1; sfx_draw_state(); }
                else {
                    int i2;
                    for (i2 = 0; i2 < 26; i2++)
                        if (scan_letter[i2] == scan) { key_ascii = 'A' + i2; break; }
                }
            }
        }
        {
            u8 j = joy1;
            if (j & 4) dx -= 4;
            if (j & 8) dx += 4;
            if (take_click()) fire_edge = 1;
#ifdef DIAG
#endif
        }
#if defined(TEST) && !defined(NO_AUTOPILOT)
        {   /* autopilot, with a deliberate "asleep" window to exercise game over */
            static u16 lfsr = 0xace1;
            static int aim;
            int asleep = (frame_no % 24000) > 20000;
            if (state == ST_PLAY && !asleep) {
                Ball *t = 0;
                s32 best = -1;
                for (i = 0; i < MAX_BALLS; i++)
                    if (balls[i].active && balls[i].y > best) { best = balls[i].y; t = &balls[i]; }
                if (balls[0].vy < 0 && (frame_no & 15) == 0) {
                    lfsr = (u16)((lfsr >> 1) ^ (-(lfsr & 1) & 0xb400));
                    aim = (int)(lfsr % 25) - 12;
                }
                if (t) dx = (int)(t->x >> 8) + 2 - paddle_w / 2 + aim - paddle_x;
            } else if (state == ST_PLAY) {
                dx = (frame_no & 64) ? 2 : -2;
            }
            if ((frame_no & 31) == 0) fire_edge = 1;
            if (state == ST_PLAY && frame_no == 1200) key_esc = 1;
            if (state == ST_PAUSE) {
                if (state_timer == 10) save_shot(scr[1 - back], "pause");
                if (state_timer > 20) key_space = 1;
            }
            if (state == ST_ENTRY && (state_timer % 40) == 0) key_enter = 1;
            if (state == ST_ENTRY) { key_ascii = 0; fire_edge = (state_timer % 20) == 0; if (state_timer == 5) key_ascii = 'T'; }
        }
#endif

        /* ---- update ---- */
        switch (state) {
        case ST_TITLE:
            if (key_esc) state = ST_QUIT;
            else if (fire_edge && state_timer > 10) {
                sfx_play(SFX_MENUCLICK);
                start_game();
                last_clock = vbl_count;
            }
#ifdef TEST
            if (state_timer == 5 || (state_timer % 250) == 0) save_shot(scr[1 - back], "title");
#endif
            break;

        case ST_OVER:
            if ((fire_edge && state_timer > 50) || state_timer > 200) {
                int r = hi_rank(score);
                if (r >= 0) { show_entry(r); sfx_play(SFX_WIN); } else show_title();
                last_clock = vbl_count;
            }
#ifdef TEST
            if (state_timer == 30) save_shot(scr[1 - back], "game over");
#endif
            break;

        case ST_ENTRY:
            entry_acc += dx;
            if (entry_acc >= 12 || entry_acc <= -12) {
                int d = entry_acc > 0 ? 1 : -1;
                entry_acc = 0;
                entry_name[entry_pos] = (char)(entry_name[entry_pos] + d);
                if (entry_name[entry_pos] > 'Z') entry_name[entry_pos] = 'A';
                if (entry_name[entry_pos] < 'A') entry_name[entry_pos] = 'Z';
                sfx_play(SFX_MENUHOVER);
                draw_entry();
            }
            if (key_ascii == 8) {
                if (entry_pos > 0) entry_pos--;
                draw_entry();
            } else if (key_ascii) {
                entry_name[entry_pos] = (char)key_ascii;
                if (entry_pos < 2) entry_pos++;
                draw_entry();
            }
            if (key_enter && state_timer > 10) {          /* Return submits */
                sfx_play(SFX_MENUCLICK);
                entry_pos = 3;
            } else if (fire_edge && state_timer > 10) {   /* click moves along */
                sfx_play(SFX_MENUCLICK);
                entry_pos++;
                if (entry_pos >= 3) entry_pos = 0;
                draw_entry();
            }
            if (entry_pos >= 3) {
                {
#ifdef TEST
                    save_shot(scr[1 - back], "entry");
#endif
                    finish_entry();
                }
                last_clock = vbl_count;
            }
            break;

        case ST_CLEAR:
#ifdef CLEARTEST
            if (state_timer == 20) save_shot(scr[1 - back], "level complete");
#endif
            if (state_timer >= 150) {           /* three seconds */
                load_level((level_idx + 1) % NUM_LEVELS);
                sync_pages();
                state = ST_PLAY;
                last_clock = vbl_count;
            }
            break;

        case ST_PAUSE:
            if (key_space) {            /* resume: repaint everything from the background */
                dirty_both(0, 0, 320, 200);
                state = ST_PLAY;
                last_clock = vbl_count;
            } else if (key_esc && state_timer > 10) {
                show_title();
                last_clock = vbl_count;
            }
            break;

        case ST_PLAY:
            if (key_esc) { show_pause(); last_clock = vbl_count; break; }
            paddle_x += dx;
            if (paddle_x < PF_X) paddle_x = PF_X;
            if (paddle_x > PF_X2 - paddle_w) paddle_x = PF_X2 - paddle_w;

            process_pending();
#ifdef ENEMYDEBUG
            {   /* debug: no ball, so nothing kills enemies or ends the level */
                int k;
                for (k = 0; k < MAX_BALLS; k++) balls[k].active = 0;
                ball_stuck = 1;
            }
#endif
            if (ball_stuck) {
                Ball *b = &balls[0];
                b->x = (s32)(paddle_x + paddle_w / 2 + 1) << 8;
                b->y = (s32)(PAD_Y - BALL_S) << 8;
                if (fire_edge) {
#ifdef ENEMYDEBUG
                    ;
#else
                    ball_stuck = 0;
#endif
                    b->x -= 1 << 8;
                    set_ball_dir(b, 20);
                    fire_edge = 0;
                }
            } else {
                int alive = 0;
                if (fire_edge) fire_turrets();
                for (i = 0; i < MAX_BALLS; i++) {
                    Ball *b = &balls[i];
                    if (!b->active) continue;
                    if (move_ball(b)) b->active = 0;
                    else alive++;
                }
                if (enemy_hit_paddle) {
                    enemy_hit_paddle = 0;
                    for (i = 0; i < MAX_BALLS; i++) balls[i].active = 0;
                    alive = 0;
                }
                if (boss_active && boss_update()) {
                    for (i = 0; i < MAX_BALLS; i++) balls[i].active = 0;
                    alive = 0;
                }
                if (++frames_since_hit > 500)       /* anti-stall bend */
                    for (i = 0; i < MAX_BALLS; i++) if (balls[i].active) balls[i].vy += 3;
                if (!alive) {
                    clear_objects();
                    if (paddle_w != 28) build_paddle(28);
                    /* losing a life clears every active power-up, not just the
                       ball and paddle size - otherwise a shield, barrier or
                       turret ammo picked up before the drop would carry over */
                    fireball_timer = 0;
                    turret_shots = 0;
                    has_shield = 0;
                    if (has_barrier) { has_barrier = 0; draw_barrier(); }
                    life_lost_level = 1;
                    if (--lives <= 0) {
                        lives = 0;
                        draw_hud();
                        sfx_play(SFX_GAMEOVER);
                        game_over();
                        break;
                    }
                    sfx_play(SFX_LOSELIFE);
                    reset_ball();
                }
            }
            /* the countdown to the exit starts when only a few bricks are left */
            if (!exit_offered && !exit_open && !boss_active && bricks_left > 0 && bricks_left <= 3) {
                if (exit_wait < 0) {
                    exit_wait = 60 * 50;      /* 60 s from now */
#ifdef TEST
                    log_kv("EXITTIMER started lvl=", (u32)level_idx + 1);
                    log_kv(" bricks_left=", (u32)bricks_left);
                    log_str("\n");
                    exit_hud_shot_in = 100;
#endif
                }
                else if (exit_wait > 0) exit_wait--;
            }
#ifdef EXITTEST
            if (level_frames == 400) { bricks_left = 3; exit_wait = 25; }
            if (level_frames == 700) { exit_open = 1; draw_exit_door(); }
            if (level_frames == 740) save_shot(scr[1 - back], "exit door");
            if (exit_open && (state_timer % 200) == 0) save_shot(scr[1 - back], "exit door");
#endif
#ifdef CLEARTEST
            if (level_frames == 500) bricks_left = 0;
#endif
            if (!exit_offered && !exit_open && !life_lost_level && exit_wait == 0) {
                int fc = free_cap();
                if (fc < 0) {
                    exit_wait = 25;    /* every capsule slot is full: try again in .5 s */
                } else {              /* the way out: a ? capsule */
                    exit_offered = 1;
                    spawn_cap(PF_X + (PF_X2 - PF_X) / 2, BR_Y + 8, PU_EXIT, -1);
                }
            }
#ifdef CAPTEST
            if (level_frames == 100) {
                apply_power(PU_TURRET);
                apply_power(PU_SHIELD);
                apply_power(PU_BARRIER);
                apply_power(PU_EXTEND);
                log_kv("BEFORE turret=", (u32)turret_shots);
                log_kv(" shield=", (u32)has_shield);
                log_kv(" barrier=", (u32)has_barrier);
                log_kv(" paddle_w=", (u32)paddle_w);
                log_str("\n");
            }
            if (level_frames == 110) {
                for (i = 0; i < MAX_BALLS; i++) balls[i].active = 0;   /* force a life loss */
            }
            if (level_frames == 130) {
                log_kv("AFTER turret=", (u32)turret_shots);
                log_kv(" shield=", (u32)has_shield);
                log_kv(" barrier=", (u32)has_barrier);
                log_kv(" paddle_w=", (u32)paddle_w);
                log_str("\n");
                save_shot(scr[1 - back], "after life loss");
            }
            if (level_frames == 130) save_shot(scr[1 - back], "turret cap");
#endif
            enemies_update();
            if (enemy_hit_paddle) {
                enemy_hit_paddle = 0;
                for (i = 0; i < MAX_BALLS; i++) balls[i].active = 0;
                ball_stuck = 1;
                if (--lives <= 0) { lives = 0; draw_hud(); sfx_play(SFX_GAMEOVER); game_over(); break; }
                sfx_play(SFX_LOSELIFE);
                clear_objects();
                reset_ball();
            }
            if (fireball_timer) fireball_timer--;
            update_caps();
            update_bullets();
            draw_boss_hud();
            draw_exit_hud();
#ifdef TEST
            if (exit_hud_shot_in > 0 && --exit_hud_shot_in == 0) save_shot(scr[1 - back], "exit hud");
#endif

            if (exit_open && paddle_x + paddle_w >= PF_X2 - 1) {
                bricks_left = 0;                     /* walked out through the door */
            }
            if (bricks_left <= 0) {
#ifdef TEST
                log_kv("CLEAR level=", (u32)level_idx + 1);
                log_kv("frames=", (u32)level_frames);
                log_kv("missed=", level_missed);
                log_str("\n");
#endif
                show_clear();
                last_clock = vbl_count;
#ifdef TEST
                level_frames = 0;
#endif
            }
#ifdef TEST
            if (++level_frames == 900 && level_shots < 8) {
                save_shot(scr[1 - back], "play");
                level_shots++;
                last_clock = vbl_count;
            }
            if (level_frames >= TEST_FRAMES_PER_LEVEL) {
                log_kv("TIMEOUT level=", (u32)level_idx + 1);
                log_kv("left=", (u32)bricks_left);
                log_kv("missed=", level_missed);
                log_str("\n");
                load_level((level_idx + 1) % NUM_LEVELS);
                sync_pages();
                level_missed = 0;
                last_clock = vbl_count;
                level_frames = 0;
            }
#endif
            if (lives != last_lives || level_idx != last_level) {
                last_score = score; last_lives = lives; last_level = level_idx;
                draw_hud();
            } else if (score != last_score) {
                last_score = score;
                draw_score();
            }
            draw_power_hud();
            break;
        }
#ifdef TEST
        if ((frame_no % 250) == 0) {
            int ne = 0, nb = 0, k;
            for (k = 0; k < MAX_ENEMIES; k++) ne += enemies[k].active;
            for (k = 0; k < MAX_BOMBS; k++) nb += bombs[k].active;
            log_kv("POP lvl=", (u32)level_idx + 1);
            log_kv("enemies=", (u32)ne);
            log_kv("bombs=", (u32)nb);
            log_str("\n");
        }
        if ((frame_no % 1000) == 0) {
        #ifdef PIXCHECK
    log_kv("DRAWVISIBLE total=", draw_on_visible);
    log_kv("LATEFLIP total=", late_flip);
    log_str("\n");
#endif
    log_kv("DIRTY peak=", (u32)dirty_peak);
            log_kv("overflow=", dirty_overflow);
            log_kv("balls=", (u32)(balls[0].active + balls[1].active + balls[2].active));
            log_str("\n");
            dirty_peak = 0;
        }
        if (frame_no >= TEST_TOTAL_FRAMES) state = ST_QUIT;
#endif

#ifdef PIXCHECK
        {   /* is the hardware showing the buffer we are about to draw into? */
            u32 shown = ((u32)(*(u8 * volatile)0xff8201) << 16) | ((u32)(*(u8 * volatile)0xff8203) << 8);
            if (shown == (u32)buf) {
                draw_on_visible++;
                if (draw_on_visible < 6) { log_kv("DRAWVISIBLE frame=", (u32)frame_no); log_str("\n"); }
            }
        }
#endif
        /* ---- render into back buffer (frozen while paused) ---- */
        if (state == ST_PAUSE || state == ST_CLEAR) goto flip;
        for (i = 0; i < ndirty[back]; i++) {
            Rect *r = &dirty[back][i];
            restore_rect(buf, r->x, r->y, r->w, r->h);
        }
        ndirty[back] = 0;
        if (state == ST_PLAY || state == ST_OVER) {
            for (i = 0; i < MAX_CAPS; i++) {
                Cap *c = &caps[i];
                if (!c->active) continue;
                blit_ps(buf, c->x, (int)(c->y >> 8), &ps_cap[c->type]);
                add_dirty(back, c->x, (int)(c->y >> 8), CAP_W, CAP_H);
            }
            for (i = 0; i < MAX_BULLETS; i++) {
                Bullet *u = &bullets[i];
                if (!u->active) continue;
                blit(buf, u->x, u->y, &spr_bullet);
                add_dirty(back, u->x, u->y, 2, 7);
            }
#ifdef DIAG
            {
                static int shot_frames = 0;
                int nb = 0;
                for (i = 0; i < MAX_BULLETS; i++) nb += bullets[i].active;
                if (nb && ++shot_frames % 12 == 6 && shots_taken < 4) save_shot(scr[1 - back], "bullets");
            }
#endif
            if (has_shield != paddle_shielded) {
                paddle_shielded = has_shield;
                build_paddle(paddle_w);
            }
            blit(buf, paddle_x, PAD_Y, &spr_paddle);
            add_dirty(back, paddle_x, PAD_Y, paddle_w, PAD_H);
            if (exit_open && (frame_no & 8)) {     /* blinking arrow at the door */
                draw_text(buf, PF_X2 - 9, EXIT_Y + 2, ">", COL_EXPL, COL_BG);
                add_dirty(back, PF_X2 - 9, EXIT_Y + 2, 8, 8);
            }
            if (has_shield) {          /* force field: dotted bar over the paddle */
                int sx2, phase = (frame_no >> 2) & 1;
                for (sx2 = paddle_x + phase; sx2 < paddle_x + paddle_w; sx2 += 2)
                    pset(buf, sx2, PAD_Y - 3, COL_WHITE);
                add_dirty(back, paddle_x, PAD_Y - 3, paddle_w, 1);
            }
            if (turret_shots) {
                blit(buf, paddle_x, PAD_Y - 3, &spr_nub);
                blit(buf, paddle_x + paddle_w - 2, PAD_Y - 3, &spr_nub);
                add_dirty(back, paddle_x, PAD_Y - 3, 2, 3);
                add_dirty(back, paddle_x + paddle_w - 2, PAD_Y - 3, 2, 3);
            }
            if (state == ST_PLAY) { boss_draw(buf); enemies_draw(buf); }
            if (state == ST_PLAY)
                for (i = 0; i < MAX_BALLS; i++) {
                    Ball *b = &balls[i];
                    int bx = (int)(b->x >> 8), by = (int)(b->y >> 8);
                    if (!b->active || by >= 200 - BALL_S) continue;
                    blit_ps(buf, bx, by, fireball_timer ? &ps_fireball : &ps_ball);
                    add_dirty(back, bx, by, BALL_S, BALL_S);
#ifdef PIXCHECK
                    {   /* read back a pixel that must be white after the blit */
                        int px = bx + 1, py = by + 1, pl, c = 0;
                        u16 *p = (u16 *)(buf + py * 160) + (px >> 4) * 4;
                        u16 m = (u16)(0x8000 >> (px & 15));
                        for (pl = 0; pl < 4; pl++) if (p[pl] & m) c |= 1 << pl;
                        if (c != COL_WHITE) {
                            ball_bad++;
                            if (ball_bad < 12) {
                                log_kv("BALLPIX col=", (u32)c);
                                log_kv("x=", (u32)bx); log_kv("y=", (u32)by);
                                log_str("\n");
                            }
                        }
                    }
#endif
                }
        }

        /* ---- flip on VBL ---- */
flip:
        {   /* Ask for the flip, then wait for the blank that carries it out.
               The ST only reloads the screen address at the blank, so the old
               buffer stays visible until then: starting to draw before the
               flip has happened means drawing into the picture being shown,
               which erases objects from the top down. Timer C is the watchdog
               in case the blank interrupt ever stops. */
            u32 v = vbl_count, t = tick50;
            show_buffer(buf);
            while (vbl_count == v && (tick50 - t) < 3) continue;
        }
        {
            u32 now = vbl_count;
            if (now - last_clock > 1 && state == ST_PLAY) {
                missed += now - last_clock - 1;
                level_missed += now - last_clock - 1;
#ifdef TEST
                {
                    int nb = 0, nc = 0, nu = 0;
                    for (i = 0; i < MAX_BALLS; i++) nb += balls[i].active;
                    for (i = 0; i < MAX_CAPS; i++) nc += caps[i].active;
                    for (i = 0; i < MAX_BULLETS; i++) nu += bullets[i].active;
                    log_kv("MISS n=", now - last_clock - 1);
                    log_kv("balls=", (u32)nb); log_kv("caps=", (u32)nc); log_kv("bul=", (u32)nu);
                    log_kv("fire=", (u32)fireball_timer); log_kv("pend=", (u32)npend);
                    log_kv("dirty=", (u32)ndirty[back]); log_kv("bricks=", (u32)bricks_drawn_frame); log_kv("hud=", (u32)hud_drawn_frame); log_str("\n");
                }
#endif
            }
            last_clock = now;
        }
#if !defined(SFX_VBL) && !defined(NOSOUND)
        sfx_tick();                          /* one sound frame per screen frame */
#endif
#ifdef SNDDIAG
        if ((frame_no % 100) == 0) sfx_play(SFX_MENUCLICK);       /* heartbeat beep */
        if ((frame_no % 25) == 0) {
            draw_text(bg, PANEL_X + 4, 128, "PLAYED", COL_LIGHT, COL_DARK);
            draw_number(PANEL_X + 56, 128, sfx_plays % 10000, 4);
            draw_text(bg, PANEL_X + 4, 138, "TICKS", COL_LIGHT, COL_DARK);
            draw_number(PANEL_X + 56, 138, sfx_ticks % 10000, 4);
            draw_text(bg, PANEL_X + 4, 148, "VOL", COL_LIGHT, COL_DARK);
            draw_number(PANEL_X + 32, 148, (u32)last_vol[0], 2);
            draw_number(PANEL_X + 50, 148, (u32)last_vol[1], 2);
            draw_number(PANEL_X + 68, 148, (u32)last_vol[2], 2);
            draw_text(bg, PANEL_X + 4, 158, "ON", COL_LIGHT, COL_DARK);
            draw_number(PANEL_X + 32, 158, (u32)sfx_enabled, 1);
        }
#endif
        back ^= 1;
    }

#ifdef PIXCHECK
    log_kv("DRAWVISIBLE total=", draw_on_visible);
    log_kv("LATEFLIP total=", late_flip);
    log_str("\n");
#endif
    log_kv("DIRTY peak=", (u32)dirty_peak);
    log_kv("overflow=", dirty_overflow);
    log_str("\n");
    log_kv("SPRPOOL used=", (u32)spr_used);
    log_kv("END frames=", (u32)frame_no);
    log_kv("missed=", missed);
    log_kv("score=", score);
    log_str("\nPOWERUPS");
    for (i = 0; i < PU_COUNT; i++) {
        char b[4] = " x=";
        b[1] = pu_text[i][0];
        log_kv(b, pu_collected[i]);
    }
    log_str("\n");

#ifdef MICROBENCH
shutdown:
#endif
#ifndef NOSOUND
    sfx_exit();
#endif
    {
        static const char ikbd_off[] = { 0x08 };
        ikbdws(1, ikbd_off);
    }
#ifndef EXIT_NORESTORE
    vbl_remove2();
    timer_remove();
    ikbd_remove();
    /* hand the screen back through XBIOS rather than the hardware registers,
       and let one frame pass before returning to TOS */
    set_screen(old_log, old_phys, old_rez);
    set_palette_xbios(old_palette);
    {
        u32 c = FRCLOCK;
        while (FRCLOCK == c) continue;
        c = FRCLOCK;
        while (FRCLOCK == c) continue;
    }
#endif
#ifdef EXIT_NOTERM
    for (;;) continue;                /* diagnostic: never return to TOS */
#endif
    sup_off(ssp);
    return 0;
}
