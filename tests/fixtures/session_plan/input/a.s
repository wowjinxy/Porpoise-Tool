.text
.fn lift_me, global
/* 80001000 00000000 4E800020 */ blr
.endfn lift_me

.fn gap_01_80002000_text, global
/* 80002000 00000004 DEADBEEF */ .4byte 0xDEADBEEF /* invalid */
.endfn gap_01_80002000_text
