.text
.fn caller_a, global
/* 80010000 00000000 48000101 */ bl helper
/* 80010004 00000004 4E800020 */ blr
.endfn caller_a

.sym scoped_alias, local
.fn helper, local
/* 80010100 00000000 4E800020 */ blr
.endfn helper

.section .init, "ax"
.fn init_caller_a, global
/* 80010200 00000000 48000101 */ bl scoped_alias
/* 80010204 00000004 4E800020 */ blr
.endfn init_caller_a

.sym scoped_alias, local
.fn init_helper, local
/* 80010300 00000000 4E800020 */ blr
.endfn init_helper
