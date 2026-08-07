.include "macros.inc"

.text
.global root_function
.fn root_function, global
/* 80003000 00000000  48 00 00 11 */	bl nested_helper
/* 80003004 00000004  4E 80 00 20 */	blr
.endfn root_function
