| OR-only pixel/box drawing for the boss area and the parallax star step.
| Same m68k C ABI as gfx2.S. Constants must match main.c (static-asserted).

	.equ	PF_X, 8
	.equ	PF_X2, 216
	.equ	PF_Y, 8
	.equ	NSTARS, 12

	.text

| void pset_or(u8 *buf, int x, int y, int col)   (clipped to the playfield)
	.globl	pset_or
pset_or:
	move.l	8(%sp),%d0
	sub.l	#PF_X,%d0
	cmp.l	#PF_X2-PF_X,%d0
	bcc.s	9f
	move.l	12(%sp),%d1
	sub.l	#PF_Y,%d1
	cmp.l	#200-PF_Y,%d1
	bcc.s	9f
	move.l	4(%sp),%a0
	add.l	#PF_Y,%d1
	mulu.w	#160,%d1
	add.l	%d1,%a0
	move.l	8(%sp),%d0
	move.l	%d0,%d1
	lsr.l	#4,%d1
	lsl.l	#3,%d1
	add.l	%d1,%a0
	and.w	#15,%d0
	move.w	#0x8000,%d1
	lsr.w	%d0,%d1
	move.l	16(%sp),%d0
	btst	#0,%d0
	beq.s	1f
	or.w	%d1,(%a0)
1:	btst	#1,%d0
	beq.s	2f
	or.w	%d1,2(%a0)
2:	btst	#2,%d0
	beq.s	3f
	or.w	%d1,4(%a0)
3:	btst	#3,%d0
	beq.s	9f
	or.w	%d1,6(%a0)
9:	rts

| void box_or(u8 *buf, int x, int y, int w, int col)
	.macro	ORP	m
	btst	#0,%d5
	beq.s	1f
	or.w	\m,(%a0)
1:	btst	#1,%d5
	beq.s	2f
	or.w	\m,2(%a0)
2:	btst	#2,%d5
	beq.s	3f
	or.w	\m,4(%a0)
3:	btst	#3,%d5
	beq.s	4f
	or.w	\m,6(%a0)
4:	addq.l	#8,%a0
	.endm

	.globl	box_or
box_or:
	movem.l	%d2-%d7,-(%sp)
	| args now at 28(sp)...
	move.l	32(%sp),%d0		| x
	move.l	36(%sp),%d1		| y
	move.l	40(%sp),%d2		| w
	cmp.l	#PF_X,%d0
	blt	9f
	cmp.l	#PF_Y,%d1
	blt	9f
	move.l	%d0,%d3
	add.l	%d2,%d3			| x+w
	cmp.l	#PF_X2,%d3
	bgt	9f
	move.l	%d1,%d4
	add.l	%d2,%d4			| y+w
	cmp.l	#200,%d4
	bgt	9f
	tst.l	%d2
	ble	9f
	move.l	44(%sp),%d5		| col
	subq.l	#1,%d3			| x2
	move.l	%d3,%d4
	lsr.l	#4,%d4			| ge
	move.l	%d0,%d6
	lsr.l	#4,%d6			| gx
	sub.l	%d6,%d4
	move.l	%d4,%d7			| ge-gx
	| row pointer
	move.l	28(%sp),%a1
	mulu.w	#160,%d1
	add.l	%d1,%a1
	lsl.l	#3,%d6
	add.l	%d6,%a1
	| masks
	and.w	#15,%d0
	move.w	#-1,%d6
	lsr.w	%d0,%d6			| mf  (d6)
	and.w	#15,%d3
	moveq	#15,%d0
	sub.w	%d3,%d0
	move.w	#-1,%d4
	lsl.w	%d0,%d4			| ml  (d4)
	tst.l	%d7
	bne.s	.Lb1
	and.w	%d4,%d6			| single group: combine
.Lb1:	move.l	%d2,%d3
	subq.l	#1,%d3			| row counter (w rows)
.Lb2:	move.l	%a1,%a0
	ORP	%d6
	tst.l	%d7
	beq.s	.Lb5
	move.l	%d7,%d0
	subq.l	#1,%d0
	beq.s	.Lb4
	move.w	#-1,%d2
.Lb3:	ORP	%d2
	subq.l	#1,%d0
	bne.s	.Lb3
.Lb4:	ORP	%d4
.Lb5:	lea	160(%a1),%a1
	dbra	%d3,.Lb2
9:	movem.l	(%sp)+,%d2-%d7
	rts

