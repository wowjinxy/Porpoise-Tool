.include "macros.inc"

.text
.global extended_integer_semantics
.fn extended_integer_semantics, global
/* 80006000 00000000  60 00 00 00 */ li r3, -1
/* 80006004 00000004  60 00 00 00 */ clrlwi r4, r3, 24
/* 80006008 00000008  60 00 00 00 */ clrrwi r5, r3, 24
/* 8000600C 0000000C  60 00 00 00 */ slwi r6, r4, 4
/* 80006010 00000010  60 00 00 00 */ srwi r7, r5, 28
/* 80006014 00000014  60 00 00 00 */ extrwi r8, r5, 8, 0
/* 80006018 00000018  60 00 00 00 */ extlwi r9, r5, 8, 0
/* 8000601C 0000001C  60 00 00 00 */ extsb r10, r4
/* 80006020 00000020  60 00 00 00 */ li r11, 0x8001
/* 80006024 00000024  60 00 00 00 */ extsh r11, r11
/* 80006028 00000028  60 00 00 00 */ cntlzw r12, r4
/* 8000602C 0000002C  60 00 00 00 */ neg r13, r4
/* 80006030 00000030  60 00 00 00 */ mullw r14, r4, r4
/* 80006034 00000034  60 00 00 00 */ andc r15, r5, r4
/* 80006038 00000038  60 00 00 00 */ nor r16, r4, r4
/* 8000603C 0000003C  60 00 00 00 */ subi r17, r4, 1
/* 80006040 00000040  60 00 00 00 */ lis r18, 2
/* 80006044 00000044  60 00 00 00 */ subis r18, r18, 1
/* 80006048 00000048  60 00 00 00 */ clrlslwi r19, r3, 16, 8
/* 8000604C 0000004C  60 00 00 00 */ rotlwi r20, r4, 8
/* 80006050 00000050  60 00 00 00 */ rotrwi r21, r4, 8
/* 80006054 00000054  60 00 00 00 */ lis r22, 0x1234
/* 80006058 00000058  60 00 00 00 */ ori r22, r22, 0x5678
/* 8000605C 0000005C  60 00 00 00 */ extlwi r23, r22, 8, 28
/* 80006060 00000060  60 00 00 00 */ blr
.endfn extended_integer_semantics

.global extended_carry_semantics
.fn extended_carry_semantics, global
/* 80006100 00000100  60 00 00 00 */ li r3, -1
/* 80006104 00000104  60 00 00 00 */ li r4, 0
/* 80006108 00000108  60 00 00 00 */ addic. r5, r3, 1
/* 8000610C 0000010C  60 00 00 00 */ subfic r6, r4, 5
/* 80006110 00000110  60 00 00 00 */ subic r7, r6, 1
/* 80006114 00000114  60 00 00 00 */ subic. r8, r4, 1
/* 80006118 00000118  60 00 00 00 */ addc r9, r3, r6
/* 8000611C 0000011C  60 00 00 00 */ adde r10, r4, r6
/* 80006120 00000120  60 00 00 00 */ addc r11, r3, r6
/* 80006124 00000124  60 00 00 00 */ addze r12, r6
/* 80006128 00000128  60 00 00 00 */ subfc r13, r6, r4
/* 8000612C 0000012C  60 00 00 00 */ addc r11, r3, r6
/* 80006130 00000130  60 00 00 00 */ subfe r14, r6, r4
/* 80006134 00000134  60 00 00 00 */ subfic r15, r4, 0
/* 80006138 00000138  60 00 00 00 */ blr
.endfn extended_carry_semantics

.global extended_addic_record_semantics
.fn extended_addic_record_semantics, global
/* 80006180 00000180  60 00 00 00 */ addic. r5, r3, 1
/* 80006184 00000184  60 00 00 00 */ blr
.endfn extended_addic_record_semantics

.global extended_subic_carry_semantics
.fn extended_subic_carry_semantics, global
/* 80006190 00000190  60 00 00 00 */ subic r5, r3, 1
/* 80006194 00000194  60 00 00 00 */ blr
.endfn extended_subic_carry_semantics

.global extended_subfc_carry_semantics
.fn extended_subfc_carry_semantics, global
/* 800061A0 000001A0  60 00 00 00 */ subfc r5, r3, r4
/* 800061A4 000001A4  60 00 00 00 */ blr
.endfn extended_subfc_carry_semantics

.global extended_adde_carry_semantics
.fn extended_adde_carry_semantics, global
/* 800061B0 000001B0  60 00 00 00 */ adde r5, r3, r4
/* 800061B4 000001B4  60 00 00 00 */ blr
.endfn extended_adde_carry_semantics

