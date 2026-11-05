| Hot per-frame routines moved out of C: rectangle fill, text, the dirty
| rectangle list, brick/enemy/boss hit tests and the ball stepper.
| All follow the m68k C ABI: args on the stack, d0/d1/a0/a1 scratch,
| d2-d7/a2-a6 preserved. The constants below must match main.c; main.c
| has _Static_asserts for every one of them.

	.equ	PF_X, 8
	.equ	PF_X2, 216
	.equ	PF_Y, 8
	.equ	BR_Y, 16
	.equ	BCOLS, 13
	.equ	BROWS, 14
	.equ	BALL_S, 4
	.equ	PAD_Y, 184
	.equ	BARRIER_Y, 196
	.equ	MAX_DIRTY, 96
	.equ	MAX_ENEMIES, 3
	.equ	ENEMY_SIZE, 12
	.equ	ENEMY_STRIDE, 24
	.equ	ENEMY_ACTIVE, 16
	.equ	B_METAL, 2
	.equ	SFX_BOUNCE, 0
	.equ	SFX_BARRIERSAVE, 11

	.text

| ---------------------------------------------------------------------
| void fill_rect(u8 *buf, int x, int y, int w, int h, int col)
| Solid fill of a rectangle in one of the 16 colours; neighbours are kept.
	.macro	PUTG
	btst	#0,%d4
	beq.s	1f
	or.w	%d7,(%a1)+
	bra.s	2f
1:	and.w	%d0,(%a1)+
2:	btst	#1,%d4
	beq.s	1f
	or.w	%d7,(%a1)+
	bra.s	2f
1:	and.w	%d0,(%a1)+
2:	btst	#2,%d4
	beq.s	1f
	or.w	%d7,(%a1)+
	bra.s	2f
1:	and.w	%d0,(%a1)+
2:	btst	#3,%d4
	beq.s	1f
	or.w	%d7,(%a1)+
	bra.s	2f
1:	and.w	%d0,(%a1)+
2:
	.endm

	.globl	fill_rect
fill_rect:
	movem.l	%d2-%d7,-(%sp)
	move.l	28(%sp),%a0		| buf
	move.l	32(%sp),%d0		| x
	move.l	36(%sp),%d1		| y
	move.l	40(%sp),%d2		| w
	move.l	44(%sp),%d3		| h
	move.l	48(%sp),%d4		| col
	tst.l	%d2
	ble	fr_done
	tst.l	%d3
	ble	fr_done
	mulu.w	#160,%d1
	add.l	%d1,%a0
	move.l	%d0,%d5
	add.l	%d2,%d5
	subq.l	#1,%d5			| x2
	moveq	#15,%d6
	and.l	%d0,%d6
	move.w	#-1,%d1
	lsr.w	%d6,%d1			| d1 = first-group mask
	moveq	#15,%d7
	and.l	%d5,%d7
	neg.l	%d7
	add.l	#15,%d7
	move.w	#-1,%d2
	lsl.w	%d7,%d2			| d2 = last-group mask
	lsr.l	#4,%d0
	lsr.l	#4,%d5
	sub.l	%d0,%d5			| d5 = groups after the first
	lsl.l	#3,%d0
	add.l	%d0,%a0
	subq.l	#1,%d3
fr_row:
	move.l	%a0,%a1
	move.w	%d1,%d7
	tst.w	%d5
	bne	fr_multi
	and.w	%d2,%d7
	move.w	%d7,%d0
	not.w	%d0
	PUTG
	bra	fr_next
fr_multi:
	move.w	%d7,%d0
	not.w	%d0
	PUTG
	move.w	%d5,%d6
	subq.w	#2,%d6
	bmi	fr_last
	moveq	#-1,%d7
	moveq	#0,%d0
fr_mid:
	PUTG
	dbra	%d6,fr_mid
fr_last:
	move.w	%d2,%d7
	move.w	%d7,%d0
	not.w	%d0
	PUTG
