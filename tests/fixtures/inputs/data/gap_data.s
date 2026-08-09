.text

.fn gap_01_80300100_text, global
.hidden gap_01_80300100_text
/* 80300100 00000100  4D 65 74 72 */ xoris r5, r11, 0x7472
/* 80300104 00000104  00 00 00 00 */ .4byte 0x00000000 /* invalid */
.endfn gap_01_80300100_text

.fn gap_helper, global
/* 80006100 00000108  4E 80 00 20 */ blr
.endfn gap_helper

.fn gap_02_80300200_text, global
/* 80006200 0000010C  4E 80 00 20 */ blr
.endfn gap_02_80300200_text
