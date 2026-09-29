| TOS startup: shrink TPA, own stack, call main, Pterm
	.text
	.globl	_start
_start:
	move.l	4(%sp),%a0		| basepage
	move.l	#0x100,%d0		| basepage size
	add.l	12(%a0),%d0		| + text
	add.l	20(%a0),%d0		| + data
	add.l	28(%a0),%d0		| + bss
	add.l	#16384,%d0		| + stack
	move.l	%a0,%d1
	add.l	%d0,%d1
	and.b	#0xfe,%d1
	move.l	%d1,%sp
	move.l	%d0,-(%sp)		| Mshrink(basepage, size)
	move.l	%a0,-(%sp)
	clr.w	-(%sp)
	move.w	#0x4a,-(%sp)
	trap	#1
	lea	12(%sp),%sp
	jsr	main
	move.w	%d0,-(%sp)
	move.w	#0x4c,-(%sp)		| Pterm(code)
	trap	#1

| NatFeats detection (call once in supervisor mode). Real hardware and
| Hatari without --natfeats raise an illegal-instruction exception on
| the NF opcodes, so trap that instead of crashing.
	.globl	nf_init
nf_init:
	movem.l	%d1-%d2/%a0-%a2,-(%sp)
	move.l	0x10.w,-(%sp)
	move.l	%sp,nf_savesp
	move.l	#nf_ill,0x10.w
	pea	nf_name
	clr.l	-(%sp)
	.word	0x7300			| NF_ID
	addq.l	#8,%sp
	move.l	%d0,nf_id
nf_back:
	move.l	nf_savesp,%sp
	move.l	(%sp)+,0x10.w
	movem.l	(%sp)+,%d1-%d2/%a0-%a2
	rts
nf_ill:
	clr.l	nf_id
	bra.s	nf_back

| nf_print(const char *s): no-op unless NatFeats was detected
	.globl	nf_print
nf_print:
	move.l	nf_id,%d0
	beq.s	1f
	move.l	4(%sp),-(%sp)		| arg: string
	move.l	%d0,-(%sp)		| feature id
	clr.l	-(%sp)			| fake return address
	.word	0x7301			| NF_CALL
	lea	12(%sp),%sp
1:	rts
nf_name: .asciz	"NF_STDERR"
	.even

	.bss
nf_id:		.ds.l	1
nf_savesp:	.ds.l	1
