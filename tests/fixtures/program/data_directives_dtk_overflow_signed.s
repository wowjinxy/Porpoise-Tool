.include "macros.inc"
.file "data_directives_dtk_overflow_signed.c"

# 0x80016200..0x80016208 | size: 0x8
.data

# .data:0x0 | 0x80016200 | size: 0x8
.obj overflow_signed, global
	.quad -9223372036854775809
.endobj overflow_signed
