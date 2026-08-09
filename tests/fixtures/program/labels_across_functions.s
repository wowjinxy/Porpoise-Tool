.text
.fn first, global
unique_target:
also_unique:
/* 80001000 00000000 60000000 */ nop
.endfn first

.fn second, local
.Lshared:
/* 80002000 00000000 60000000 */ nop
dangling_target:
.endfn second

.fn third, local
.Lshared:
/* 80003000 00000000 4E800020 */ blr
.endfn third
