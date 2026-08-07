.include "macros.inc"

.text
.global alpha_shared
.fn alpha_shared, global
/* 80003100 00000100  38 63 00 03 */	addi r3, r3, 3
/* 80003104 00000104  4E 80 00 20 */	blr
.endfn alpha_shared
