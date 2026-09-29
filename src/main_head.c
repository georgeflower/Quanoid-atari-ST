/* Qarkanoid - Atari ST port of the Qarkanoid browser game
 * milestone 4: power-ups, title, high scores
 * 320x200 low res, double buffer, dirty rectangles, 50 Hz.
 */
#include "levels.h"

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef short s16;
typedef long s32;
typedef unsigned long u32;

/* ---- system (sys.S / crt0.S) ---- */
long sup_on(void);
void sup_off(long);
long get_rez(void);
long phys_base(void);
long log_base(void);
void set_palette_xbios(const u16 *);
void dosound(const char *);
void set_screen(long logb, long physb, long rez);
long kbdvbase(void);
long malloc_st(long);
long con_stat(void);
long con_in(void);
long cconws(const char *);
long f_create(const char *);
long f_write(long, long, void *);
long f_close(long);
long f_open(const char *, long);
long f_read(long, long, void *);

long get_sr(void);
void ikbd_install(void);
void ikbd_remove(void);
void timer_install(void);
void timer_remove(void);
long take_key(void);
extern volatile unsigned long tick50;
extern volatile unsigned long vbl_count;
void vbl_install(void);
void vbl_remove2(void);
void nf_print(const char *);
void nf_init(void);
int main(void);

extern unsigned char joy1;
long take_mouse_dx(void);
long take_click(void);
long irq_off(void);
void irq_restore(long);
void line_asm(u8 *buf, int x0, int y0, int x1, int y1, int col);
void restore_asm(u8 *dst, const u8 *src, int groups, int rows);
void line_plane(u8 *buf, int x0, int y0, int x1, int y1, int plane);
void blit_pre(u8 *buf, int gx, int y, int h, const u16 *tab);
#define VERSION "V2.8"
long ikbdws(long count, const char *buf);
extern unsigned char mouse_btn;

void *memcpy(void *d, const void *s, unsigned long n);
void *memset(void *d, int c, unsigned long n);
void *memcpy(void *d, const void *s, unsigned long n)
{
    u8 *a = d; const u8 *b = s;
    while (n--) *a++ = *b++;
    return d;
}
void *memset(void *d, int c, unsigned long n)
{
    u8 *a = d;
    while (n--) *a++ = (u8)c;
    return d;
}

/* ---- build options ---- */
#ifdef TEST
#ifndef TEST_FRAMES_PER_LEVEL
#define TEST_FRAMES_PER_LEVEL 3000
#endif
#ifndef TEST_TOTAL_FRAMES
#define TEST_TOTAL_FRAMES     60000
#endif
#endif

/* ---- layout ---- */
#define PF_X    8          /* playfield inner left */
#define PF_X2   216        /* playfield inner right (exclusive) */
#define PF_Y    8          /* inner top */
#define BR_Y    16         /* first brick row */
#define BCOLS   13
#define BROWS   14
#define PAD_Y   184
#define PAD_H   6
#define BALL_S  4
#define PANEL_X 224

#define COL_BG 0
#define COL_METAL 9
#define COL_EXPL 10
#define COL_CRACK 11
#define COL_WHITE 12
#define COL_PADDLE 13
#define COL_LIGHT 14
#define COL_DARK 15

static u32 * volatile frclock_p = (u32 *)0x466;
static u32 * volatile hz200_p = (u32 *)0x4ba;
#define FRCLOCK (*frclock_p)
#define HZ200 (*hz200_p)

static const short ang_sin[33]={-207,-198,-188,-177,-166,-155,-142,-129,-116,-103,-89,-74,-60,-45,-30,-15,0,15,30,45,60,74,89,103,116,129,142,155,166,177,188,198,207};
static const short ang_cos[33]={150,162,174,185,195,204,213,221,228,235,240,245,249,252,254,256,256,256,254,252,249,245,240,235,228,221,213,204,195,185,174,162,150};

/* ---- output helpers ---- */
static char *utoa(u32 v, char *end)
{
    *end = 0;
    do { *--end = (char)('0' + v % 10); v /= 10; } while (v);
    return end;
}
static void log_str(const char *s) { nf_print(s); }
static void log_kv(const char *k, u32 v)
{
    char b[12];
    nf_print(k);
    nf_print(utoa(v, b + 11));
    nf_print(" ");
}

/* ---- screen buffers ---- */
static u8 *scr[2];
static u8 *bg;
static int back = 1;
static u16 palette[16];
static u16 old_palette[16];
static long old_phys;
static long old_log;
static long old_rez;

static u8 *alloc_screen(void)
{
    u32 p = (u32)malloc_st(32000 + 512);
    if ((long)p <= 0) return 0;
    p = (p + 255) & ~255UL;         /* STFM video base: 256-byte boundary */
    memset((u8 *)p, 0, 32000);
    return (u8 *)p;
}

static void show_buffer(u8 *b)
{
    u32 a = (u32)b;
    *(u8 *)0xff8201 = (u8)(a >> 16);
    *(u8 *)0xff8203 = (u8)(a >> 8);
}

static void set_palette(void)
{
    int i;
    for (i = 0; i < 16; i++) ((u16 *)0xff8240)[i] = palette[i];
}

/* ---- drawing ---- */
static void fill_rect(u8 *buf, int x, int y, int w, int h, int col)
{
    int x2 = x + w - 1, gx = x >> 4, ge = x2 >> 4, g, pl, yy;
    u16 mf = (u16)(0xffff >> (x & 15));
    u16 ml = (u16)(0xffff << (15 - (x2 & 15)));
    for (yy = y; yy < y + h; yy++) {
        u16 *row = (u16 *)(buf + yy * 160);
        for (g = gx; g <= ge; g++) {
            u16 m = 0xffff, *p = row + g * 4;
            if (g == gx) m &= mf;
            if (g == ge) m &= ml;
            for (pl = 0; pl < 4; pl++) {
                if ((col >> pl) & 1) p[pl] |= m;
                else p[pl] &= (u16)~m;
            }
        }
    }
}