fr_next:
	lea	160(%a0),%a0
	dbra	%d3,fr_row
fr_done:
	movem.l	(%sp)+,%d2-%d7
	rts

| ---------------------------------------------------------------------
| void draw_text_a(u8 *buf, int x, int y, const char *s, int col, int bgc,
|                  const u8 *font)       font = 96 glyphs x 8 row bytes
| 8x8 cells, glyph in the top 5 bits of each row byte as in font.h.
	.macro	TFLAG	p
	moveq	#0,%d0
	btst	#\p,%d4
	beq.s	1f
	moveq	#-1,%d0
1:	move.l	%d0,\p*4(%a5)
	moveq	#0,%d0
	btst	#\p,%d5
	beq.s	1f
	moveq	#-1,%d0
1:	move.l	%d0,16+\p*4(%a5)
	.endm

	.macro	TPLANE	p, withb
	move.l	%d2,%d4
	and.l	\p*4(%a5),%d4
	move.l	%d3,%d5
	and.l	16+\p*4(%a5),%d5
	or.l	%d5,%d4
	move.w	\p*2(%a0),%d5
	and.w	%d0,%d5
	swap	%d4
	or.w	%d4,%d5
	move.w	%d5,\p*2(%a0)
	.if \withb
	swap	%d4
	move.w	8+\p*2(%a0),%d5
	and.w	%d1,%d5
	or.w	%d4,%d5
	move.w	%d5,8+\p*2(%a0)
	.endif
	.endm

	.macro	TROWS	withb
tr\withb:
	moveq	#0,%d2
	move.b	(%a3)+,%d2
	lsl.l	%d6,%d2			| g = glyph row, positioned
	move.l	%d2,%d3
	not.l	%d3
	and.l	%d7,%d3			| background pixels inside the cell
	move.l	%d7,%d0
	not.l	%d0
	move.w	%d0,%d1			| ~mask low word
	swap	%d0			| ~mask high word
	TPLANE	0,\withb
	TPLANE	1,\withb
	TPLANE	2,\withb
	TPLANE	3,\withb
	lea	160(%a0),%a0
	cmp.l	%a4,%a3
	bne	tr\withb
	.endm

	.globl	draw_text_a
draw_text_a:
	movem.l	%d2-%d7/%a2-%a5,-(%sp)
	lea	-40(%sp),%sp
	move.l	%sp,%a5
	move.l	84(%sp),%a0		| buf
	move.l	92(%sp),%d1		| y
	mulu.w	#160,%d1
	add.l	%d1,%a0
	move.l	%a0,36(%a5)		| row base
	move.l	88(%sp),32(%a5)		| x
	move.l	96(%sp),%a1		| string
	move.l	108(%sp),%a2		| font
	move.l	100(%sp),%d4		| col
	move.l	104(%sp),%d5		| bgc
	TFLAG	0
	TFLAG	1
	TFLAG	2
	TFLAG	3
dt_char:
	moveq	#0,%d0
	move.b	(%a1)+,%d0
	beq	dt_done
	cmp.w	#32,%d0
	bcs.s	dt_sp
	cmp.w	#127,%d0
	bhi.s	dt_sp
	sub.w	#32,%d0
	bra.s	dt_ok
dt_sp:	moveq	#0,%d0
dt_ok:	lsl.w	#3,%d0
	lea	0(%a2,%d0.w),%a3
	lea	8(%a3),%a4
	move.l	32(%a5),%d1
	moveq	#15,%d2
	and.l	%d1,%d2			| sh
	moveq	#24,%d6
	sub.l	%d2,%d6			| d6 = 24 - sh
	lsr.l	#4,%d1
	lsl.l	#3,%d1
	move.l	36(%a5),%a0
	add.l	%d1,%a0
	move.l	#0xff,%d7
	lsl.l	%d6,%d7			| cell mask
	cmp.w	#16,%d6
	bge	dt_one
	TROWS	1
	bra	dt_next