.global extended_addze_carry_semantics
.fn extended_addze_carry_semantics, global
/* 800061C0 000001C0  60 00 00 00 */ addze r5, r3
/* 800061C4 000001C4  60 00 00 00 */ blr
.endfn extended_addze_carry_semantics

.global extended_subfe_carry_semantics
.fn extended_subfe_carry_semantics, global
/* 800061D0 000001D0  60 00 00 00 */ subfe r5, r3, r4
/* 800061D4 000001D4  60 00 00 00 */ blr
.endfn extended_subfe_carry_semantics

.global extended_memory_semantics
.fn extended_memory_semantics, global
/* 80006200 00000200  60 00 00 00 */ stwx r5, r3, r4
/* 80006204 00000204  60 00 00 00 */ lwzx r6, r3, r4
/* 80006208 00000208  60 00 00 00 */ stbx r5, r3, r7
/* 8000620C 0000020C  60 00 00 00 */ lbzx r8, r3, r7
/* 80006210 00000210  60 00 00 00 */ sthx r5, r3, r9
/* 80006214 00000214  60 00 00 00 */ lhzx r10, r3, r9
/* 80006218 00000218  60 00 00 00 */ stfsx f1, r3, r11
/* 8000621C 0000021C  60 00 00 00 */ lfsx f2, r3, r11
/* 80006220 00000220  60 00 00 00 */ stmw r28, 0x20(r3)
/* 80006224 00000224  60 00 00 00 */ li r28, 0
/* 80006228 00000228  60 00 00 00 */ li r29, 0
/* 8000622C 0000022C  60 00 00 00 */ li r30, 0
/* 80006230 00000230  60 00 00 00 */ li r31, 0
/* 80006234 00000234  60 00 00 00 */ lmw r28, 0x20(r3)
/* 80006238 00000238  60 00 00 00 */ mr r20, r3
/* 8000623C 0000023C  60 00 00 00 */ stwux r5, r20, r12
/* 80006240 00000240  60 00 00 00 */ mr r21, r3
/* 80006244 00000244  60 00 00 00 */ lwzux r22, r21, r12
/* 80006248 00000248  60 00 00 00 */ blr
.endfn extended_memory_semantics

.global extended_lwzux_fault_semantics
.fn extended_lwzux_fault_semantics, global
/* 80006280 00000280  60 00 00 00 */ lwzux r22, r21, r12
/* 80006284 00000284  60 00 00 00 */ blr
.endfn extended_lwzux_fault_semantics

.global extended_stwux_fault_semantics
.fn extended_stwux_fault_semantics, global
/* 80006290 00000290  60 00 00 00 */ stwux r5, r20, r12
/* 80006294 00000294  60 00 00 00 */ blr
.endfn extended_stwux_fault_semantics

.global extended_stmw_fault_semantics
.fn extended_stmw_fault_semantics, global
/* 800062A0 000002A0  60 00 00 00 */ stmw r28, 0(r3)
/* 800062A4 000002A4  60 00 00 00 */ blr
.endfn extended_stmw_fault_semantics

.global extended_cr_semantics
.fn extended_cr_semantics, global
/* 80006300 00000300  60 00 00 00 */ li r3, 1
/* 80006304 00000304  60 00 00 00 */ cmpwi r3, 0
/* 80006308 00000308  60 00 00 00 */ cror eq, gt, eq
/* 8000630C 0000030C  60 00 00 00 */ crclr cr1eq
/* 80006310 00000310  60 00 00 00 */ crset cr1gt
/* 80006314 00000314  60 00 00 00 */ blr
.endfn extended_cr_semantics

.global extended_rlwinm_record_semantics
.fn extended_rlwinm_record_semantics, global
/* 80006400 00000400  60 00 00 00 */ li r3, -1
/* 80006404 00000404  60 00 00 00 */ rlwinm. r4, r3, 0, 0, 31
/* 80006408 00000408  60 00 00 00 */ blr
.endfn extended_rlwinm_record_semantics

.global extended_alias_record_semantics
.fn extended_alias_record_semantics, global
/* 80006420 00000420  60 00 00 00 */ li r3, -1
/* 80006424 00000424  60 00 00 00 */ srwi. r4, r3, 31
/* 80006428 00000428  60 00 00 00 */ blr
.endfn extended_alias_record_semantics

.global extended_sraw_record_semantics
.fn extended_sraw_record_semantics, global
/* 80006440 00000440  60 00 00 00 */ li r3, 0x8001
/* 80006444 00000444  60 00 00 00 */ li r4, 32
/* 80006448 00000448  60 00 00 00 */ sraw. r5, r3, r4
/* 8000644C 0000044C  60 00 00 00 */ blr
.endfn extended_sraw_record_semantics
