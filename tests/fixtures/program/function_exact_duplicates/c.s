.text
.fn canonical_body, global
.same_label:
/* 80004000 00000000 3C608001 */ lis r3, object_c@ha
/* 80004004 00000004 3C808001 */ lis r4, object_c@h
/* 80004008 00000008 38631234 */ addi r3, r3, object_c@l
/* 8000400C 0000000C 80AD0020 */ lwz r5, small_c@sda21(r0)
/* 80004010 00000010 4E800020 */ blr
.endfn canonical_body