dt_one:
	TROWS	0
dt_next:
	addq.l	#8,32(%a5)
	bra	dt_char
dt_done:
	lea	40(%sp),%sp
	movem.l	(%sp)+,%d2-%d7/%a2-%a5
	rts

| ---------------------------------------------------------------------
| void add_dirty(int b, int x, int y, int w, int h)
| Appends to dirty[b]; whole-screen rectangle when the list is full.
	.globl	add_dirty
add_dirty:
	move.l	4(%sp),%d1
	move.l	%d1,%d0
	lsl.l	#2,%d0
	lea	ndirty,%a0
	add.l	%d0,%a0			| &ndirty[b]
	mulu.w	#MAX_DIRTY*8,%d1
	lea	dirty,%a1
	add.l	%d1,%a1			| &dirty[b][0]
	move.l	(%a0),%d0		| n
	move.w	dirty_peak,%d1
	cmp.w	%d1,%d0
	ble.s	1f
	move.w	%d0,dirty_peak
1:	cmp.l	#MAX_DIRTY,%d0
	blt.s	2f
	addq.l	#1,dirty_overflow
	move.l	#1,(%a0)
	clr.l	(%a1)+
	move.w	#320,(%a1)+
	move.w	#200,(%a1)
	rts
2:	addq.l	#1,(%a0)
	lsl.l	#3,%d0
	add.l	%d0,%a1
	move.w	10(%sp),(%a1)+
	move.w	14(%sp),(%a1)+
	move.w	18(%sp),(%a1)+
	move.w	22(%sp),(%a1)
	rts

| ---------------------------------------------------------------------
| int box_hit(int x, int y)
| Index of the brick under any corner of the 4x4 ball at x,y, else -1.
| Works out the (at most 2) columns and rows the ball touches once, and
| looks each distinct cell up a single time.
	.macro	CCOL	r
	tst.l	\r
	bmi.s	1f
	cmp.l	#BCOLS*16,\r
	bcc.s	1f
	lsr.l	#4,\r
	bra.s	2f
1:	moveq	#-1,\r
2:
	.endm

	.macro	CROW	r
	tst.l	\r
	bmi.s	1f
	cmp.l	#BROWS*8,\r
	bcc.s	1f
	lsr.l	#3,\r
	move.l	\r,%d3
	lsl.l	#2,%d3
	add.l	%d3,\r			| 5r
	add.l	%d3,%d3
	add.l	%d3,\r			| 13r = BCOLS*r
	bra.s	2f
1:	moveq	#-1,\r
2:
	.endm

	.macro	TRY	row, col
	move.l	\row,%d3
	add.l	\col,%d3
	tst.b	0(%a0,%d3.l)
	beq.s	1f
	move.l	%d3,%d0
	bra	bh_out
1:
	.endm

	.globl	box_hit
box_hit:
	movem.l	%d2-%d5,-(%sp)
	moveq	#-1,%d0
	move.l	20(%sp),%d1
	sub.l	#PF_X,%d1		| dx
	move.l	24(%sp),%d2
	sub.l	#BR_Y,%d2		| dy
	move.l	%d1,%d4
	addq.l	#BALL_S-1,%d4		| dx + 3
	bmi	bh_out
	cmp.l	#BCOLS*16,%d1
	bge	bh_out
	move.l	%d2,%d5
	addq.l	#BALL_S-1,%d5		| dy + 3
	bmi	bh_out
	cmp.l	#BROWS*8,%d2
	bge	bh_out
	lea	cell_type,%a0
	CCOL	%d1			| left column or -1
	CCOL	%d4			| right column or -1
	CROW	%d2			| top row offset or -1
	CROW	%d5			| bottom row offset or -1
	tst.l	%d2
	bmi.s	bh_bot
	tst.l	%d1
	bmi.s	bh_tr
	TRY	%d2,%d1
