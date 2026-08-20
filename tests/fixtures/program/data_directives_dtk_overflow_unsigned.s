.include "macros.inc"
.file "data_directives_dtk_overflow_unsigned.c"

# 0x80016100..0x80016108 | size: 0x8
.data

# .data:0x0 | 0x80016100 | size: 0x8
.obj overflow_unsigned, global
	.8byte 18446744073709551616
.endobj overflow_unsigned
