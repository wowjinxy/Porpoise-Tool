.include "macros.inc"

.text
.global nested_helper
.fn nested_helper, global
/* 80003010 00000010  38 63 00 02 */	addi r3, r3, 2
/* 80003014 00000014  4E 80 00 20 */	blr
.endfn nested_helper