bh_tr:	tst.l	%d4
	bmi.s	bh_bot
	cmp.l	%d1,%d4
	beq.s	bh_bot
	TRY	%d2,%d4
bh_bot:	tst.l	%d5
	bmi.s	bh_out
	cmp.l	%d2,%d5
	beq.s	bh_out
	tst.l	%d1
	bmi.s	bh_br
	TRY	%d5,%d1
bh_br:	tst.l	%d4
	bmi.s	bh_out
	cmp.l	%d1,%d4
	beq.s	bh_out
	TRY	%d5,%d4
bh_out:
	movem.l	(%sp)+,%d2-%d5
	rts

| ---------------------------------------------------------------------
| int enemy_scan(const Enemy *e, int x, int y)
| Index of the first active enemy overlapping the ball at x,y, else -1.
	.globl	enemy_scan
enemy_scan:
	movem.l	%d2-%d5,-(%sp)
	move.l	20(%sp),%a0
	move.l	24(%sp),%d1
	move.l	28(%sp),%d2
	moveq	#0,%d0
	moveq	#MAX_ENEMIES-1,%d3
es_loop:
	tst.b	ENEMY_ACTIVE(%a0)
	beq.s	es_next
	move.l	(%a0),%d4
	asr.l	#8,%d4
	move.l	%d1,%d5
	sub.l	%d4,%d5
	addq.l	#BALL_S-1,%d5
	cmp.l	#BALL_S+ENEMY_SIZE-1,%d5
	bcc.s	es_next
	move.l	4(%a0),%d4
	asr.l	#8,%d4
	move.l	%d2,%d5
	sub.l	%d4,%d5
	addq.l	#BALL_S-1,%d5
	cmp.l	#BALL_S+ENEMY_SIZE-1,%d5
	bcs.s	es_hit
es_next:
	lea	ENEMY_STRIDE(%a0),%a0
	addq.l	#1,%d0
	dbra	%d3,es_loop
	moveq	#-1,%d0
es_hit:
	movem.l	(%sp)+,%d2-%d5
	rts

| int ball_box(int x, int y, int bx, int by, int w, int h)
| 1 if the 4x4 ball at x,y overlaps the box bx,by,w,h.
	.globl	ball_box
ball_box:
	move.l	4(%sp),%d0
	sub.l	12(%sp),%d0
	addq.l	#BALL_S-1,%d0
	move.l	20(%sp),%d1
	addq.l	#BALL_S-1,%d1
	cmp.l	%d1,%d0
	bcc.s	bb_no
	move.l	8(%sp),%d0
	sub.l	16(%sp),%d0
	addq.l	#BALL_S-1,%d0
	move.l	24(%sp),%d1
	addq.l	#BALL_S-1,%d1
	cmp.l	%d1,%d0
	bcc.s	bb_no
	moveq	#1,%d0
	rts
bb_no:
	moveq	#0,%d0
	rts

| ---------------------------------------------------------------------
| int move_ball(Ball *b)       returns 1 if the ball fell out
| Ball: s32 x,y,vx,vy (8.8); s16 speed; u8 active.
| Steps one pixel at a time along the faster axis so thin bricks are never
| skipped; x then y, bouncing off walls, bricks, boss, enemies and paddle.
	.macro	CALL1	fn
	move.l	%a2,-(%sp)
	jsr	\fn
	addq.l	#4,%sp
	.endm

| bounce test against the boss box and the enemies; jumps to \target on a hit
	.macro	BALLHIT	target
	tst.l	boss_active
	beq.s	1f
	CALL1	boss_ball_hit
	tst.l	%d0
	bne	\target
1:	tst.b	enemies+ENEMY_ACTIVE
	bne.s	3f
	tst.b	enemies+ENEMY_STRIDE+ENEMY_ACTIVE
	bne.s	3f
	tst.b	enemies+2*ENEMY_STRIDE+ENEMY_ACTIVE
	beq.s	2f
