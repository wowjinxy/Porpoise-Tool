.text
.fn main, global
/* 80001000 00000000 4E800020 */ blr
.endfn main

.fn __OSThreadInit, global
/* 80002000 00000004 4E800020 */ blr
.endfn __OSThreadInit

.fn __init_user, local
/* 80003000 00000008 4E800020 */ blr
.endfn __init_user

.fn __init_user, local
/* 80003100 00000108 4E800020 */ blr
.endfn __init_user

.fn DVDInit, global
/* 80004000 0000000C 4E800020 */ blr
.endfn DVDInit
