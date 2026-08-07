.include "macros.inc"

.text
.global integer_semantics
.fn integer_semantics, global
/* 80005000 00000000  38 60 00 01 */ addi r3, r0, 1
/* 80005004 00000004  38 80 00 02 */ li r4, 2
/* 80005008 00000008  7C A3 22 14 */ add r5, r3, r4
/* 8000500C 0000000C  7C 66 20 30 */ slw r6, r3, r4
/* 80005010 00000010  7C C8 0E 70 */ srawi r8, r6, 1
/* 80005014 00000014  54 C9 08 3E */ rlwinm r9, r6, 1, 0, 31
/* 80005018 00000018  90 A0 00 01 */ stw r5, 1(r0)
/* 8000501C 0000001C  80 E0 00 01 */ lwz r7, 1(r0)
/* 80005020 00000020  2C 07 00 03 */ cmpwi r7, 3
/* 80005024 00000024  40 82 00 10 */ bne .Lfailure
/* 80005028 00000028  38 60 00 07 */ li r3, 7
/* 8000502C 0000002C  7C 00 04 AC */ sync
/* 80005030 00000030  4E 80 00 20 */ blr
.Lfailure:
/* 80005034 00000034  38 60 FF FF */ li r3, -1
/* 80005038 00000038  4E 80 00 20 */ blr
.endfn integer_semantics

.global scalar_float_semantics
.fn scalar_float_semantics, global
/* 80005100 00000100  FC 61 10 2A */ fadd f3, f1, f2
/* 80005104 00000104  4E 80 00 20 */ blr
.endfn scalar_float_semantics

.global paired_float_semantics
.fn paired_float_semantics, global
/* 80005200 00000200  10 61 10 2A */ ps_add f3, f1, f2
/* 80005204 00000204  4E 80 00 20 */ blr
.endfn paired_float_semantics

.global branch_helper
.fn branch_helper, global
/* 80005300 00000300  38 63 00 01 */ addi r3, r3, 1
/* 80005304 00000304  4E 80 00 20 */ blr
.endfn branch_helper

.global direct_branch_semantics
.fn direct_branch_semantics, global
/* 80005310 00000310  38 60 00 04 */ li r3, 4
/* 80005314 00000314  48 00 00 01 */ bl branch_helper
/* 80005318 00000318  4E 80 00 20 */ blr
.endfn direct_branch_semantics

.global indirect_branch_semantics
.fn indirect_branch_semantics, global
/* 80005320 00000320  38 60 00 0A */ li r3, 10
/* 80005324 00000324  3C 80 80 00 */ lis r4, -32768
/* 80005328 00000328  60 84 53 00 */ ori r4, r4, 0x5300
/* 8000532C 0000032C  7C 89 03 A6 */ mtctr r4
/* 80005330 00000330  4E 80 04 21 */ bctrl
/* 80005334 00000334  4E 80 00 20 */ blr
.endfn indirect_branch_semantics

.global faulting_load
.fn faulting_load, global
/* 80005400 00000400  3C 80 70 00 */ lis r4, 0x7000
/* 80005404 00000404  80 64 00 00 */ lwz r3, 0(r4)
/* 80005408 00000408  4E 80 00 20 */ blr
.endfn faulting_load

.global fault_propagation
.fn fault_propagation, global
/* 80005420 00000420  38 A0 00 01 */ li r5, 1
/* 80005424 00000424  4B FF FF DD */ bl faulting_load
/* 80005428 00000428  38 A0 00 02 */ li r5, 2
/* 8000542C 0000042C  4E 80 00 20 */ blr
.endfn fault_propagation