3:	move.l	4(%a2),%d0
	asr.l	#8,%d0
	move.l	%d0,-(%sp)
	move.l	(%a2),%d0
	asr.l	#8,%d0
	move.l	%d0,-(%sp)
	pea	enemies
	jsr	enemy_scan
	lea	12(%sp),%sp
	tst.l	%d0
	blt.s	2f
	move.l	%d0,-(%sp)
	jsr	enemy_hurt
	addq.l	#4,%sp
	bra	\target
2:
	.endm

	.globl	move_ball
move_ball:
	movem.l	%d2-%d6/%a2,-(%sp)
	move.l	28(%sp),%a2
	move.l	8(%a2),%d0
	bpl.s	1f
	neg.l	%d0
1:	move.l	12(%a2),%d1
	bpl.s	2f
	neg.l	%d1
2:	cmp.l	%d1,%d0
	bge.s	3f
	move.l	%d1,%d0
3:	asr.l	#8,%d0
	addq.l	#1,%d0
	move.l	%d0,%d4			| d4 = steps
	move.l	8(%a2),%d2
	divs.w	%d4,%d2
	ext.l	%d2			| d2 = sx
	move.l	12(%a2),%d3
	divs.w	%d4,%d3
	ext.l	%d3			| d3 = sy
	moveq	#0,%d6
	tst.l	fireball_timer
	ble.s	4f
	moveq	#1,%d6			| d6 = fireball active
4:	moveq	#0,%d5			| d5 = step counter
mb_step:
	cmp.l	%d4,%d5
	bge	mb_ret0
	| ---- horizontal ----
	add.l	%d2,(%a2)
	BALLHIT	mb_xback
	move.l	(%a2),%d0
	asr.l	#8,%d0			| x
	cmp.l	#PF_X,%d0
	blt.s	mb_xwall
	addq.l	#BALL_S,%d0
	cmp.l	#PF_X2,%d0
	bgt.s	mb_xwall
	| Boss levels have no bricks.
	tst.l	boss_active
	bne	mb_ystep
	move.l	4(%a2),%d1
	asr.l	#8,%d1
	cmp.l	#BR_Y+BROWS*8,%d1
	bge	mb_ystep
	cmp.l	#BR_Y-BALL_S+1,%d1
	blt	mb_ystep
	move.l	%d1,-(%sp)
	move.l	(%a2),%d0
	asr.l	#8,%d0
	move.l	%d0,-(%sp)
	jsr	box_hit
	addq.l	#8,%sp
	tst.l	%d0
	blt	mb_ystep
	clr.l	frames_since_hit
	tst.l	%d6
	beq.s	mb_xflip
	lea	cell_type,%a0
	cmp.b	#B_METAL,0(%a0,%d0.l)
	beq.s	mb_xflip
	bra.s	mb_xdmg
mb_xflip:
	sub.l	%d2,(%a2)
	neg.l	%d2
	neg.l	8(%a2)
mb_xdmg:
	move.l	%d6,-(%sp)
	move.l	%a2,-(%sp)
	move.l	%d0,-(%sp)
	jsr	damage_brick
	lea	12(%sp),%sp
	bra	mb_ystep
mb_xwall:
	sub.l	%d2,(%a2)
	neg.l	%d2
	neg.l	8(%a2)
	clr.l	frames_since_hit
	pea	SFX_BOUNCE
	jsr	sfx_play
	addq.l	#4,%sp
	bra	mb_ystep
mb_xback:
	sub.l	%d2,(%a2)
	neg.l	%d2
	neg.l	8(%a2)
	clr.l	frames_since_hit
	| ---- vertical ----
