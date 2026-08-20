.text
.fn caller_b, global
/* 80011000 00000000 48000101 */ bl helper
/* 80011004 00000004 4E800020 */ blr
.endfn caller_b

.sym scoped_alias, local
.fn helper, local
/* 80011100 00000000 4E800020 */ blr
.endfn helper
