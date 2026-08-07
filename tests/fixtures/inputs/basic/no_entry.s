.include "macros.inc"

.text
.global add_one
.fn add_one, global
/* 80001000 00000000  38 63 00 01 */	addi r3, r3, 1
/* 80001004 00000004  4E 80 00 20 */	blr
.endfn add_one