mb_ystep:
	add.l	%d3,4(%a2)
	BALLHIT	mb_yback
	move.l	4(%a2),%d1
	asr.l	#8,%d1			| y
	cmp.l	#PF_Y,%d1
	blt	mb_ywall
	tst.l	boss_active
	bne	mb_ypad
	cmp.l	#BR_Y+BROWS*8,%d1
	bge	mb_ypad
	cmp.l	#BR_Y-BALL_S+1,%d1
	blt	mb_ypad
	move.l	%d1,-(%sp)
	move.l	(%a2),%d0
	asr.l	#8,%d0
	move.l	%d0,-(%sp)
	jsr	box_hit
	addq.l	#8,%sp
	tst.l	%d0
	blt.s	mb_ypad
	clr.l	frames_since_hit
	tst.l	%d6
	beq.s	mb_yflip
	lea	cell_type,%a0
	cmp.b	#B_METAL,0(%a0,%d0.l)
	beq.s	mb_yflip
	bra.s	mb_ydmg
mb_yflip:
	sub.l	%d3,4(%a2)
	neg.l	%d3
	neg.l	12(%a2)
mb_ydmg:
	move.l	%d6,-(%sp)
	move.l	%a2,-(%sp)
	move.l	%d0,-(%sp)
	jsr	damage_brick
	lea	12(%sp),%sp
	bra	mb_next
mb_ypad:
	move.l	4(%a2),%d1
	asr.l	#8,%d1			| y
	tst.l	%d3
	ble	mb_ybar
	move.l	%d1,%d0
	addq.l	#BALL_S,%d0
	cmp.l	#PAD_Y,%d0
	blt	mb_ybar
	cmp.l	#PAD_Y+3,%d0
	bgt	mb_ybar
	move.l	(%a2),%d0
	asr.l	#8,%d0			| x
	addq.l	#BALL_S,%d0
	cmp.l	paddle_x,%d0
	ble	mb_ybar
	move.l	(%a2),%d0
	asr.l	#8,%d0
	move.l	paddle_x,%d1
	add.l	paddle_w,%d1
	cmp.l	%d1,%d0
	bge	mb_ybar
	move.l	#(PAD_Y-BALL_S)<<8,4(%a2)
	CALL1	paddle_bounce
	move.l	8(%a2),%d2
	divs.w	%d4,%d2
	ext.l	%d2
	move.l	12(%a2),%d3
	divs.w	%d4,%d3
	ext.l	%d3
	bra	mb_next
mb_ybar:
	tst.l	%d3
	ble.s	mb_yfall
	tst.l	has_barrier
	beq.s	mb_yfall
	move.l	4(%a2),%d1
	asr.l	#8,%d1
	addq.l	#BALL_S,%d1
	cmp.l	#BARRIER_Y,%d1
	blt.s	mb_yfall
	move.l	#(BARRIER_Y-BALL_S)<<8,4(%a2)
	neg.l	%d3
	neg.l	12(%a2)
	clr.l	has_barrier
	jsr	draw_barrier
	pea	SFX_BARRIERSAVE
	jsr	sfx_play
	addq.l	#4,%sp
	bra.s	mb_next
mb_yfall:
	move.l	4(%a2),%d1
	asr.l	#8,%d1
	cmp.l	#200-BALL_S,%d1
	blt.s	mb_next
	moveq	#1,%d0
	bra.s	mb_out
mb_ywall:
	sub.l	%d3,4(%a2)
	neg.l	%d3
	neg.l	12(%a2)
	clr.l	frames_since_hit
	pea	SFX_BOUNCE
	jsr	sfx_play
	addq.l	#4,%sp
	bra.s	mb_next
mb_yback:
	sub.l	%d3,4(%a2)
	neg.l	%d3
	neg.l	12(%a2)
	clr.l	frames_since_hit
mb_next:
	addq.l	#1,%d5
	bra	mb_step
mb_ret0:
	moveq	#0,%d0
mb_out:
	movem.l	(%sp)+,%d2-%d6/%a2
	rts
