| 68000 line drawer for the boss wireframes: ORs colour bits into the four
| bitplanes, no clipping (the caller keeps the line inside the playfield).
| void line_asm(u8 *buf, int x0, int y0, int x1, int y1, int colmask)

	.macro	PUT
	btst	#0,%d5
	beq.s	.Lp1\@
	or.w	%d0,(%a0)
.Lp1\@:	btst	#1,%d5
	beq.s	.Lp2\@
	or.w	%d0,2(%a0)
.Lp2\@:	btst	#2,%d5
	beq.s	.Lp3\@
	or.w	%d0,4(%a0)
.Lp3\@:	btst	#3,%d5
	beq.s	.Lp4\@
	or.w	%d0,6(%a0)
.Lp4\@:
	.endm

	.text
	.globl	line_asm
line_asm:
	movem.l	%d2-%d7/%a2,-(%sp)	| 28 bytes of saved registers
	move.l	32(%sp),%a0		| buf
	move.l	36(%sp),%d6		| x0
	move.l	40(%sp),%d7		| y0
	move.l	44(%sp),%d1		| x1
	move.l	48(%sp),%d2		| y1
	move.l	52(%sp),%d5		| colour mask
	sub.l	%d6,%d1			| dx
	sub.l	%d7,%d2			| dy
	mulu.w	#160,%d7
	add.l	%d7,%a0			| + y0 * 160
	move.l	%d6,%d3
	lsr.l	#4,%d3
	lsl.l	#3,%d3
	add.l	%d3,%a0			| + (x0 >> 4) * 8
	moveq	#15,%d3
	and.l	%d6,%d3
	move.w	#0x8000,%d0
	lsr.w	%d3,%d0			| pixel mask
	move.l	#160,%a1		| row step
	tst.l	%d2
	bpl.s	1f
	neg.l	%d2
	move.l	#-160,%a1
1:	moveq	#1,%d4			| +1 = draw to the right
	tst.l	%d1
	bpl.s	2f
	neg.l	%d1
	moveq	#-1,%d4
2:	cmp.l	%d2,%d1
	blt.s	ymajor

	move.l	%d1,%d3
	lsr.l	#1,%d3			| err = dx / 2
	move.l	%d1,%d7
xloop:
	PUT
	sub.l	%d2,%d3
	bpl.s	3f
	add.l	%d1,%d3
	add.l	%a1,%a0
3:	tst.l	%d4
	bmi.s	4f
	lsr.w	#1,%d0
	bne.s	6f
	move.w	#0x8000,%d0
	addq.l	#8,%a0
	bra.s	6f
4:	lsl.w	#1,%d0
	bne.s	6f
	move.w	#1,%d0
	subq.l	#8,%a0
6:	dbra	%d7,xloop
	bra.s	done

ymajor:
	move.l	%d2,%d3
	lsr.l	#1,%d3			| err = dy / 2
	move.l	%d2,%d7
yloop:
	PUT
	sub.l	%d1,%d3
	bpl.s	7f
	add.l	%d2,%d3
	tst.l	%d4
	bmi.s	5f
	lsr.w	#1,%d0
	bne.s	7f
	move.w	#0x8000,%d0
	addq.l	#8,%a0
	bra.s	7f
5:	lsl.w	#1,%d0
	bne.s	7f
	move.w	#1,%d0
	subq.l	#8,%a0
7:	add.l	%a1,%a0
	dbra	%d7,yloop
done:
	movem.l	(%sp)+,%d2-%d7/%a2
	rts

| void restore_asm(u8 *dst, const u8 *src, int groups, int rows)
| copies whole 16-pixel groups (8 bytes) per row, dst/src already point at the
| first group of the first row; rows are 160 bytes apart.
	.globl	restore_asm
restore_asm:
	movem.l	%d2-%d3/%a2-%a3,-(%sp)
	move.l	20(%sp),%a0		| dst
	move.l	24(%sp),%a1		| src
	move.l	28(%sp),%d1		| groups
	move.l	32(%sp),%d2		| rows
	subq.l	#1,%d2
	bmi.s	9f
	move.l	%d1,%d3
	lsl.l	#3,%d3
	move.l	#160,%d0
	sub.l	%d3,%d0			| bytes to advance to the next row
	subq.l	#1,%d1
	bmi.s	9f
1:	move.l	%d1,%d3
2:	move.l	(%a1)+,(%a0)+
	move.l	(%a1)+,(%a0)+
	dbra	%d3,2b
	add.l	%d0,%a0
	add.l	%d0,%a1
	dbra	%d2,1b
9:	movem.l	(%sp)+,%d2-%d3/%a2-%a3
	rts

| void line_plane(u8 *buf, int x0, int y0, int x1, int y1, int plane)
| Same as line_asm but ORs into a single bitplane, which is about twice as
| fast - the boss levels have spare palette entries, so one plane is enough.
	.globl	line_plane
line_plane:
	movem.l	%d2-%d7,-(%sp)		| 24 bytes
	move.l	28(%sp),%a0		| buf
	move.l	32(%sp),%d6		| x0
	move.l	36(%sp),%d7		| y0
	move.l	40(%sp),%d1		| x1
	move.l	44(%sp),%d2		| y1
	move.l	48(%sp),%d5		| plane 0-3
	sub.l	%d6,%d1
	sub.l	%d7,%d2
	mulu.w	#160,%d7
	add.l	%d7,%a0
	move.l	%d6,%d3
	lsr.l	#4,%d3
	lsl.l	#3,%d3
	add.l	%d3,%a0
	add.l	%d5,%d5
	add.l	%d5,%a0			| + plane * 2
	moveq	#15,%d3
	and.l	%d6,%d3
	move.w	#0x8000,%d0
	lsr.w	%d3,%d0
	move.l	#160,%a1
	tst.l	%d2
	bpl.s	1f
	neg.l	%d2
	move.l	#-160,%a1
1:	moveq	#1,%d4
	tst.l	%d1
	bpl.s	2f
	neg.l	%d1
	moveq	#-1,%d4
2:	cmp.l	%d2,%d1
	blt.s	pymajor
	move.l	%d1,%d3
	lsr.l	#1,%d3
	move.l	%d1,%d7
pxloop:
	or.w	%d0,(%a0)
	sub.l	%d2,%d3
	bpl.s	3f
	add.l	%d1,%d3
	add.l	%a1,%a0
3:	tst.l	%d4
	bmi.s	4f
	lsr.w	#1,%d0
	bne.s	6f
	move.w	#0x8000,%d0
	addq.l	#8,%a0
	bra.s	6f
4:	lsl.w	#1,%d0
	bne.s	6f
	move.w	#1,%d0
	subq.l	#8,%a0
6:	dbra	%d7,pxloop
	bra.s	pdone
pymajor:
	move.l	%d2,%d3
	lsr.l	#1,%d3
	move.l	%d2,%d7
pyloop:
	or.w	%d0,(%a0)
	sub.l	%d1,%d3
	bpl.s	7f
	add.l	%d2,%d3
	tst.l	%d4
	bmi.s	5f
	lsr.w	#1,%d0
	bne.s	7f
	move.w	#0x8000,%d0
	addq.l	#8,%a0
	bra.s	7f
5:	lsl.w	#1,%d0
	bne.s	7f
	move.w	#1,%d0
	subq.l	#8,%a0
7:	add.l	%a1,%a0
	dbra	%d7,pyloop
pdone:
	movem.l	(%sp)+,%d2-%d7
	rts
