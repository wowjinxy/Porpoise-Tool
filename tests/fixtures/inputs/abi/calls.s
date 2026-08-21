.include "macros.inc"

.text
.global call_imports
.fn call_imports, global
/* 80004000 00000000  48 00 00 01 */ bl PorpoiseStubAdd
/* 80004004 00000004  48 00 00 01 */ bl PorpoiseStubIdentity
/* 80004008 00000008  48 00 00 01 */ bl PorpoiseStubFloatMix
/* 8000400C 0000000C  48 00 00 01 */ bl PorpoiseStubReport
/* 80004010 00000010  4E 80 00 20 */ blr
.endfn call_imports
