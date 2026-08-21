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

.global relocation_immediate_semantics
.fn relocation_immediate_semantics, global
/* 80006500 00000500  3C 60 80 01 */ lis r3, synthetic_address@ha
/* 80006504 00000504  38 83 FF FC */ addi r4, r3, synthetic_address@l
/* 80006508 00000508  3C A0 7F FF */ lis r5, synthetic_address@h
/* 8000650C 0000050C  38 CD FF F0 */ li r6, synthetic_sda@sda21
/* 80006510 00000510  34 E6 00 10 */ addic. r7, r6, synthetic_low@l
/* 80006514 00000514  60 08 12 34 */ ori r8, r0, synthetic_mask@l
/* 80006518 00000518  4E 80 00 20 */ blr
.endfn relocation_immediate_semantics

.global relocation_memory_semantics
.fn relocation_memory_semantics, global
/* 80006540 00000540  81 23 00 20 */ lwz r9, synthetic_word@l(r3)
/* 80006544 00000544  91 2D 00 30 */ stw r9, synthetic_sda_word@sda21(r0)
/* 80006548 00000548  81 4D 00 30 */ lwz r10, synthetic_sda_word@sda21(r0)
/* 8000654C 0000054C  C0 23 00 24 */ lfs f1, synthetic_float@l(r3)
/* 80006550 00000550  C8 42 00 40 */ lfd f2, synthetic_double@sda21(r0)
/* 80006554 00000554  D0 22 00 50 */ stfs f1, synthetic_float_sda@sda21(r0)
/* 80006558 00000558  85 63 00 28 */ lwzu r11, synthetic_update@l(r3)
/* 8000655C 0000055C  4E 80 00 20 */ blr
.endfn relocation_memory_semantics

.global quoted_direct_branch_semantics
.fn quoted_direct_branch_semantics, global
/* 80006580 00000580  48 00 00 41 */ bl "quoted,branch\\callee"
/* 80006584 00000584  4E 80 00 20 */ blr
.endfn quoted_direct_branch_semantics

.global quoted_conditional_branch_semantics
.fn quoted_conditional_branch_semantics, global
/* 800065A0 000005A0  38 60 00 00 */ li r3, 0
/* 800065A4 000005A4  2C 03 00 00 */ cmpwi r3, 0
/* 800065A8 000005A8  41 82 00 18 */ beq "quoted,branch\\callee"
/* 800065AC 000005AC  38 60 00 01 */ li r3, 1
/* 800065B0 000005B0  4E 80 00 20 */ blr
.endfn quoted_conditional_branch_semantics

.fn "quoted,branch\callee", global
/* 800065C0 000005C0  38 60 00 2A */ li r3, 42
/* 800065C4 000005C4  4E 80 00 20 */ blr
.endfn "quoted,branch\callee"

.global scalar_compare_semantics
.fn scalar_compare_semantics, global
/* 80006600 00000600  FD 01 10 00 */ fcmpu cr2, f1, f2
/* 80006604 00000604  FD 83 10 40 */ fcmpo cr3, f3, f2
/* 80006608 00000608  4E 80 00 20 */ blr
.endfn scalar_compare_semantics

.global scalar_select_semantics
.fn scalar_select_semantics, global
/* 80006620 00000620  FC E4 30 6E */ fsel f7, f4, f5, f6
/* 80006624 00000624  FD 09 30 6F */ fsel. f8, f9, f5, f6
/* 80006628 00000628  4E 80 00 20 */ blr
.endfn scalar_select_semantics

.global scalar_unary_record_semantics
.fn scalar_unary_record_semantics, global
/* 80006640 00000640  FD 60 50 91 */ fmr. f11, f10
/* 80006644 00000644  FD 80 50 51 */ fneg. f12, f10
/* 80006648 00000648  FD A0 52 11 */ fabs. f13, f10
/* 8000664C 0000064C  FD C0 51 11 */ fnabs. f14, f10
/* 80006650 00000650  4E 80 00 20 */ blr
.endfn scalar_unary_record_semantics

