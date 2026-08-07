.include "macros.inc"

.text
.global unterminated_function
.fn unterminated_function, global
/* 80004000 00000000  38 63 00 01 */	addi r3, r3, 1
