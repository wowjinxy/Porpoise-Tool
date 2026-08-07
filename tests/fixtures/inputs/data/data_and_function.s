.include "macros.inc"

.section .rodata
.global sample_data
sample_data:
/* 80300000 00000000  12 34 56 78 */ .4byte 0x12345678
/* 80300004 00000004  41 42 43 00 */ .byte 0x41, 0x42, 0x43, 0x00

.text
.global data_user
.fn data_user, global
.Lreturn:
/* 80006000 00000000  4E 80 00 20 */ blr
.endfn data_user