static void plot(u8 *buf, int x, int y, int col)
{
    fill_rect(buf, x, y, 1, 1, col);
}

static void restore_rect(u8 *buf, int x, int y, int w, int h)
{
    int gx, n, yy;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 320) w = 320 - x;
    if (y + h > 200) h = 200 - y;
    if (w <= 0 || h <= 0) return;
    gx = x >> 4;
    n = ((x + w - 1) >> 4) - gx + 1;
    restore_asm(buf + y * 160 + gx * 8, bg + y * 160 + gx * 8, n, h);
    (void)yy;
}

/* sprites: per row, per 16-px group: mask, p0..p3 */
typedef struct { s16 w, h, ng, cap; u16 *d; } Sprite;
static u16 spr_pool[3072];
static int spr_used;

static void make_sprite(Sprite *s, const char *pix, int w, int h)
{
    int r, c, g;
    s->w = (s16)w; s->h = (s16)h; s->ng = (s16)((w + 15) >> 4);
    if (!s->d || s->cap < h * s->ng * 5) {
        s->d = spr_pool + spr_used;
        s->cap = (s16)(h * s->ng * 5);
        spr_used += s->cap;
    }
    memset(s->d, 0, (unsigned long)h * s->ng * 5 * 2);
    for (r = 0; r < h; r++)
        for (c = 0; c < w; c++) {
            char ch = pix[r * w + c];
            int col, pl;
            u16 bit = (u16)(0x8000 >> (c & 15));
            u16 *gp;
            if (ch == ' ') continue;
            col = (ch <= '9') ? ch - '0' : ch - 'a' + 10;
            g = c >> 4;
            gp = s->d + (r * s->ng + g) * 5;
            gp[0] |= bit;
            for (pl = 0; pl < 4; pl++)
                if ((col >> pl) & 1) gp[1 + pl] |= bit;
        }
}

static void blit(u8 *buf, int x, int y, const Sprite *s)
{
    int sh = x & 15, gx = x >> 4, r, i, pl;
    const u16 *d = s->d;
    for (r = 0; r < s->h; r++) {
        u16 *row = (u16 *)(buf + (y + r) * 160) + gx * 4;
        for (i = 0; i < s->ng; i++, d += 5) {
            u32 m = ((u32)d[0] << 16) >> sh;
            u16 mh = (u16)(m >> 16), ml = (u16)m;
            u16 *a = row + i * 4, *b = a + 4;
            if (!m) continue;
            for (pl = 0; pl < 4; pl++) {
                u32 v = ((u32)d[1 + pl] << 16) >> sh;
                a[pl] = (u16)((a[pl] & ~mh) | (u16)(v >> 16));
                b[pl] = (u16)((b[pl] & ~ml) | (u16)v);
            }
        }
    }
}

/* ---- font (own 5x7 glyphs in 8x8 cells) ---- */
#include "font.h"

static void draw_text(u8 *buf, int x, int y, const char *str, int col, int bgc)
{
    for (; *str; str++, x += 8) {
        int ch = (u8)*str, r, pl, sh = x & 15;
        const u8 *gl;
        if (ch < 32 || ch > 127) ch = 32;
        gl = font8[ch - 32];
        for (r = 0; r < 8; r++) {
            u16 *a = (u16 *)(buf + (y + r) * 160) + (x >> 4) * 4, *b = a + 4;
            u32 m = 0xff000000UL >> sh, g = (u32)gl[r] << 24 >> sh;
            u16 mh = (u16)(m >> 16), ml = (u16)m, gh = (u16)(g >> 16), gl2 = (u16)g;
            for (pl = 0; pl < 4; pl++) {
                u16 vh = 0, vl = 0;
                if ((col >> pl) & 1) { vh |= gh; vl |= gl2; }
                if ((bgc >> pl) & 1) { vh |= mh & ~gh; vl |= ml & ~gl2; }
                a[pl] = (u16)((a[pl] & ~mh) | vh);
                if (ml) b[pl] = (u16)((b[pl] & ~ml) | vl);
            }
        }
    }
}

/* ---- dirty rects ---- */
typedef struct { s16 x, y, w, h; } Rect;
#define MAX_DIRTY 96
static Rect dirty[2][MAX_DIRTY];
static int ndirty[2];

static u32 dirty_overflow;
static u16 dirty_peak;

static void add_dirty(int b, int x, int y, int w, int h)
{
    Rect *r;
    if (ndirty[b] > (int)dirty_peak) dirty_peak = (u16)ndirty[b];
    /* Merging overlapping rectangles was measured to cost more than the
       restores it saved (2285 vs 1523 missed frames per 30000), so the list
       is simply 96 entries deep with a whole-screen fallback. */
    if (ndirty[b] >= MAX_DIRTY) {          /* still full: restore everything */
        dirty_overflow++;
        ndirty[b] = 1;
        dirty[b][0].x = 0; dirty[b][0].y = 0;
        dirty[b][0].w = 320; dirty[b][0].h = 200;
        return;
    }
    r = &dirty[b][ndirty[b]++];
    r->x = (s16)x; r->y = (s16)y; r->w = (s16)w; r->h = (s16)h;
}
static void dirty_both(int x, int y, int w, int h)
{
    add_dirty(0, x, y, w, h);
    add_dirty(1, x, y, w, h);
}

