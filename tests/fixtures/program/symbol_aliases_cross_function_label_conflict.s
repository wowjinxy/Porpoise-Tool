.text
.sym "shared.label", global
.fn alias_owner, global
/* 80004000 00000000 60000000 */ nop
.endfn alias_owner

.fn label_owner, local
shared_label:
/* 80005000 00000000 4E800020 */ blr
.endfn label_owner