.global remaining_integer_semantics
.fn remaining_integer_semantics, global
/* 80006700 00000700  60 00 00 00 */ divw r5, r3, r4
/* 80006704 00000704  60 00 00 00 */ divwu r6, r7, r8
/* 80006708 00000708  60 00 00 00 */ mulhw r9, r10, r11
/* 8000670C 0000070C  60 00 00 00 */ mulhwu r12, r10, r11
/* 80006710 00000710  60 00 00 00 */ rlwnm r13, r14, r15, 8, 23
/* 80006714 00000714  60 00 00 00 */ orc r16, r17, r18
/* 80006718 00000718  4E 80 00 20 */ blr
.endfn remaining_integer_semantics

.global remaining_divide_exception_semantics
.fn remaining_divide_exception_semantics, global
/* 80006740 00000740  60 00 00 00 */ divw r5, r3, r4
/* 80006744 00000744  60 00 00 00 */ divwu r6, r7, r8
/* 80006748 00000748  60 00 00 00 */ divw. r9, r10, r11
/* 8000674C 0000074C  4E 80 00 20 */ blr
.endfn remaining_divide_exception_semantics

.global remaining_divwu_record_semantics
.fn remaining_divwu_record_semantics, global
/* 80006760 00000760  60 00 00 00 */ divwu. r5, r3, r4
/* 80006764 00000764  4E 80 00 20 */ blr
.endfn remaining_divwu_record_semantics

.global remaining_subfze_semantics
.fn remaining_subfze_semantics, global
/* 80006780 00000780  60 00 00 00 */ subfze r5, r3
/* 80006784 00000784  60 00 00 00 */ subfze. r6, r4
/* 80006788 00000788  4E 80 00 20 */ blr
.endfn remaining_subfze_semantics

.global remaining_sthbrx_semantics
.fn remaining_sthbrx_semantics, global
/* 800067A0 000007A0  60 00 00 00 */ sthbrx r5, r3, r4
/* 800067A4 000007A4  4E 80 00 20 */ blr
.endfn remaining_sthbrx_semantics

.global remaining_branch_hint_semantics
.fn remaining_branch_hint_semantics, global
/* 800067C0 000007C0  38 80 00 00 */ li r4, 0
/* 800067C4 000007C4  38 60 00 00 */ li r3, 0
/* 800067C8 000007C8  2C 03 00 00 */ cmpwi r3, 0
/* 800067CC 000007CC  41 82 00 08 */ beq+ remaining_branch_hint_equal
/* 800067D0 000007D0  38 80 00 01 */ li r4, 1
remaining_branch_hint_equal:
/* 800067D4 000007D4  2C 03 00 01 */ cmpwi r3, 1
/* 800067D8 000007D8  40 81 00 08 */ ble+ remaining_branch_hint_done
/* 800067DC 000007DC  38 80 00 02 */ li r4, 2
remaining_branch_hint_done:
/* 800067E0 000007E0  4E 80 00 20 */ blr
.endfn remaining_branch_hint_semantics

.global remaining_beqlr_semantics
.fn remaining_beqlr_semantics, global
/* 80006800 00000800  4D 82 00 20 */ beqlr
/* 80006804 00000804  38 60 00 00 */ li r3, 0
/* 80006808 00000808  4E 80 00 20 */ blr
.endfn remaining_beqlr_semantics

.global remaining_bnelr_semantics
.fn remaining_bnelr_semantics, global
/* 80006820 00000820  4C 86 00 20 */ bnelr cr1
/* 80006824 00000824  38 60 00 00 */ li r3, 0
/* 80006828 00000828  4E 80 00 20 */ blr
.endfn remaining_bnelr_semantics

.global remaining_bgelr_semantics
.fn remaining_bgelr_semantics, global
/* 80006840 00000840  4C 88 00 20 */ bgelr cr2
/* 80006844 00000844  38 60 00 00 */ li r3, 0
/* 80006848 00000848  4E 80 00 20 */ blr
.endfn remaining_bgelr_semantics

.global remaining_blelr_semantics
.fn remaining_blelr_semantics, global
/* 80006860 00000860  4C 91 00 20 */ blelr cr3
/* 80006864 00000864  38 60 00 00 */ li r3, 0
/* 80006868 00000868  4E 80 00 20 */ blr
.endfn remaining_blelr_semantics

