| Thin TOS call wrappers (C args are 32-bit on the stack)
	.text
	.globl	sup_on, get_rez, set_screen, kbdvbase, malloc_st
	.globl	con_stat, con_in, f_create, f_write, f_close, phys_base, cconws

sup_on:			| long sup_on(void)  -> old ssp
	clr.l	-(%sp)
	move.w	#0x20,-(%sp)		| Super
	trap	#1
	addq.l	#6,%sp
	rts
get_rez:		| XBIOS 4
	move.w	#4,-(%sp)
	trap	#14
	addq.l	#2,%sp
	ext.l	%d0
	rts
phys_base:		| XBIOS 2
	move.w	#2,-(%sp)
	trap	#14
	addq.l	#2,%sp
	rts
set_screen:		| set_screen(log, phys, rez)  XBIOS 5
	move.l	12(%sp),%d0
	move.w	%d0,-(%sp)
	move.l	10(%sp),-(%sp)
	move.l	10(%sp),-(%sp)
	move.w	#5,-(%sp)
	trap	#14
	lea	12(%sp),%sp
	rts
kbdvbase:		| XBIOS 34
	move.w	#34,-(%sp)
	trap	#14
	addq.l	#2,%sp
	rts
malloc_st:		| GEMDOS 0x48 Malloc(size)
	move.l	4(%sp),-(%sp)
	move.w	#0x48,-(%sp)
	trap	#1
	addq.l	#6,%sp
	rts
con_stat:		| BIOS Bconstat(2)
	move.w	#2,-(%sp)
	move.w	#1,-(%sp)
	trap	#13
	addq.l	#4,%sp
	rts
con_in:			| BIOS Bconin(2)
	move.w	#2,-(%sp)
	move.w	#2,-(%sp)
	trap	#13
	addq.l	#4,%sp
	rts
cconws:			| GEMDOS 9
	move.l	4(%sp),-(%sp)
	move.w	#9,-(%sp)
	trap	#1
	addq.l	#6,%sp
	rts
f_create:		| f_create(name) GEMDOS 0x3c
	clr.w	-(%sp)
	move.l	6(%sp),-(%sp)
	move.w	#0x3c,-(%sp)
	trap	#1
	addq.l	#8,%sp
	rts
f_write:		| f_write(handle, len, buf) GEMDOS 0x40
	move.l	12(%sp),-(%sp)
	move.l	12(%sp),-(%sp)
	move.l	12(%sp),%d0
	move.w	%d0,-(%sp)
	move.w	#0x40,-(%sp)
	trap	#1
	lea	12(%sp),%sp
	rts
f_close:		| f_close(handle) GEMDOS 0x3e
	move.l	4(%sp),%d0
	move.w	%d0,-(%sp)
	move.w	#0x3e,-(%sp)
	trap	#1
	addq.l	#4,%sp
	rts

	.globl	linea_fonts, sup_off
linea_fonts:		| returns pointer to Linea system font header list
	movem.l	%d2/%a2,-(%sp)
	.word	0xa000
	move.l	%a1,%d0
	movem.l	(%sp)+,%d2/%a2
	rts
sup_off:		| sup_off(oldssp)
	move.l	4(%sp),%d0
	move.l	(%sp),%a0		| return address (stack switches!)
	move.l	%d0,-(%sp)
	move.w	#0x20,-(%sp)		| Super
	trap	#1
	addq.l	#6,%sp
	rts
	.globl	get_sr
get_sr:
	moveq	#0,%d0
	move.w	%sr,%d0
	rts

	.globl	ikbdws
ikbdws:			| ikbdws(count, ptr)  XBIOS 25 Ikbdws(count-1, ptr)
	move.l	8(%sp),-(%sp)
	move.l	8(%sp),%d0
	subq.w	#1,%d0
	move.w	%d0,-(%sp)
	move.w	#25,-(%sp)
	trap	#14
	addq.l	#8,%sp
	rts

	.globl	f_open, f_read
f_open:			| f_open(name, mode) GEMDOS 0x3d
	move.l	8(%sp),%d0
	move.w	%d0,-(%sp)
	move.l	6(%sp),-(%sp)
	move.w	#0x3d,-(%sp)
	trap	#1
	addq.l	#8,%sp
	rts
f_read:			| f_read(handle, len, buf) GEMDOS 0x3f
	move.l	12(%sp),-(%sp)
	move.l	12(%sp),-(%sp)
	move.l	12(%sp),%d0
	move.w	%d0,-(%sp)
	move.w	#0x3f,-(%sp)
	trap	#1
	lea	12(%sp),%sp
	rts

| ---- sound support (supervisor mode) ----
	.globl	irq_off, irq_restore
irq_off:		| long irq_off(void): old SR, interrupts masked
	moveq	#0,%d0
	move.w	%sr,%d0
	ori.w	#0x0700,%sr
	rts
irq_restore:		| irq_restore(sr)
	move.l	4(%sp),%d0
	move.w	%d0,%sr
	rts

	.globl	log_base, set_palette_xbios
log_base:		| XBIOS 3 Logbase
	move.w	#3,-(%sp)
	trap	#14
	addq.l	#2,%sp
	rts
set_palette_xbios:	| XBIOS 6 Setpalette(ptr)
	move.l	4(%sp),-(%sp)
	move.w	#6,-(%sp)
	trap	#14
	addq.l	#6,%sp
	rts

	.globl	dosound
dosound:		| XBIOS 32 Dosound(ptr)
	move.l	4(%sp),-(%sp)
	move.w	#32,-(%sp)
	trap	#14
	addq.l	#6,%sp
	rts
