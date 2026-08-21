.include "macros.inc"
.file "dtk_data_symbols.c"

.text
.fn entry, global
/* 80001000 00000000  4E 80 00 20 */ blr
.endfn entry

# 0x80002000..0x8000200C | size: 0xC
.init
# .init:0x0 | 0x80002000 | size: 0x0
.sym executable_blob, global
/* 80002000 00000000  4D 65 74 72 */ .4byte 0x4D657472 /* invalid */
/* 80002004 00000004  6F 77 65 72 */ xoris r23, r27, 0x6572
.L_executable_blob:
/* 80002008 00000008  6B 73 20 54 */ xori r19, r27, 0x2054
# .init:0xC | 0x8000200C | size: 0x0
.sym executable_blob_end, global

# .init:0x20 | 0x80002020 | size: 0x4
.fn after_blob, global
/* 80002020 00000020  4E 80 00 20 */ blr
.endfn after_blob

# 0x80003000..0x80003008 | size: 0x8
.data
# .data:0x0 | 0x80003000 | size: 0x0
.sym ...data.0, local

# .data:0x0 | 0x80003000 | size: 0x8
.obj pointer_pair, global
.L_pointer_target:
    .4byte 0x00000000
    .rel ...data.0, .L_pointer_target
.endobj pointer_pair

# 0x80004000..0x80004008 | size: 0x8
.section .bss, "wa", @nobits
# .bss:0x0 | 0x80004000 | size: 0x8
.obj storage, local
# .bss:0x0 | 0x80004000 | size: 0x0
.sym ...bss.0, local
    .skip 0x8
.endobj storage

# 0x80005000..0x80005008 | size: 0x8
.rodata
# .rodata:0x0 | 0x80005000 | size: 0x8
.obj implicit_section_pointer, local
    .4byte ...rodata.0+0x4
    .4byte 0x00000000
.endobj implicit_section_pointer
