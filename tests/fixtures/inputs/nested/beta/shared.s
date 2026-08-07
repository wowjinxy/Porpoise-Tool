.include "macros.inc"

.text
.global beta_shared
.fn beta_shared, global
/* 80003200 00000200  38 63 00 04 */	addi r3, r3, 4
/* 80003204 00000204  4E 80 00 20 */	blr
.endfn beta_shared
