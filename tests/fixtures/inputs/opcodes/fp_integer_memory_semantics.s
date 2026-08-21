.text

.global scalar_fctiw_semantics
.fn scalar_fctiw_semantics, global
/* 80006F00 00000F00  FC 40 08 1C */ fctiw f2, f1
/* 80006F04 00000F04  FC 60 08 1D */ fctiw. f3, f1
/* 80006F08 00000F08  4E 80 00 20 */ blr
.endfn scalar_fctiw_semantics

.global integer_word_memory_semantics
.fn integer_word_memory_semantics, global
/* 80006F20 00000F20  7C 83 2F AE */ stfiwx f4, r3, r5
/* 80006F24 00000F24  7C C3 3E 2C */ lhbrx r6, r3, r7
/* 80006F28 00000F28  7D 00 4E 2C */ lhbrx r8, r0, r9
/* 80006F2C 00000F2C  4E 80 00 20 */ blr
.endfn integer_word_memory_semantics