.global remaining_bgtlr_semantics
.fn remaining_bgtlr_semantics, global
/* 80006880 00000880  4D 91 00 20 */ bgtlr cr4
/* 80006884 00000884  38 60 00 00 */ li r3, 0
/* 80006888 00000888  4E 80 00 20 */ blr
.endfn remaining_bgtlr_semantics

.global remaining_bltlr_semantics
.fn remaining_bltlr_semantics, global
/* 800068A0 000008A0  4D 98 00 20 */ bltlr cr5
/* 800068A4 000008A4  38 60 00 00 */ li r3, 0
/* 800068A8 000008A8  4E 80 00 20 */ blr
.endfn remaining_bltlr_semantics

.global remaining_blrl_semantics
.fn remaining_blrl_semantics, global
/* 80006900 00000900  3D 80 80 00 */ lis r12, 0x8000
/* 80006904 00000904  61 8C 69 20 */ ori r12, r12, 0x6920
/* 80006908 00000908  7D 88 03 A6 */ mtlr r12
/* 8000690C 0000090C  4E 80 00 21 */ blrl
/* 80006910 00000910  4E 80 00 20 */ blr
.endfn remaining_blrl_semantics

.fn remaining_blrl_callee, global
/* 80006920 00000920  38 60 00 4D */ li r3, 77
/* 80006924 00000924  4E 80 00 20 */ blr
.endfn remaining_blrl_callee

.global remaining_raw_float_memory_semantics
.fn remaining_raw_float_memory_semantics, global
/* 80006940 00000940  C0 23 00 00 */ lfs f1, 0(r3)
/* 80006944 00000944  D0 23 00 04 */ stfs f1, 4(r3)
/* 80006948 00000948  C8 43 00 08 */ lfd f2, 8(r3)
/* 8000694C 0000094C  D8 43 00 10 */ stfd f2, 16(r3)
/* 80006950 00000950  7C 63 24 2E */ lfsx f3, r3, r4
/* 80006954 00000954  7C 63 2D 2E */ stfsx f3, r3, r5
/* 80006958 00000958  7C 83 34 AE */ lfdx f4, r3, r6
/* 8000695C 0000095C  7C 83 3D AE */ stfdx f4, r3, r7
/* 80006960 00000960  4E 80 00 20 */ blr
.endfn remaining_raw_float_memory_semantics

.global remaining_scalar_single_lane_semantics
.fn remaining_scalar_single_lane_semantics, global
/* 80006980 00000980  EC 61 10 2A */ fadds f3, f1, f2
/* 80006984 00000984  4E 80 00 20 */ blr
.endfn remaining_scalar_single_lane_semantics

.global paired_advanced_arithmetic_semantics
.fn paired_advanced_arithmetic_semantics, global
/* 80006A00 00000A00  60 00 00 00 */ ps_madd f4, f1, f2, f3
/* 80006A04 00000A04  60 00 00 00 */ ps_msub f5, f1, f2, f3
/* 80006A08 00000A08  60 00 00 00 */ ps_nmadd f6, f1, f2, f3
/* 80006A0C 00000A0C  60 00 00 00 */ ps_nmsub f7, f1, f2, f3
/* 80006A10 00000A10  60 00 00 00 */ ps_madds0 f8, f1, f2, f3
/* 80006A14 00000A14  60 00 00 00 */ ps_madds1 f9, f1, f2, f3
/* 80006A18 00000A18  60 00 00 00 */ ps_muls0 f10, f1, f2
/* 80006A1C 00000A1C  60 00 00 00 */ ps_muls1 f11, f1, f2
/* 80006A20 00000A20  60 00 00 00 */ ps_sum0 f12, f1, f2, f3
/* 80006A24 00000A24  60 00 00 00 */ ps_sum1 f13, f1, f2, f3
/* 80006A28 00000A28  60 00 00 00 */ ps_div f14, f15, f1
/* 80006A2C 00000A2C  4E 80 00 20 */ blr
.endfn paired_advanced_arithmetic_semantics

