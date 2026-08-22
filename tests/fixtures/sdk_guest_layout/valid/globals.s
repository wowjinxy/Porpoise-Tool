.file "OSArena.c"
# 0x80010100..0x8001010C | size: 0xC
.section .sbss, "wa", @nobits
# .sbss:0x0 | 0x80010100 | size: 0x4
.obj __OSArenaLo_80010100, global
    .skip 0x4
.endobj __OSArenaLo_80010100

# .sbss:0x4 | 0x80010104 | size: 0x4
.obj __OSArenaHi, global
    .skip 0x4
.endobj __OSArenaHi

# .sbss:0x8 | 0x80010108 | size: 0x4
.obj AreWeInitialized_80010108, global
    .skip 0x4
.endobj AreWeInitialized_80010108

# 0x80010114..0x80010118 | size: 0x4
.section .bss, "wa", @nobits
# .bss:0x0 | 0x80010114 | size: 0x4
.obj __DVDLongFileNameFlag, global
    .skip 0x4
.endobj __DVDLongFileNameFlag

# 0x80010200..0x80010204 | size: 0x4
.section .data, "wa"
# .data:0x0 | 0x80010200 | size: 0x4
.obj BootInfo_80010200, global
    .4byte 0xDEADBEEF
.endobj BootInfo_80010200
