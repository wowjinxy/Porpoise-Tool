.include "macros.inc"
.file "jump_dispatch.c"

.text
.global jump_dispatch
.fn jump_dispatch, global
/* 80008000 00000000  3C 80 80 01 */ lis r4, 0x8001
/* 80008004 00000004  54 60 10 3A */ slwi r0, r3, 2
/* 80008008 00000008  7C A4 00 2E */ lwzx r5, r4, r0
/* 8000800C 0000000C  7C A9 03 A6 */ mtctr r5
/* 80008010 00000010  4E 80 04 20 */ bctr
/* 80008014 00000014  38 60 00 11 */ li r3, 17
/* 80008018 00000018  4E 80 00 20 */ blr
/* 8000801C 0000001C  38 60 00 22 */ li r3, 34
/* 80008020 00000020  4E 80 00 20 */ blr
.endfn jump_dispatch

.global safe_anchor
.fn safe_anchor, global
/* 80008040 00000040  4E 80 00 20 */ blr
.endfn safe_anchor