.global paired_exact_data_semantics
.fn paired_exact_data_semantics, global
/* 80006A40 00000A40  60 00 00 00 */ ps_merge00 f4, f1, f2
/* 80006A44 00000A44  60 00 00 00 */ ps_merge01 f5, f1, f2
/* 80006A48 00000A48  60 00 00 00 */ ps_merge10 f6, f1, f2
/* 80006A4C 00000A4C  60 00 00 00 */ ps_merge11 f7, f1, f2
/* 80006A50 00000A50  60 00 00 00 */ ps_mr f8, f1
/* 80006A54 00000A54  60 00 00 00 */ ps_neg f9, f1
/* 80006A58 00000A58  60 00 00 00 */ ps_sel f10, f3, f11, f12
/* 80006A5C 00000A5C  60 00 00 00 */ ps_cmpo0 cr6, f13, f14
/* 80006A60 00000A60  4E 80 00 20 */ blr
.endfn paired_exact_data_semantics

.global alias_mid_owner
.fn alias_mid_owner, global
/* 80006B00 00000B00  38 60 00 01 */ li r3, 1
.sym alias_mid_entry, global
/* 80006B04 00000B04  38 63 00 04 */ addi r3, r3, 4
/* 80006B08 00000B08  4E 80 00 20 */ blr
.endfn alias_mid_owner

.sym alias_pre_entry, global
.global alias_pre_owner
.fn alias_pre_owner, global
/* 80006B20 00000B20  38 60 00 07 */ li r3, 7
/* 80006B24 00000B24  4E 80 00 20 */ blr
.endfn alias_pre_owner

.global cross_label_linked_caller
.fn cross_label_linked_caller, global
/* 80006B40 00000B40  38 60 00 0A */ li r3, 10
/* 80006B44 00000B44  48 00 00 01 */ bl unique_cross_target
/* 80006B48 00000B48  38 63 00 01 */ addi r3, r3, 1
/* 80006B4C 00000B4C  4E 80 00 20 */ blr
.endfn cross_label_linked_caller

.global cross_label_tail_caller
.fn cross_label_tail_caller, global
/* 80006B60 00000B60  38 60 00 14 */ li r3, 20
/* 80006B64 00000B64  48 00 00 00 */ b unique_cross_target
/* 80006B68 00000B68  38 60 00 00 */ li r3, 0
/* 80006B6C 00000B6C  4E 80 00 20 */ blr
.endfn cross_label_tail_caller

.global cross_label_conditional_caller
.fn cross_label_conditional_caller, global
/* 80006B80 00000B80  38 60 00 1E */ li r3, 30
/* 80006B84 00000B84  2C 03 00 1E */ cmpwi r3, 30
/* 80006B88 00000B88  41 82 00 08 */ beq unique_cross_target
/* 80006B8C 00000B8C  38 60 00 00 */ li r3, 0
/* 80006B90 00000B90  4E 80 00 20 */ blr
.endfn cross_label_conditional_caller

.global cross_label_owner
.fn cross_label_owner, global
/* 80006BA0 00000BA0  38 60 00 64 */ li r3, 100
unique_cross_target:
/* 80006BA4 00000BA4  38 63 00 05 */ addi r3, r3, 5
/* 80006BA8 00000BA8  4E 80 00 20 */ blr
.endfn cross_label_owner

.global alias_branch_caller
.fn alias_branch_caller, global
/* 80006BC0 00000BC0  38 60 00 14 */ li r3, 20
/* 80006BC4 00000BC4  48 00 00 01 */ bl alias_mid_entry
/* 80006BC8 00000BC8  38 63 00 01 */ addi r3, r3, 1
/* 80006BCC 00000BCC  4E 80 00 20 */ blr
.endfn alias_branch_caller

.global scalar_frsp_semantics
.fn scalar_frsp_semantics, global
/* 80006C00 00000C00  60 00 00 00 */ frsp f2, f1
/* 80006C04 00000C04  60 00 00 00 */ frsp. f3, f1
/* 80006C08 00000C08  4E 80 00 20 */ blr
.endfn scalar_frsp_semantics

.global scalar_fctiwz_semantics
.fn scalar_fctiwz_semantics, global
/* 80006C20 00000C20  60 00 00 00 */ fctiwz f2, f1
/* 80006C24 00000C24  60 00 00 00 */ fctiwz. f3, f1
/* 80006C28 00000C28  4E 80 00 20 */ blr
.endfn scalar_fctiwz_semantics

