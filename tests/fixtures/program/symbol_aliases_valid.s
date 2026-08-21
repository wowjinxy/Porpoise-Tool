.text
.sym "entry.alias", global
.fn primary, global
.sym start_alias, local
/* 80001000 00000000 60000000 */ nop
.sym resume_alias, global
resume_label:
.sym same-address-alias, local
/* 80001004 00000004 4E800020 */ blr
.endfn primary

.sym second_alias, local
.fn secondary, local
/* 80002000 00000000 4E800020 */ blr
.endfn secondary
