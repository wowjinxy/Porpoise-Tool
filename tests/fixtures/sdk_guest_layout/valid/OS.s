.file "OS.c"
.text
.fn OSInit, global
/* 80001000 00000000 38630001 */ addi r3, r3, 1
/* 80001004 00000004 38840002 */ addi r4, r4, 2
/* 80001008 00000008 38A50003 */ addi r5, r5, 3
/* 8000100C 0000000C 38C60004 */ addi r6, r6, 4
/* 80001010 00000010 38E70005 */ addi r7, r7, 5
/* 80001014 00000014 39080006 */ addi r8, r8, 6
/* 80001018 00000018 39290007 */ addi r9, r9, 7
/* 8000101C 0000001C 4E800020 */ blr
.endfn OSInit

.fn title_main, global
/* 80001020 00000020 38600000 */ li r3, 0
/* 80001024 00000024 4E800020 */ blr
.endfn title_main

# 0x8001010C..0x80010114 | size: 0x8
.section .sbss, "wa", @nobits
# .sbss:0x0 | 0x8001010C | size: 0x4
.obj BootInfo_8001010C, global
    .skip 0x4
.endobj BootInfo_8001010C

# .sbss:0x4 | 0x80010110 | size: 0x4
.obj BI2DebugFlag, local
    .skip 0x4
.endobj BI2DebugFlag
