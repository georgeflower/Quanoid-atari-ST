| Pre-shifted sprite blitter.
|
| The C blit() routine barrel-shifts every mask and plane word at draw time,
| which the micro-benchmark measured at 5000-13000 cycles per sprite (see
| MICRO.LOG) - most of it the shifts themselves, done fresh every frame even
| though a sprite only has 16 possible x alignments. Classic ST practice is
| to precompute all 16 shifted forms once (build_preshift() in C, at
| startup) and have the per-frame blit do plain AND/OR moves. That is what
| this routine does; no shift instruction appears in the hot path at all.
|
| void blit_pre(u8 *buf, int gx, int y, int h, const u16 *tab)
|   tab points to h rows of 10 words: mask_hi, mask_lo,
|   p0_hi, p0_lo, p1_hi, p1_lo, p2_hi, p2_lo, p3_hi, p3_lo
|   (already shifted for this sprite's x & 15, by build_preshift)

	.text
	.globl	blit_pre
blit_pre:
	movem.l	%d2-%d7/%a2,-(%sp)	| 28 bytes of saved registers
	move.l	32(%sp),%a0		| buf
	move.l	36(%sp),%d0		| gx
	move.l	40(%sp),%d1		| y
	move.l	44(%sp),%d7		| h
	move.l	48(%sp),%a1		| tab
	mulu.w	#160,%d1
	add.l	%d1,%a0
	lsl.l	#3,%d0
	add.l	%d0,%a0			| a0 = buf + y*160 + gx*8 (first group)
	lea	8(%a0),%a2		| a2 = second group (next 16 px)
	subq.l	#1,%d7
row:
	movem.w	(%a1)+,%d0-%d1		| d0=mask_hi d1=mask_lo
	not.w	%d0
	not.w	%d1
	movem.w	(%a1)+,%d2-%d3		| plane 0
	move.w	(%a0),%d4
	and.w	%d0,%d4
	or.w	%d2,%d4
	move.w	%d4,(%a0)
	move.w	(%a2),%d4
	and.w	%d1,%d4
	or.w	%d3,%d4
	move.w	%d4,(%a2)
	movem.w	(%a1)+,%d2-%d3		| plane 1
	move.w	2(%a0),%d4
	and.w	%d0,%d4
	or.w	%d2,%d4
	move.w	%d4,2(%a0)
	move.w	2(%a2),%d4
	and.w	%d1,%d4
	or.w	%d3,%d4
	move.w	%d4,2(%a2)
	movem.w	(%a1)+,%d2-%d3		| plane 2
	move.w	4(%a0),%d4
	and.w	%d0,%d4
	or.w	%d2,%d4
	move.w	%d4,4(%a0)
	move.w	4(%a2),%d4
	and.w	%d1,%d4
	or.w	%d3,%d4
	move.w	%d4,4(%a2)
	movem.w	(%a1)+,%d2-%d3		| plane 3
	move.w	6(%a0),%d4
	and.w	%d0,%d4
	or.w	%d2,%d4
	move.w	%d4,6(%a0)
	move.w	6(%a2),%d4
	and.w	%d1,%d4
	or.w	%d3,%d4
	move.w	%d4,6(%a2)
	add.w	#160,%a0
	add.w	#160,%a2
	dbra	%d7,row
	movem.l	(%sp)+,%d2-%d7/%a2
	rts
