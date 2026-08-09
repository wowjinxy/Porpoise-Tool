.include "macros.inc"

.section metadata, "a"
/*
 * Generic object metadata:
 * Uses frame: no
 * Saved registers: none
 */
.4byte 0x00000000

.text
.fn commented_function, global
/*
 * Ordinary comments within a function are metadata too.
 */
/* 8000A000 00000000  60 00 00 00 */ nop
/* 8000A004 00000004  4E 80 00 20 */ blr
.endfn commented_function
