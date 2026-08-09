.include "macros.inc"

.text
.global system_state_semantics
.fn system_state_semantics, global
/* 80008000 00000000  7C 61 02 A6 */ mfxer r3
/* 80008004 00000004  7C 61 03 A6 */ mtxer r3
/* 80008008 00000008  7C 90 E2 A6 */ mfspr r4, GQR0
/* 8000800C 0000000C  7C 91 E3 A6 */ mtspr GQR1, r4
/* 80008010 00000010  7C A0 00 A6 */ mfmsr r5
/* 80008014 00000014  7C C0 04 A6 */ mfsr r6, 0
/* 80008018 00000018  7D 68 11 20 */ mtcrf 0x81, r11
/* 8000801C 0000001C  7C FF 43 A6 */ mtspr PVR, r7
/* 80008020 00000020  7D 4C 42 E6 */ mftb r10
/* 80008024 00000024  7C 00 47 EC */ dcbz r0, r8
/* 80008028 00000028  7C 00 43 AC */ dcbi r0, r8
/* 8000802C 0000002C  7D 94 82 A6 */ mfibatu r12, 2
/* 80008030 00000030  7D B3 8A A6 */ mfibatl r13, 5
/* 80008034 00000034  7D DE 82 A6 */ mfdbatu r14, 3
/* 80008038 00000038  7D FD 8A A6 */ mfdbatl r15, 6
/* 8000803C 0000003C  7E 12 83 A6 */ mtibatu 1, r16
/* 80008040 00000040  7E 31 8B A6 */ mtibatl 4, r17
/* 80008044 00000044  7E 5C 83 A6 */ mtdbatu 2, r18
/* 80008048 00000048  7E 7F 8B A6 */ mtdbatl 7, r19
/* 8000804C 0000004C  7E 8C 42 E6 */ mftb r20, 268
/* 80008050 00000050  7E AD 42 E6 */ mftb r21, 269
/* 80008054 00000054  7F 1B EB A6 */ mtspr SIA, r24
/* 80008058 00000058  4E 80 00 20 */ blr
.endfn system_state_semantics

.global system_event_semantics
.fn system_event_semantics, global
/* 80008100 00000100  0F E9 00 00 */ twui r9, 0
/* 80008104 00000104  44 00 00 02 */ sc
/* 80008108 00000108  4E 80 00 20 */ blr
.endfn system_event_semantics

.global system_rfi_semantics
.fn system_rfi_semantics, global
/* 80008200 00000200  4C 00 00 64 */ rfi
.endfn system_rfi_semantics
