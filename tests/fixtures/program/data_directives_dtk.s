.include "macros.inc"
.file "data_directives_dtk.c"

.text
.fn data_directives_entry, global
/* 80008000 00000000  4E 80 00 20 */ blr
.endfn data_directives_entry

# 0x80016000..0x8001603F | size: 0x3F
.data

# .data:0x0 | 0x80016000 | size: 0x3F
.obj data_directives, global
	.8byte 18446744073709551615
	.8byte -9223372036854775808
	.quad 0x0123456789ABCDEF
	.int 0xFFFFFFFF, -2147483648
	.short 0xFFFF, -32768
	.word 0x11223344
	.long 0x55667788
	.ascii "A", "B\n"
	.asciz "C"
	.byte 0x7F
	.2byte 0x1234
	.4byte 0x9ABCDEF0
	.string "Z"
	.space 2, 0xEE
	.zero 2
	.skip 1, 0xDD
.endobj data_directives
