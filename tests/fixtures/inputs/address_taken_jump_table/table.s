.include "macros.inc"
.file "jump_table.c"

# 0x80010000..0x8001000C | size: 0xC
.data
# .data:0x0 | 0x80010000 | size: 0xC
.obj jump_table, global
    .4byte jump_dispatch+0x14
    .4byte jump_dispatch+0x14
    .4byte jump_dispatch+0x1C
.endobj jump_table