| ---------------------------------------------------------------------
| void stars_step(void)
| One frame of the star field: a star moves 1px every star_period[layer]
| frames. State lives in C globals (star_x/star_y/star_cnt/star_y0).
	.globl	stars_step
	.macro	DIRTY1			| mark 1x1 at d3.l=x, d4.l=y in both buffers
	move.l	#1,-(%sp)
	move.l	#1,-(%sp)
	move.l	%d4,-(%sp)
	move.l	%d3,-(%sp)
	clr.l	-(%sp)
	jsr	add_dirty
	move.l	#1,(%sp)
	jsr	add_dirty
	lea	20(%sp),%sp
	.endm

| a0 = plane pointer, d1 = mask for pixel x=d3,y=d4 ; trashes d0
sp_calc:
	move.l	bg,%a0
	move.l	%d4,%d0
	mulu.w	#160,%d0
	add.l	%d0,%a0
	move.l	%d3,%d0
	lsr.l	#4,%d0
	lsl.l	#3,%d0
	add.l	%d0,%a0
	move.l	%d3,%d0
	and.w	#15,%d0
	move.w	#0x8000,%d1
	lsr.w	%d0,%d1
	rts

| d0 <- colour (0-15) of bg pixel x=d3,y=d4
sp_get:
	bsr.s	sp_calc
	moveq	#0,%d0
	move.w	(%a0)+,%d2
	and.w	%d1,%d2
	beq.s	1f
	moveq	#1,%d0
1:	move.w	(%a0)+,%d2
	and.w	%d1,%d2
	beq.s	2f
	bset	#1,%d0
2:	move.w	(%a0)+,%d2
	and.w	%d1,%d2
	beq.s	3f
	bset	#2,%d0
3:	move.w	(%a0),%d2
	and.w	%d1,%d2
	beq.s	4f
	bset	#3,%d0
4:	rts

| write colour d5 at x=d3,y=d4
sp_put:
	bsr.s	sp_calc
	move.w	%d1,%d2
	not.w	%d2
	moveq	#3,%d0
1:	btst	#0,%d5
	beq.s	2f
	or.w	%d1,(%a0)+
	bra.s	3f
2:	and.w	%d2,(%a0)+
3:	lsr.w	#1,%d5
	dbra	%d0,1b
	rts

stars_step:
	movem.l	%d2-%d7/%a2-%a3,-(%sp)
	lea	star_x,%a2
	lea	star_y,%a3
	moveq	#0,%d7			| i
	moveq	#0,%d6			| layer l
1:	move.w	%d7,%d0
	add.w	%d0,%d0
	move.w	(%a3,%d0.w),%d4		| star_y[i]
	ext.l	%d4
	lea	star_cnt,%a0
	lea	star_period,%a1
	moveq	#0,%d3
	move.b	(%a2,%d7.w),%d3		| x
	tst.l	%d4
	bmi.s	2f
	move.b	(%a0,%d7.w),%d0
	addq.b	#1,%d0
	cmp.b	(%a1,%d6.w),%d0
	bcc.s	3f
	move.b	%d0,(%a0,%d7.w)
	bra	8f
3:	| fall through with cnt reset
2:	clr.b	(%a0,%d7.w)
	tst.l	%d4
	bmi.s	4f
	| old position
	bsr	sp_get
	lea	star_col,%a0
	moveq	#0,%d2
	move.b	(%a0,%d6.w),%d2
	cmp.b	%d2,%d0
	bne.s	5f
	moveq	#0,%d5
	bsr	sp_put
	DIRTY1
5:	addq.l	#1,%d4			| y = old+1
	bra.s	6f
4:	lea	star_y0,%a0
	move.w	%d7,%d0
	add.w	%d0,%d0
	move.w	(%a0,%d0.w),%d4
	ext.l	%d4
6:	cmp.l	#PF_Y+190,%d4
	blt.s	7f
	moveq	#PF_Y,%d4
7:	move.w	%d7,%d0
	add.w	%d0,%d0
	move.w	%d4,(%a3,%d0.w)
	bsr	sp_get
	tst.l	%d0
	bne.s	8f
	lea	star_col,%a0
	moveq	#0,%d5
	move.b	(%a0,%d6.w),%d5
	bsr	sp_put
	DIRTY1
8:	addq.w	#1,%d6
	cmp.w	#3,%d6
	bne.s	9f
	moveq	#0,%d6
9:	addq.w	#1,%d7
	cmp.w	#NSTARS,%d7
	blt	1b
	movem.l	(%sp)+,%d2-%d7/%a2-%a3
	rts
