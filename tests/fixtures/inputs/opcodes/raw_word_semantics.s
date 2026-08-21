.text

.fn raw_invalid_encoding_semantics, global
/* 80019000 00000000  00 00 00 00 */ .4byte 0x00000000 /* invalid */
.endfn raw_invalid_encoding_semantics

.fn raw_lmw_overlap_semantics, global
/* 80019010 00000010  B8 03 00 00 */ .4byte 0xB8030000 /* illegal: lmw r0, 0x0(r3) */
/* 80019014 00000014  4E 80 00 20 */ blr
.endfn raw_lmw_overlap_semantics
