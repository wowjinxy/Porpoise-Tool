.include "macros.inc"

.text
.global main
.fn main, global
/* 80002000 00000000  94 21 FF F0 */	stwu r1, -16(r1)
/* 80002004 00000004  3C 80 12 34 */	lis r4, 0x1234
/* 80002008 00000008  60 84 56 78 */	ori r4, r4, 0x5678
/* 8000200C 0000000C  90 82 00 00 */	stw r4, 0(r2)
/* 80002010 00000010  3C A0 89 AB */	lis r5, 0x89AB
/* 80002014 00000014  60 A5 CD EF */	ori r5, r5, 0xCDEF
/* 80002018 00000018  90 AD 00 00 */	stw r5, 0(r13)
/* 8000201C 0000001C  38 60 00 00 */	li r3, 0
/* 80002020 00000020  E0 23 80 00 */	psq_l f1, 0(r3), 1, qr0
/* 80002024 00000024  4E 80 00 20 */	blr
.endfn main

.global startup_first
.fn startup_first, global
/* 80002100 00000100  3C 60 80 00 */	lis r3, 0x8000
/* 80002104 00000104  38 80 00 11 */	li r4, 0x11
/* 80002108 00000108  90 83 10 04 */	stw r4, 0x1004(r3)
/* 8000210C 0000010C  4E 80 00 20 */	blr
.endfn startup_first

.global startup_second
.fn startup_second, global
/* 80002120 00000120  3C 60 80 00 */	lis r3, 0x8000
/* 80002124 00000124  80 83 10 04 */	lwz r4, 0x1004(r3)
/* 80002128 00000128  38 84 00 22 */	addi r4, r4, 0x22
/* 8000212C 0000012C  90 83 10 08 */	stw r4, 0x1008(r3)
/* 80002130 00000130  4E 80 00 20 */	blr
.endfn startup_second