.global scalar_mffs_semantics
.fn scalar_mffs_semantics, global
/* 80006C40 00000C40  60 00 00 00 */ mffs f2
/* 80006C44 00000C44  60 00 00 00 */ mffs. f3
/* 80006C48 00000C48  4E 80 00 20 */ blr
.endfn scalar_mffs_semantics

.global scalar_mtfsf_semantics
.fn scalar_mtfsf_semantics, global
/* 80006C60 00000C60  60 00 00 00 */ mtfsf 0x80, f1
/* 80006C64 00000C64  60 00 00 00 */ mtfsf. 0x01, f2
/* 80006C68 00000C68  4E 80 00 20 */ blr
.endfn scalar_mtfsf_semantics

.global scalar_mtfsb1_semantics
.fn scalar_mtfsb1_semantics, global
/* 80006C80 00000C80  60 00 00 00 */ mtfsb1 cr7gt
/* 80006C84 00000C84  60 00 00 00 */ mtfsb1. 3
/* 80006C88 00000C88  4E 80 00 20 */ blr
.endfn scalar_mtfsb1_semantics

.global scalar_fma_double_semantics
.fn scalar_fma_double_semantics, global
/* 80006CA0 00000CA0  60 00 00 00 */ fmadd f4, f1, f2, f3
/* 80006CA4 00000CA4  60 00 00 00 */ fmsub f5, f1, f2, f3
/* 80006CA8 00000CA8  60 00 00 00 */ fnmadd f6, f1, f2, f3
/* 80006CAC 00000CAC  60 00 00 00 */ fnmsub f7, f1, f2, f3
/* 80006CB0 00000CB0  60 00 00 00 */ fmadd. f8, f1, f2, f3
/* 80006CB4 00000CB4  4E 80 00 20 */ blr
.endfn scalar_fma_double_semantics

.global scalar_fma_single_semantics
.fn scalar_fma_single_semantics, global
/* 80006CC0 00000CC0  60 00 00 00 */ fmadds f4, f1, f2, f3
/* 80006CC4 00000CC4  60 00 00 00 */ fmsubs f5, f1, f2, f3
/* 80006CC8 00000CC8  60 00 00 00 */ fnmadds f6, f1, f2, f3
/* 80006CCC 00000CCC  60 00 00 00 */ fnmsubs f7, f1, f2, f3
/* 80006CD0 00000CD0  60 00 00 00 */ fmadds. f8, f1, f2, f3
/* 80006CD4 00000CD4  4E 80 00 20 */ blr
.endfn scalar_fma_single_semantics

.global psq_d_load_semantics
.fn psq_d_load_semantics, global
/* 80006D00 00000D00  E2 83 18 00 */ psq_l f20, -2048(r3), 0, qr1
/* 80006D04 00000D04  E2 A4 A7 FF */ psq_l f21, 2047(r4), 1, qr2
/* 80006D08 00000D08  4E 80 00 20 */ blr
.endfn psq_d_load_semantics

.global psq_d_load_update_semantics
.fn psq_d_load_update_semantics, global
/* 80006D20 00000D20  E6 C5 18 00 */ psq_lu f22, -2048(r5), 0, qr1
/* 80006D24 00000D24  E6 E6 A7 FF */ psq_lu f23, 2047(r6), 1, qr2
/* 80006D28 00000D28  4E 80 00 20 */ blr
.endfn psq_d_load_update_semantics

.global psq_d_store_semantics
.fn psq_d_store_semantics, global
/* 80006D40 00000D40  F3 07 18 00 */ psq_st f24, -2048(r7), 0, qr1
/* 80006D44 00000D44  F3 28 A7 FF */ psq_st f25, 2047(r8), 1, qr2
/* 80006D48 00000D48  4E 80 00 20 */ blr
.endfn psq_d_store_semantics

.global psq_d_store_update_semantics
.fn psq_d_store_update_semantics, global
/* 80006D60 00000D60  F7 49 18 00 */ psq_stu f26, -2048(r9), 0, qr1
/* 80006D64 00000D64  F7 6A A7 FF */ psq_stu f27, 2047(r10), 1, qr2
/* 80006D68 00000D68  4E 80 00 20 */ blr
.endfn psq_d_store_update_semantics

