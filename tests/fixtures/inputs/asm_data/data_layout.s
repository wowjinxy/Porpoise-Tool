.include "macros.inc"
.file "data_layout.c"

.text
.fn entry_fn, global
.L_after:
/* 80008000 00000000  4E 80 00 20 */ blr
.endfn entry_fn

# 0x80010000..0x8001003C | size: 0x3C
.data
.balign 8

# .data:0x0 | 0x80010000 | size: 0x18
.obj "scalar_blob", global
	.byte 0x12, 0x34 # comments are outside quoted strings
	.2byte 0x5678
	.4byte 0x9ABCDEF0
	.float 1.0
	.double -2.0
	.string "Hi\n"
.endobj "scalar_blob"

# .data:0x18 | 0x80010018 | size: 0xC
.obj pointer_blob, global
	.4byte scalar_blob
	.4byte scalar_blob+0x2
	.rel entry_fn, .L_after
.endobj pointer_blob

# .data:0x24 | 0x80010024 | size: 0x8
.obj local_blob, local
.L_local_data:
	.byte 0xAA, 0xBB, 0xCC, 0xDD
	.4byte .L_local_data
.endobj local_blob

# .data:0x2C | 0x8001002C | size: 0x10
.obj zero_blob, global
	.skip 0x10
.endobj zero_blob
