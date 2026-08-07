.include "macros.inc"

.text
.global add_one
.fn add_one, global
/* 80007000 00000000  38 63 00 01 */ addi r3, r3, 1
/* 80007004 00000004  4E 80 00 20 */ blr
.endfn add_one

.global add_float
.fn add_float, global
/* 80007020 00000020  EC 21 10 2A */ fadds f1, f1, f2
/* 80007024 00000024  4E 80 00 20 */ blr
.endfn add_float

.global add_double
.fn add_double, global
/* 80007040 00000040  FC 21 10 2A */ fadd f1, f1, f2
/* 80007044 00000044  4E 80 00 20 */ blr
.endfn add_double
