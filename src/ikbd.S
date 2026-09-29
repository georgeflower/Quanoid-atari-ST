| Qarkanoid - keyboard/mouse/joystick and frame timing, done the way ST games
| do it: our own handler on the IKBD interrupt vector and a chained Timer C.
| Reference: TOS ROM listing (sndirq runs from Timer C), Atari IKBD protocol,
| and Patrice Mandin's IKBD routine used by the ESW kernel.

	.equ	ikbd_vec, 0x118		| MFP channel B bit 6 - IKBD/MIDI ACIA
	.equ	timerc_vec, 0x114
	.equ	ierb, 0xfffffa09
	.equ	imrb, 0xfffffa15
	.equ	isrb, 0xfffffa11
	.equ	kbd_ctrl, 0xfffffc00
	.equ	kbd_data, 0xfffffc02
	.equ	midi_ctrl, 0xfffffc04
	.equ	midi_data, 0xfffffc06

	.text
	.globl	ikbd_install, ikbd_remove, timer_install, timer_remove
	.globl	mouse_dx, mouse_btn, mouse_click, joy1, joy_click
	.globl	key_buf, key_head, key_tail, tick50, take_mouse_dx, take_click, take_key

| ---- install / remove -------------------------------------------------
ikbd_install:
	movem.l	%d0-%d1/%a0,-(%sp)
	move.w	%sr,-(%sp)
	move.w	#0x2700,%sr
	move.l	ikbd_vec.w,old_ikbd
	move.l	#ikbd_isr,ikbd_vec.w
	bset	#6,ierb.w
	bset	#6,imrb.w
	clr.w	pkt_left
	clr.w	pkt_kind
	move.w	(%sp)+,%sr
	movem.l	(%sp)+,%d0-%d1/%a0
	rts

ikbd_remove:
	move.w	%sr,-(%sp)
	move.w	#0x2700,%sr
	move.l	old_ikbd,ikbd_vec.w
	move.w	(%sp)+,%sr
	rts

timer_install:
	move.w	%sr,-(%sp)
	move.w	#0x2700,%sr
	clr.w	tc_count
	clr.l	tick50
	move.l	timerc_vec.w,old_timerc
	move.l	#timerc_isr,timerc_vec.w
	move.w	(%sp)+,%sr
	rts

timer_remove:
	move.w	%sr,-(%sp)
	move.w	#0x2700,%sr
	move.l	old_timerc,timerc_vec.w
	move.w	(%sp)+,%sr
	rts

| ---- Timer C: 200 Hz, every 4th tick is a game frame -------------------
timerc_isr:
	addq.w	#1,tc_count
	cmpi.w	#4,tc_count
	blt.s	1f
	clr.w	tc_count
	addq.l	#1,tick50
1:	move.l	old_timerc,-(%sp)	| chain to TOS's own Timer C handler
	rts

| ---- IKBD interrupt ---------------------------------------------------
| Packets: $F8-$FB mouse (2 data bytes), $FE/$FF joystick (1 byte),
| $F6 status (6), $F7 absolute mouse (5), $FC clock (6), $FD joysticks (2),
| anything else is a key make/break code.
ikbd_isr:
	movem.l	%d0-%d2/%a0-%a1,-(%sp)
1:	btst	#0,midi_ctrl.w		| MIDI shares this interrupt
	beq.s	2f
	move.b	midi_data.w,%d0		| discard MIDI bytes
2:	btst	#0,kbd_ctrl.w		| receive buffer full?
	beq.w	9f
	moveq	#0,%d0
	move.b	kbd_data.w,%d0
	tst.w	pkt_left
	bne.w	pkt_byte

	cmpi.b	#0xf6,%d0		| start of a packet?
	bcs.w	key_byte
	cmpi.b	#0xf8,%d0
	bcs.w	long_pkt		| $F6 status, $F7 absolute
	cmpi.b	#0xfc,%d0
	bcs.w	mouse_hdr		| $F8-$FB relative mouse
	cmpi.b	#0xfe,%d0
	bcs.w	long_pkt		| $FC clock, $FD joystick report
	| $FE / $FF joystick event
	move.w	#1,pkt_left
	moveq	#2,%d1
	cmpi.b	#0xff,%d0
	beq.s	3f
	moveq	#3,%d1			| joystick 0: ignored
3:	move.w	%d1,pkt_kind
	bra.w	1b

