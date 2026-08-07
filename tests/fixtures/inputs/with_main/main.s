.include "macros.inc"

.text
.global main
.fn main, global
/* 80002000 00000000  38 60 00 00 */	li r3, 0
/* 80002004 00000004  4E 80 00 20 */	blr
.endfn main