.global psq_indexed_load_semantics
.fn psq_indexed_load_semantics, global
/* 80006D80 00000D80  13 80 58 8C */ psq_lx f28, r0, r11, 0, qr1
/* 80006D84 00000D84  13 AC 6D 0C */ psq_lx f29, r12, r13, 1, qr2
/* 80006D88 00000D88  4E 80 00 20 */ blr
.endfn psq_indexed_load_semantics

.global psq_indexed_store_semantics
.fn psq_indexed_store_semantics, global
/* 80006DA0 00000DA0  13 C0 70 8E */ psq_stx f30, r0, r14, 0, qr1
/* 80006DA4 00000DA4  13 EF 85 0E */ psq_stx f31, r15, r16, 1, qr2
/* 80006DA8 00000DA8  4E 80 00 20 */ blr
.endfn psq_indexed_store_semantics

.global psq_indexed_load_update_semantics
.fn psq_indexed_load_update_semantics, global
/* 80006DC0 00000DC0  12 52 98 CC */ psq_lux f18, r18, r19, 0, qr1
/* 80006DC4 00000DC4  12 34 AD 4C */ psq_lux f17, r20, r21, 1, qr2
/* 80006DC8 00000DC8  4E 80 00 20 */ blr
.endfn psq_indexed_load_update_semantics

.global psq_indexed_store_update_semantics
.fn psq_indexed_store_update_semantics, global
/* 80006DE0 00000DE0  12 16 B8 CE */ psq_stux f16, r22, r23, 0, qr1
/* 80006DE4 00000DE4  11 F8 CD 4E */ psq_stux f15, r24, r25, 1, qr2
/* 80006DE8 00000DE8  4E 80 00 20 */ blr
.endfn psq_indexed_store_update_semantics

.global psq_empty_displacement_semantics
.fn psq_empty_displacement_semantics, global
/* 80006E00 00000E00  E2 71 90 00 */ psq_l f19, (r17), 1, qr1
/* 80006E04 00000E04  4E 80 00 20 */ blr
.endfn psq_empty_displacement_semantics

.global integer_alias_semantics
.fn integer_alias_semantics, global
/* 80006E20 00000E20  7C 65 22 38 */ eqv r5, r3, r4
/* 80006E24 00000E24  5C 67 40 3E */ rotlw r7, r3, r8
/* 80006E28 00000E28  4E 80 00 20 */ blr
.endfn integer_alias_semantics

.global predicted_not_equal_branch_semantics
.fn predicted_not_equal_branch_semantics, global
/* 80006E40 00000E40  38 80 00 00 */ li r4, 0
/* 80006E44 00000E44  2C 03 00 00 */ cmpwi r3, 0
/* 80006E48 00000E48  40 A2 00 08 */ bne+ predicted_not_equal_branch_done
/* 80006E4C 00000E4C  38 80 00 01 */ li r4, 1
predicted_not_equal_branch_done:
/* 80006E50 00000E50  4E 80 00 20 */ blr
.endfn predicted_not_equal_branch_semantics

.global counter_zero_branch_semantics
.fn counter_zero_branch_semantics, global
/* 80006E60 00000E60  38 A0 00 01 */ li r5, 1
/* 80006E64 00000E64  42 40 00 08 */ bdz counter_zero_branch_done
/* 80006E68 00000E68  38 A0 00 02 */ li r5, 2
counter_zero_branch_done:
/* 80006E6C 00000E6C  4E 80 00 20 */ blr
.endfn counter_zero_branch_semantics

.global absolute_link_branch_semantics
.fn absolute_link_branch_semantics, global
/* 80006E80 00000E80  38 C0 00 00 */ li r6, 0
/* 80006E84 00000E84  48 00 00 63 */ bla 0x60
/* 80006E88 00000E88  7C E8 02 A6 */ mflr r7
/* 80006E8C 00000E8C  4E 80 00 20 */ blr
.endfn absolute_link_branch_semantics

.global absolute_branch_target
.fn absolute_branch_target, global
/* 00000060 00000060  38 C0 00 33 */ li r6, 0x33
/* 00000064 00000064  4E 80 00 20 */ blr
.endfn absolute_branch_target