mouse_hdr:
	move.b	%d0,%d1
	andi.b	#3,%d1			| buttons: bit0 right, bit1 left
	move.b	mouse_btn,%d2
	not.b	%d2
	and.b	%d1,%d2			| newly pressed?
	beq.s	4f
	st	mouse_click
4:	move.b	%d1,mouse_btn
	move.w	#2,pkt_left
	move.w	#1,pkt_kind
	bra.w	1b

long_pkt:
	moveq	#6,%d1			| skip the rest of a long packet
	move.w	%d1,pkt_left
	clr.w	pkt_kind
	bra.w	1b

key_byte:
	tst.b	%d0
	beq.w	1b
	btst	#7,%d0			| ignore break codes
	bne.w	1b
	move.w	key_head,%d1
	move.w	%d1,%d2
	addq.w	#1,%d2
	andi.w	#15,%d2
	cmp.w	key_tail,%d2
	beq.w	1b			| buffer full, drop
	lea	key_buf,%a0
	move.b	%d0,0(%a0,%d1.w)
	move.w	%d2,key_head
	bra.w	1b

pkt_byte:
	subq.w	#1,pkt_left
	move.w	pkt_kind,%d1
	cmpi.w	#1,%d1
	beq.w	mouse_data
	cmpi.w	#2,%d1
	beq.w	joy_data
	bra.w	1b			| part of a packet we ignore

mouse_data:
	cmpi.w	#1,pkt_left
	bne.w	1b			| second byte is dy: ignored
	ext.w	%d0			| first byte is dx
	add.w	%d0,mouse_dx
	bra.w	1b

joy_data:
	move.b	joy1,%d1
	not.b	%d1
	and.b	%d0,%d1
	bpl.s	5f
	st	joy_click		| fire newly pressed
5:	move.b	%d0,joy1
	bra.w	1b

9:	bclr	#6,isrb.w		| clear in-service, MFP is in software-EOI mode
	movem.l	(%sp)+,%d0-%d2/%a0-%a1
	rte

| ---- accessors used by the game --------------------------------------
take_mouse_dx:			| long take_mouse_dx(void)
	move.w	%sr,-(%sp)
	ori.w	#0x0700,%sr
	move.w	mouse_dx,%d0
	clr.w	mouse_dx
	move.w	(%sp)+,%sr
	ext.l	%d0
	rts

take_click:			| long take_click(void): a button was pressed
	move.w	%sr,-(%sp)
	ori.w	#0x0700,%sr
	moveq	#0,%d0
	move.b	mouse_click,%d0
	or.b	joy_click,%d0
	clr.b	mouse_click
	clr.b	joy_click
	move.w	(%sp)+,%sr
	rts

take_key:			| long take_key(void): next scan code, or 0
	moveq	#0,%d0
	move.w	key_tail,%d1
	cmp.w	key_head,%d1
	beq.s	1f
	lea	key_buf,%a0
	move.b	0(%a0,%d1.w),%d0
	addq.w	#1,%d1
	andi.w	#15,%d1
	move.w	%d1,key_tail
1:	rts

	.bss
	.even
old_ikbd:	.ds.l	1
old_timerc:	.ds.l	1
tick50:		.ds.l	1
tc_count:	.ds.w	1
pkt_left:	.ds.w	1
pkt_kind:	.ds.w	1
key_head:	.ds.w	1
key_tail:	.ds.w	1
mouse_dx:	.ds.w	1
mouse_btn:	.ds.b	1
mouse_click:	.ds.b	1
joy1:		.ds.b	1
joy_click:	.ds.b	1
key_buf:	.ds.b	16
	.even

	.text
| ---- our own vertical blank counter --------------------------------
| Chained to TOS's handler so nothing of TOS's own VBL work is skipped.
| The page flip waits on this, so it always happens during the blank.
	.globl	vbl_install, vbl_remove2, vbl_count
vbl_install:
	move.w	%sr,-(%sp)
	move.w	#0x2700,%sr
	clr.l	vbl_count
	move.l	0x70.w,old_vbl
	move.l	#vbl_isr,0x70.w
	move.w	(%sp)+,%sr
	rts
vbl_remove2:
	move.w	%sr,-(%sp)
	move.w	#0x2700,%sr
	move.l	old_vbl,0x70.w
	move.w	(%sp)+,%sr
	rts
vbl_isr:
	addq.l	#1,vbl_count
	move.l	old_vbl,-(%sp)		| chain to TOS's VBL handler
	rts

	.bss
old_vbl:	.ds.l	1
vbl_count:	.ds.l	1
