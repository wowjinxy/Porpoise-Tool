.include "macros.inc"

.text
.global unsupported_instruction
.fn unsupported_instruction, global
/* 80005000 00000000  FF FF FF FF */	porpoise_unknown r3, r4
/* 80005004 00000004  4E 80 00 20 */	blr
.endfn unsupported_instruction
