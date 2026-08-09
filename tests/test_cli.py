#!/usr/bin/env python3
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile


TOOL = Path(sys.argv[1]).resolve()
ROOT = Path(sys.argv[2]).resolve()
FIXTURES = ROOT / "tests" / "fixtures"
CHILD_MESON_ARGS = shlex.split(os.environ.get("PORPOISE_TEST_MESON_ARGS", ""))


def run(*arguments, cwd=None, expected=0):
    completed = subprocess.run(
        [str(argument) for argument in arguments],
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if completed.returncode != expected:
        raise AssertionError(
            f"expected exit {expected}, got {completed.returncode}: {' '.join(map(str, arguments))}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    return completed


def tree_digest(path):
    digest = hashlib.sha256()
    for file in sorted(value for value in path.rglob("*") if value.is_file()):
        digest.update(file.relative_to(path).as_posix().encode())
        digest.update(b"\0")
        digest.update(file.read_bytes())
    return digest.hexdigest()


def add_stub(project):
    target = project / "subprojects" / "libPorpoise"
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(FIXTURES / "libporpoise_stub", target)


with tempfile.TemporaryDirectory(prefix="porpoise-tests-", ignore_cleanup_errors=True) as temporary:
    temporary = Path(temporary)

    assert "Usage:" in run(TOOL, "--help").stdout
    assert "0.2.0" in run(TOOL, "--version").stdout
    run(TOOL, expected=2)
    run(TOOL, FIXTURES / "inputs" / "basic", expected=2)
    run(TOOL, FIXTURES / "inputs" / "basic", "extra", "--output", temporary / "extra-output", expected=2)
    run(
        TOOL,
        FIXTURES / "inputs" / "basic",
        "--output",
        temporary / "verbosity-output",
        "--quiet",
        "--verbose",
        expected=2,
    )
    run(TOOL, temporary / "missing.s", "--output", temporary / "missing-output", expected=4)
    run(
        TOOL,
        FIXTURES / "inputs" / "basic",
        "--config",
        temporary / "missing-config.json",
        "--output",
        temporary / "missing-config-output",
        expected=4,
    )
    run(
        TOOL,
        FIXTURES / "inputs" / "basic",
        "--abi",
        temporary / "missing-abi.json",
        "--output",
        temporary / "missing-abi-output",
        expected=4,
    )
    run(
        TOOL,
        FIXTURES / "inputs" / "basic",
        "--skip-list",
        temporary / "missing-skip.txt",
        "--output",
        temporary / "missing-skip-output",
        expected=4,
    )
    output_file = temporary / "output-file"
    output_file.write_text("not a directory", encoding="utf-8")
    run(TOOL, FIXTURES / "inputs" / "basic", "--output", output_file, expected=4)

    empty_input = temporary / "empty"
    empty_input.mkdir()
    run(TOOL, empty_input, "--output", temporary / "empty-output", expected=3)

    data_output = temporary / "data-output"
    run(TOOL, FIXTURES / "inputs" / "data", "--output", data_output)
    data_report = json.loads((data_output / "porpoise-report.json").read_text(encoding="utf-8"))
    assert data_report["summary"]["functions"] == 1
    assert data_report["summary"]["data_words"] == 2
    assert "0x80300000" in (data_output / "src" / "porpoise_data.c").read_text(encoding="utf-8")

    no_entry = temporary / "no-entry"
    run(TOOL, FIXTURES / "inputs" / "basic", "--output", no_entry)
    assert not (no_entry / "src" / "porpoise_entry.c").exists()
    report = json.loads((no_entry / "porpoise-report.json").read_text(encoding="utf-8"))
    assert report["summary"]["unsupported"] == 0
    assert report["summary"]["functions"] == 1
    assert not (no_entry / "subprojects").exists()
    assert (no_entry / "src" / "lifted" / "no_entry.c").read_bytes() == (
        ROOT / "tests" / "golden" / "basic" / "no_entry.c"
    ).read_bytes()
    assert (no_entry / "porpoise-report.json").read_bytes() == (
        ROOT / "tests" / "golden" / "basic" / "porpoise-report.json"
    ).read_bytes()
    assert (no_entry / "meson.build").read_bytes() == (
        ROOT / "tests" / "golden" / "basic" / "meson.build"
    ).read_bytes()
    add_stub(no_entry)
    assert "fallback: ['libPorpoise', 'libporpoise_dep']" in (no_entry / "meson.build").read_text(encoding="utf-8")
    assert "porpoise-title-host" not in (no_entry / "meson.build").read_text(encoding="utf-8")
    assert not any(no_entry.glob("subprojects/*.wrap"))
    run("meson", "setup", "build", "--wrap-mode=forcefallback", *CHILD_MESON_ARGS, cwd=no_entry)
    run("meson", "compile", "-C", "build", cwd=no_entry)

    opcodes = temporary / "opcodes"
    run(TOOL, FIXTURES / "inputs" / "opcodes", "--output", opcodes)
    opcode_report = json.loads((opcodes / "porpoise-report.json").read_text(encoding="utf-8"))
    semantic_mnemonics = {
        instruction["mnemonic"]
        for instruction in opcode_report["instructions"]
        if instruction["semantic_test"]
    }
    assert semantic_mnemonics == {
        "add", "addc", "adde", "addi", "addic.", "addze", "andc",
        "bctrl", "beq+", "beqlr", "bgelr", "bgtlr", "bl", "ble+", "blelr",
        "blrl", "bltlr", "bne", "bnelr", "clrlslwi", "clrlwi", "clrrwi",
        "cmpwi", "cntlzw", "crclr", "cror", "crset", "divw", "divw.",
        "divwu", "divwu.", "extlwi", "extrwi", "extsb", "extsh", "fabs.",
        "fadd", "fadds", "fcmpo", "fcmpu", "fctiwz", "fctiwz.", "fmadd",
        "fmadd.", "fmadds", "fmadds.", "fmr.", "fmsub", "fmsubs", "fnabs.",
        "fneg.", "fnmadd", "fnmadds", "fnmsub", "fnmsubs", "frsp", "frsp.",
        "fsel", "fsel.", "lbzx", "lfsx", "lhzx", "li", "lis", "lmw", "mffs",
        "mffs.",
        "lwz", "lwzux", "lwzx", "mtctr", "mulhw", "mulhwu", "mullw", "neg",
        "mtfsb1", "mtfsb1.", "mtfsf", "mtfsf.", "nor", "orc", "ori",
        "ps_add", "ps_cmpo0", "ps_div", "ps_madd",
        "ps_madds0", "ps_madds1", "ps_merge00", "ps_merge01", "ps_merge10",
        "ps_merge11", "ps_mr", "ps_msub", "ps_muls0", "ps_muls1",
        "ps_neg", "ps_nmadd", "ps_nmsub", "ps_sel", "ps_sum0", "ps_sum1",
        "psq_l", "psq_lu", "psq_lux", "psq_lx", "psq_st", "psq_stu",
        "psq_stux", "psq_stx",
        "rlwinm", "rlwinm.", "rlwnm", "rotlwi", "rotrwi", "slw", "slwi", "sraw.", "srawi", "srwi",
        "srwi.", "stbx", "stfsx", "sthbrx", "sthx", "stmw", "stw", "stwux",
        "stwx", "subfc", "subfe", "subfic", "subfze", "subfze.", "subi",
        "subic", "subic.", "subis", "sync",
    }
    assert opcode_report["summary"]["unsupported"] == 0
    conditional_return_detail = (
        "taken LR branch returns through the C call stack instead of dispatching "
        "the arbitrary guest LR target"
    )
    for mnemonic in {"beqlr", "bnelr", "bgelr", "blelr", "bgtlr", "bltlr"}:
        entries = [
            instruction
            for instruction in opcode_report["instructions"]
            if instruction["mnemonic"] == mnemonic
        ]
        assert entries and all(instruction["status"] == "approximate" for instruction in entries)
        assert all(instruction["detail"] == conditional_return_detail for instruction in entries)
        assert all(instruction["semantic_test"] for instruction in entries)
    for mnemonic in {
        "beq+", "ble+", "blrl", "divw", "divw.", "divwu", "divwu.",
        "fctiwz", "fctiwz.", "mffs", "mffs.", "mtfsb1", "mtfsb1.",
        "mtfsf", "mtfsf.", "mulhw", "mulhwu", "orc", "ps_cmpo0", "ps_merge00", "ps_merge01",
        "ps_merge10", "ps_merge11", "ps_mr", "ps_neg", "ps_sel", "rlwnm",
        "sthbrx", "subfze", "subfze.",
    }:
        entries = [
            instruction
            for instruction in opcode_report["instructions"]
            if instruction["mnemonic"] == mnemonic
        ]
        assert entries and all(instruction["status"] == "lowered" for instruction in entries)
        assert all(instruction["semantic_test"] for instruction in entries)
    for mnemonic in {"fadd", "fadds"}:
        entries = [
            instruction
            for instruction in opcode_report["instructions"]
            if instruction["mnemonic"] == mnemonic
        ]
        assert entries and all(instruction["status"] == "approximate" for instruction in entries)
        assert all(
            instruction["detail"] == (
                "host arithmetic does not reproduce all PPC floating-point rounding, "
                "exception, and status semantics"
            )
            for instruction in entries
        )
    scalar_frsp_detail = (
        "runtime duplicates lane 0 into architecturally undefined destination lane 1 "
        "for deterministic compatibility"
    )
    for mnemonic in {"frsp", "frsp."}:
        entries = [
            instruction
            for instruction in opcode_report["instructions"]
            if instruction["mnemonic"] == mnemonic
        ]
        assert entries and all(instruction["status"] == "approximate" for instruction in entries)
        assert all(instruction["detail"] == scalar_frsp_detail for instruction in entries)
        assert all(instruction["semantic_test"] for instruction in entries)
    scalar_fma_detail = (
        "finite arithmetic uses host C99 fma and does not reproduce all PPC rounding "
        "and exception semantics"
    )
    for mnemonic in {
        "fmadd", "fmadd.", "fmadds", "fmadds.", "fmsub", "fmsubs",
        "fnmadd", "fnmadds", "fnmsub", "fnmsubs",
    }:
        entries = [
            instruction
            for instruction in opcode_report["instructions"]
            if instruction["mnemonic"] == mnemonic
        ]
        assert entries and all(instruction["status"] == "approximate" for instruction in entries)
        assert all(instruction["detail"] == scalar_fma_detail for instruction in entries)
        assert all(instruction["semantic_test"] for instruction in entries)
    paired_arithmetic_detail = (
        "host arithmetic does not reproduce PPC paired-single rounding, "
        "exception, and FPSCR semantics"
    )
    for mnemonic in {"ps_add", "ps_div", "ps_sum0", "ps_sum1"}:
        entries = [
            instruction
            for instruction in opcode_report["instructions"]
            if instruction["mnemonic"] == mnemonic
        ]
        assert entries and all(instruction["status"] == "approximate" for instruction in entries)
        assert all(instruction["detail"] == paired_arithmetic_detail for instruction in entries)
        assert all(instruction["semantic_test"] for instruction in entries)
    paired_multiply_detail = (
        "host arithmetic does not reproduce PPC paired-single Force25, rounding, "
        "exception, and FPSCR semantics"
    )
    for mnemonic in {"ps_muls0", "ps_muls1"}:
        entries = [
            instruction
            for instruction in opcode_report["instructions"]
            if instruction["mnemonic"] == mnemonic
        ]
        assert entries and all(instruction["status"] == "approximate" for instruction in entries)
        assert all(instruction["detail"] == paired_multiply_detail for instruction in entries)
        assert all(instruction["semantic_test"] for instruction in entries)
    paired_fused_detail = (
        "host arithmetic does not reproduce PPC paired-single Force25, fused rounding, "
        "exception, and FPSCR semantics"
    )
    for mnemonic in {
        "ps_madd", "ps_madds0", "ps_madds1", "ps_msub", "ps_nmadd", "ps_nmsub",
    }:
        entries = [
            instruction
            for instruction in opcode_report["instructions"]
            if instruction["mnemonic"] == mnemonic
        ]
        assert entries and all(instruction["status"] == "approximate" for instruction in entries)
        assert all(instruction["detail"] == paired_fused_detail for instruction in entries)
        assert all(instruction["semantic_test"] for instruction in entries)
    psq_detail = (
        "runtime models GQR quantization deterministically but does not reproduce all "
        "Gekko NI behavior or FPSCR and floating-point exception side effects"
    )
    for mnemonic in {
        "psq_l", "psq_lu", "psq_lux", "psq_lx",
        "psq_st", "psq_stu", "psq_stux", "psq_stx",
    }:
        entries = [
            instruction
            for instruction in opcode_report["instructions"]
            if instruction["mnemonic"] == mnemonic
        ]
        assert entries and all(instruction["status"] == "approximate" for instruction in entries)
        assert all(instruction["detail"] == psq_detail for instruction in entries)
        assert all(instruction["semantic_test"] for instruction in entries)
    extended_lifted_source = (
        opcodes / "src" / "lifted" / "extended_semantics.c"
    ).read_text(encoding="utf-8")
    assert extended_lifted_source.count("porpoise_psq_load(") == 9
    assert extended_lifted_source.count("porpoise_psq_store(") == 8
    harness = opcodes / "tests" / "semantic_harness.c"
    harness.parent.mkdir(parents=True)
    harness.write_text(
        "#include <stdlib.h>\n"
        "#include \"porpoise_generated.h\"\n"
        "#include \"porpoise_libporpoise_adapter.h\"\n"
        "#include <porpoise/stub.h>\n"
        "#define CHECK(condition) do { if (!(condition)) abort(); } while (0)\n"
        "int main(void) {\n"
        "  PorpoiseHostAdapter host; PorpoisePpcState state;\n"
        "  CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);\n"
        "  porpoise_state_init(&state, &host);\n"
        "  porpoise_lifted_integer_semantics(&state);\n"
        "  CHECK(!porpoise_state_has_fault(&state));\n"
        "  CHECK(state.gpr[3] == 7U && state.gpr[5] == 3U);\n"
        "  CHECK(state.gpr[6] == 4U && state.gpr[8] == 2U && state.gpr[9] == 8U);\n"
        "  porpoise_fpr_set_f64(&state, 1U, 0U, 1.25); porpoise_fpr_set_f64(&state, 2U, 0U, 2.5);\n"
        "  porpoise_fpr_set_bits(&state, 3U, 1U, UINT64_C(0x0123456789ABCDEF));\n"
        "  porpoise_lifted_scalar_float_semantics(&state);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 3U, 0U) == 3.75);\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 3U, 1U) == UINT64_C(0x0123456789ABCDEF));\n"
        "  porpoise_fpr_set_f64(&state, 1U, 0U, 16777216.0); porpoise_fpr_set_f64(&state, 2U, 0U, 1.0);\n"
        "  porpoise_lifted_remaining_scalar_single_lane_semantics(&state);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 3U, 0U) == 16777216.0);\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 3U, 0U) == porpoise_fpr_get_bits(&state, 3U, 1U));\n"
        "  porpoise_fpr_set_f64(&state, 1U, 0U, 1.0); porpoise_fpr_set_f64(&state, 1U, 1U, 2.0);\n"
        "  porpoise_fpr_set_f64(&state, 2U, 0U, 3.0); porpoise_fpr_set_f64(&state, 2U, 1U, 4.0);\n"
        "  porpoise_lifted_paired_float_semantics(&state);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 3U, 0U) == 4.0 && porpoise_fpr_get_f64(&state, 3U, 1U) == 6.0);\n"
        "  porpoise_fpr_set_f64(&state, 1U, 0U, 2.0); porpoise_fpr_set_f64(&state, 1U, 1U, 3.0);\n"
        "  porpoise_fpr_set_f64(&state, 2U, 0U, 5.0); porpoise_fpr_set_f64(&state, 2U, 1U, 7.0);\n"
        "  porpoise_fpr_set_f64(&state, 3U, 0U, 11.0); porpoise_fpr_set_f64(&state, 3U, 1U, 13.0);\n"
        "  porpoise_fpr_set_f64(&state, 15U, 0U, 14.0); porpoise_fpr_set_f64(&state, 15U, 1U, 24.0);\n"
        "  porpoise_lifted_paired_advanced_arithmetic_semantics(&state);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 4U, 0U) == 21.0 && porpoise_fpr_get_f64(&state, 4U, 1U) == 34.0);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 5U, 0U) == -1.0 && porpoise_fpr_get_f64(&state, 5U, 1U) == 8.0);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 6U, 0U) == -21.0 && porpoise_fpr_get_f64(&state, 6U, 1U) == -34.0);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 7U, 0U) == 1.0 && porpoise_fpr_get_f64(&state, 7U, 1U) == -8.0);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 8U, 0U) == 21.0 && porpoise_fpr_get_f64(&state, 8U, 1U) == 28.0);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 9U, 0U) == 25.0 && porpoise_fpr_get_f64(&state, 9U, 1U) == 34.0);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 10U, 0U) == 10.0 && porpoise_fpr_get_f64(&state, 10U, 1U) == 15.0);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 11U, 0U) == 14.0 && porpoise_fpr_get_f64(&state, 11U, 1U) == 21.0);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 12U, 0U) == 15.0 && porpoise_fpr_get_f64(&state, 12U, 1U) == 7.0);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 13U, 0U) == 5.0 && porpoise_fpr_get_f64(&state, 13U, 1U) == 15.0);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 14U, 0U) == 7.0 && porpoise_fpr_get_f64(&state, 14U, 1U) == 8.0);\n"
        "  porpoise_fpr_set_bits(&state, 1U, 0U, UINT64_C(0x7FF800000000CAFE));\n"
        "  porpoise_fpr_set_f64(&state, 1U, 1U, 2.0); porpoise_fpr_set_f64(&state, 2U, 0U, 1.0); porpoise_fpr_set_f64(&state, 2U, 1U, 1.0);\n"
        "  porpoise_fpr_set_f64(&state, 3U, 0U, 0.0); porpoise_fpr_set_f64(&state, 3U, 1U, 0.0);\n"
        "  porpoise_lifted_paired_advanced_arithmetic_semantics(&state);\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 4U, 0U) == porpoise_fpr_get_bits(&state, 6U, 0U));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 5U, 0U) == porpoise_fpr_get_bits(&state, 7U, 0U));\n"
        "  porpoise_fpr_set_bits(&state, 1U, 0U, UINT64_C(0x0123456789ABCDEF));\n"
        "  porpoise_fpr_set_bits(&state, 1U, 1U, UINT64_C(0xFEDCBA9876543210));\n"
        "  porpoise_fpr_set_bits(&state, 2U, 0U, UINT64_C(0x1111111111111111));\n"
        "  porpoise_fpr_set_bits(&state, 2U, 1U, UINT64_C(0x2222222222222222));\n"
        "  porpoise_fpr_set_f64(&state, 3U, 0U, 1.0); porpoise_fpr_set_bits(&state, 3U, 1U, UINT64_C(0x7FF8000000001234));\n"
        "  porpoise_fpr_set_bits(&state, 11U, 0U, UINT64_C(0x7FF0000012345678)); porpoise_fpr_set_bits(&state, 11U, 1U, UINT64_C(0x3333333333333333));\n"
        "  porpoise_fpr_set_bits(&state, 12U, 0U, UINT64_C(0xFFF80000ABCDEF01)); porpoise_fpr_set_bits(&state, 12U, 1U, UINT64_C(0x4444444444444444));\n"
        "  porpoise_fpr_set_f64(&state, 13U, 0U, -2.0); porpoise_fpr_set_f64(&state, 14U, 0U, 1.0);\n"
        "  porpoise_lifted_paired_exact_data_semantics(&state);\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 4U, 0U) == UINT64_C(0x0123456789ABCDEF) && porpoise_fpr_get_bits(&state, 4U, 1U) == UINT64_C(0x1111111111111111));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 5U, 0U) == UINT64_C(0x0123456789ABCDEF) && porpoise_fpr_get_bits(&state, 5U, 1U) == UINT64_C(0x2222222222222222));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 6U, 0U) == UINT64_C(0xFEDCBA9876543210) && porpoise_fpr_get_bits(&state, 6U, 1U) == UINT64_C(0x1111111111111111));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 7U, 0U) == UINT64_C(0xFEDCBA9876543210) && porpoise_fpr_get_bits(&state, 7U, 1U) == UINT64_C(0x2222222222222222));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 8U, 0U) == UINT64_C(0x0123456789ABCDEF) && porpoise_fpr_get_bits(&state, 8U, 1U) == UINT64_C(0xFEDCBA9876543210));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 9U, 0U) == UINT64_C(0x8123456789ABCDEF) && porpoise_fpr_get_bits(&state, 9U, 1U) == UINT64_C(0x7EDCBA9876543210));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 10U, 0U) == UINT64_C(0x7FF0000012345678) && porpoise_fpr_get_bits(&state, 10U, 1U) == UINT64_C(0x4444444444444444));\n"
        "  CHECK(porpoise_cr_get_field(&state, 6U) == PORPOISE_FPCC_LESS);\n"
        "  porpoise_fpr_set_bits(&state, 3U, 0U, UINT64_C(0x8000000000000000)); porpoise_fpr_set_f64(&state, 3U, 1U, -1.0);\n"
        "  porpoise_lifted_paired_exact_data_semantics(&state);\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 10U, 0U) == UINT64_C(0x7FF0000012345678) && porpoise_fpr_get_bits(&state, 10U, 1U) == UINT64_C(0x4444444444444444));\n"
        "  state.fpscr = 0U; porpoise_fpr_set_bits(&state, 1U, 0U, UINT64_C(0x3FF0000010000000));\n"
        "  porpoise_fpr_set_bits(&state, 2U, 1U, UINT64_C(0x1111111111111111)); porpoise_fpr_set_bits(&state, 3U, 1U, UINT64_C(0x2222222222222222));\n"
        "  porpoise_lifted_scalar_frsp_semantics(&state);\n"
        "  CHECK(!porpoise_state_has_fault(&state));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 2U, 0U) == UINT64_C(0x3FF0000000000000) && porpoise_fpr_get_bits(&state, 2U, 1U) == UINT64_C(0x3FF0000000000000));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 3U, 0U) == UINT64_C(0x3FF0000000000000) && porpoise_fpr_get_bits(&state, 3U, 1U) == UINT64_C(0x3FF0000000000000));\n"
        "  CHECK(porpoise_cr_get_field(&state, 1U) == (uint8_t)((state.fpscr >> 28U) & 0xFU));\n"
        "  state.fpscr = 0U; porpoise_fpr_set_f64(&state, 1U, 0U, 1.75);\n"
        "  porpoise_fpr_set_bits(&state, 2U, 1U, UINT64_C(0x1122334455667788)); porpoise_fpr_set_bits(&state, 3U, 1U, UINT64_C(0x8877665544332211));\n"
        "  porpoise_lifted_scalar_fctiwz_semantics(&state);\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 2U, 0U) == UINT64_C(0xFFF8000000000001) && porpoise_fpr_get_bits(&state, 2U, 1U) == UINT64_C(0x1122334455667788));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 3U, 0U) == UINT64_C(0xFFF8000000000001) && porpoise_fpr_get_bits(&state, 3U, 1U) == UINT64_C(0x8877665544332211));\n"
        "  CHECK(porpoise_cr_get_field(&state, 1U) == (uint8_t)((state.fpscr >> 28U) & 0xFU));\n"
        "  state.fpscr = PORPOISE_FPSCR_FX | PORPOISE_FPSCR_OX | PORPOISE_FPSCR_NI | 1U;\n"
        "  porpoise_fpr_set_bits(&state, 2U, 1U, UINT64_C(0x13579BDF2468ACE0)); porpoise_fpr_set_bits(&state, 3U, 1U, UINT64_C(0x02468ACE13579BDF));\n"
        "  porpoise_lifted_scalar_mffs_semantics(&state);\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 2U, 0U) == (UINT64_C(0xFFF8000000000000) | state.fpscr) && porpoise_fpr_get_bits(&state, 2U, 1U) == UINT64_C(0x13579BDF2468ACE0));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 3U, 0U) == (UINT64_C(0xFFF8000000000000) | state.fpscr) && porpoise_fpr_get_bits(&state, 3U, 1U) == UINT64_C(0x02468ACE13579BDF));\n"
        "  CHECK(porpoise_cr_get_field(&state, 1U) == (uint8_t)((state.fpscr >> 28U) & 0xFU));\n"
        "  state.fpscr = 0U; porpoise_fpr_set_bits(&state, 1U, 0U, UINT64_C(0x7FF8000010000000)); porpoise_fpr_set_bits(&state, 2U, 0U, UINT64_C(0x7FF0000000000005));\n"
        "  porpoise_lifted_scalar_mtfsf_semantics(&state);\n"
        "  CHECK((state.fpscr & PORPOISE_FPSCR_OX) != 0U && (state.fpscr & UINT32_C(0xF)) == UINT32_C(0x5));\n"
        "  CHECK(porpoise_cr_get_field(&state, 1U) == (uint8_t)((state.fpscr >> 28U) & 0xFU));\n"
        "  state.fpscr = 0U; porpoise_lifted_scalar_mtfsb1_semantics(&state);\n"
        "  CHECK((state.fpscr & (PORPOISE_FPSCR_NI | PORPOISE_FPSCR_OX | PORPOISE_FPSCR_FX)) == (PORPOISE_FPSCR_NI | PORPOISE_FPSCR_OX | PORPOISE_FPSCR_FX));\n"
        "  CHECK(porpoise_cr_get_field(&state, 1U) == (uint8_t)((state.fpscr >> 28U) & 0xFU));\n"
        "  state.fpscr = 0U; porpoise_fpr_set_f64(&state, 1U, 0U, 2.0); porpoise_fpr_set_f64(&state, 2U, 0U, 3.0); porpoise_fpr_set_f64(&state, 3U, 0U, 4.0);\n"
        "  porpoise_fpr_set_bits(&state, 4U, 1U, UINT64_C(0x1111111111111111)); porpoise_fpr_set_bits(&state, 5U, 1U, UINT64_C(0x2222222222222222)); porpoise_fpr_set_bits(&state, 6U, 1U, UINT64_C(0x3333333333333333)); porpoise_fpr_set_bits(&state, 7U, 1U, UINT64_C(0x4444444444444444)); porpoise_fpr_set_bits(&state, 8U, 1U, UINT64_C(0x5555555555555555));\n"
        "  porpoise_lifted_scalar_fma_double_semantics(&state);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 4U, 0U) == 10.0 && porpoise_fpr_get_f64(&state, 5U, 0U) == 2.0);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 6U, 0U) == -10.0 && porpoise_fpr_get_f64(&state, 7U, 0U) == -2.0 && porpoise_fpr_get_f64(&state, 8U, 0U) == 10.0);\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 4U, 1U) == UINT64_C(0x1111111111111111) && porpoise_fpr_get_bits(&state, 8U, 1U) == UINT64_C(0x5555555555555555));\n"
        "  CHECK(porpoise_cr_get_field(&state, 1U) == (uint8_t)((state.fpscr >> 28U) & 0xFU));\n"
        "  state.fpscr = 0U; porpoise_lifted_scalar_fma_single_semantics(&state);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 4U, 0U) == 10.0 && porpoise_fpr_get_f64(&state, 5U, 0U) == 2.0);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 6U, 0U) == -10.0 && porpoise_fpr_get_f64(&state, 7U, 0U) == -2.0 && porpoise_fpr_get_f64(&state, 8U, 0U) == 10.0);\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 4U, 0U) == porpoise_fpr_get_bits(&state, 4U, 1U) && porpoise_fpr_get_bits(&state, 8U, 0U) == porpoise_fpr_get_bits(&state, 8U, 1U));\n"
        "  CHECK(porpoise_cr_get_field(&state, 1U) == (uint8_t)((state.fpscr >> 28U) & 0xFU));\n"
        "  state.fpscr = 0U; porpoise_fpr_set_bits(&state, 1U, 0U, UINT64_C(0xFFF80000000000A1)); porpoise_fpr_set_bits(&state, 2U, 0U, UINT64_C(0x7FF80000000000C3)); porpoise_fpr_set_bits(&state, 3U, 0U, UINT64_C(0x7FF80000000000B2));\n"
        "  porpoise_lifted_scalar_fma_double_semantics(&state);\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 4U, 0U) == UINT64_C(0xFFF80000000000A1) && porpoise_fpr_get_bits(&state, 6U, 0U) == UINT64_C(0xFFF80000000000A1));\n"
        "  porpoise_lifted_direct_branch_semantics(&state);\n"
        "  CHECK(state.gpr[3] == 5U);\n"
        "  porpoise_lifted_indirect_branch_semantics(&state);\n"
        "  CHECK(state.gpr[3] == 11U);\n"
        "  state.gpr[3] = 0U;\n"
        "  porpoise_lifted_quoted_direct_branch_semantics(&state);\n"
        "  CHECK(state.gpr[3] == 42U);\n"
        "  state.gpr[3] = 0U;\n"
        "  porpoise_lifted_quoted_conditional_branch_semantics(&state);\n"
        "  CHECK(state.gpr[3] == 42U);\n"
        "  state.gpr[3] = 0U; porpoise_lifted_alias_mid_owner(&state);\n"
        "  CHECK(state.gpr[3] == 5U);\n"
        "  state.gpr[3] = 10U; CHECK(porpoise_call_address(&state, UINT32_C(0x80006B04)));\n"
        "  CHECK(!porpoise_state_has_fault(&state) && state.gpr[3] == 14U);\n"
        "  porpoise_lifted_alias_branch_caller(&state); CHECK(state.gpr[3] == 25U);\n"
        "  state.gpr[3] = 0U; CHECK(porpoise_call_address(&state, UINT32_C(0x80006B20)));\n"
        "  CHECK(!porpoise_state_has_fault(&state) && state.gpr[3] == 7U);\n"
        "  state.gpr[3] = 0U; porpoise_lifted_cross_label_owner(&state);\n"
        "  CHECK(state.gpr[3] == 105U);\n"
        "  state.gpr[3] = 40U; CHECK(porpoise_call_address(&state, UINT32_C(0x80006BA4)));\n"
        "  CHECK(!porpoise_state_has_fault(&state) && state.gpr[3] == 45U);\n"
        "  porpoise_lifted_cross_label_linked_caller(&state); CHECK(state.gpr[3] == 16U);\n"
        "  porpoise_lifted_cross_label_tail_caller(&state); CHECK(state.gpr[3] == 25U);\n"
        "  porpoise_lifted_cross_label_conditional_caller(&state); CHECK(state.gpr[3] == 35U);\n"
        "  state.fpscr = 0U;\n"
        "  porpoise_fpr_set_f64(&state, 1U, 0U, -2.0);\n"
        "  porpoise_fpr_set_f64(&state, 2U, 0U, 1.0);\n"
        "  porpoise_fpr_set_bits(&state, 3U, 0U, UINT64_C(0x7FF8000000001234));\n"
        "  porpoise_lifted_scalar_compare_semantics(&state);\n"
        "  CHECK(!porpoise_state_has_fault(&state));\n"
        "  CHECK(porpoise_cr_get_field(&state, 2U) == PORPOISE_FPCC_LESS);\n"
        "  CHECK(porpoise_cr_get_field(&state, 3U) == PORPOISE_FPCC_UNORDERED);\n"
        "  CHECK((state.fpscr & PORPOISE_FPSCR_FPCC_MASK) == (PORPOISE_FPCC_UNORDERED << 12U));\n"
        "  CHECK((state.fpscr & (PORPOISE_FPSCR_FX | PORPOISE_FPSCR_VX | PORPOISE_FPSCR_VXVC)) == (PORPOISE_FPSCR_FX | PORPOISE_FPSCR_VX | PORPOISE_FPSCR_VXVC));\n"
        "  state.fpscr = PORPOISE_FPSCR_FX | PORPOISE_FPSCR_FEX | PORPOISE_FPSCR_VX | PORPOISE_FPSCR_OX;\n"
        "  porpoise_fpr_set_bits(&state, 4U, 0U, UINT64_C(0x8000000000000000));\n"
        "  porpoise_fpr_set_bits(&state, 5U, 0U, UINT64_C(0x7FF8000012345678));\n"
        "  porpoise_fpr_set_bits(&state, 6U, 0U, UINT64_C(0xFFF8000087654321));\n"
        "  porpoise_fpr_set_bits(&state, 9U, 0U, UINT64_C(0x7FF0000000000001));\n"
        "  porpoise_fpr_set_bits(&state, 7U, 1U, UINT64_C(0x1111111111111111));\n"
        "  porpoise_fpr_set_bits(&state, 8U, 1U, UINT64_C(0x2222222222222222));\n"
        "  porpoise_lifted_scalar_select_semantics(&state);\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 7U, 0U) == UINT64_C(0x7FF8000012345678));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 8U, 0U) == UINT64_C(0xFFF8000087654321));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 7U, 1U) == UINT64_C(0x1111111111111111));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 8U, 1U) == UINT64_C(0x2222222222222222));\n"
        "  CHECK(porpoise_cr_get_field(&state, 1U) == 0xFU);\n"
        "  porpoise_fpr_set_bits(&state, 10U, 0U, UINT64_C(0xFFF0000000000123));\n"
        "  porpoise_fpr_set_bits(&state, 11U, 1U, UINT64_C(0x3333333333333333));\n"
        "  porpoise_lifted_scalar_unary_record_semantics(&state);\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 11U, 0U) == UINT64_C(0xFFF0000000000123));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 12U, 0U) == UINT64_C(0x7FF0000000000123));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 13U, 0U) == UINT64_C(0x7FF0000000000123));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 14U, 0U) == UINT64_C(0xFFF0000000000123));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 11U, 1U) == UINT64_C(0x3333333333333333));\n"
        "  CHECK(porpoise_cr_get_field(&state, 1U) == 0xFU);\n"
        "  state.fpscr = 0U;\n"
        "  porpoise_lifted_extended_integer_semantics(&state);\n"
        "  CHECK(state.gpr[4] == 0xFFU && state.gpr[5] == 0xFF000000U);\n"
        "  CHECK(state.gpr[6] == 0xFF0U && state.gpr[7] == 0xFU);\n"
        "  CHECK(state.gpr[8] == 0xFFU && state.gpr[9] == 0xFF000000U);\n"
        "  CHECK(state.gpr[10] == 0xFFFFFFFFU && state.gpr[11] == 0xFFFF8001U);\n"
        "  CHECK(state.gpr[12] == 24U && state.gpr[13] == 0xFFFFFF01U);\n"
        "  CHECK(state.gpr[14] == 0xFE01U && state.gpr[15] == 0xFF000000U);\n"
        "  CHECK(state.gpr[16] == 0xFFFFFF00U && state.gpr[17] == 0xFEU);\n"
        "  CHECK(state.gpr[18] == 0x10000U && state.gpr[19] == 0x00FFFF00U);\n"
        "  CHECK(state.gpr[20] == 0xFF00U && state.gpr[21] == 0xFF000000U);\n"
        "  CHECK(state.gpr[22] == 0x12345678U && state.gpr[23] == 0x81000000U);\n"
        "  state.gpr[3] = 0xFFFFFFEBU; state.gpr[4] = 4U; state.gpr[7] = 21U; state.gpr[8] = 4U;\n"
        "  state.gpr[10] = 0xFFFFFFFEU; state.gpr[11] = 3U; state.gpr[14] = 0x12345678U; state.gpr[15] = 8U;\n"
        "  state.gpr[17] = 0x0FU; state.gpr[18] = 0xF0U;\n"
        "  porpoise_lifted_remaining_integer_semantics(&state);\n"
        "  CHECK(state.gpr[5] == 0xFFFFFFFBU && state.gpr[6] == 5U);\n"
        "  CHECK(state.gpr[9] == 0xFFFFFFFFU && state.gpr[12] == 2U);\n"
        "  CHECK(state.gpr[13] == 0x00567800U && state.gpr[16] == 0xFFFFFF0FU);\n"
        "  state.gpr[3] = 0xFFFFFFFFU; state.gpr[4] = 0U; state.gpr[7] = 123U; state.gpr[8] = 0U;\n"
        "  state.gpr[10] = 0x80000000U; state.gpr[11] = 0xFFFFFFFFU; state.xer = 0x80000000U;\n"
        "  porpoise_lifted_remaining_divide_exception_semantics(&state);\n"
        "  CHECK(state.gpr[5] == 0xFFFFFFFFU && state.gpr[6] == 0U && state.gpr[9] == 0xFFFFFFFFU);\n"
        "  CHECK(porpoise_cr_get_field(&state, 0U) == 9U);\n"
        "  state.gpr[3] = 8U; state.gpr[4] = 2U; state.xer = 0U;\n"
        "  porpoise_lifted_remaining_divwu_record_semantics(&state);\n"
        "  CHECK(state.gpr[5] == 4U && porpoise_cr_get_field(&state, 0U) == 4U);\n"
        "  state.gpr[3] = 0U; state.gpr[4] = 0U; state.xer = 0x20000000U;\n"
        "  porpoise_lifted_remaining_subfze_semantics(&state);\n"
        "  CHECK(state.gpr[5] == 0U && state.gpr[6] == 0U && (state.xer & 0x20000000U) != 0U);\n"
        "  CHECK(porpoise_cr_get_field(&state, 0U) == 2U);\n"
        "  state.gpr[3] = 0U; state.gpr[4] = 0U; state.xer = 0U;\n"
        "  porpoise_lifted_remaining_subfze_semantics(&state);\n"
        "  CHECK(state.gpr[5] == 0xFFFFFFFFU && state.gpr[6] == 0xFFFFFFFFU && (state.xer & 0x20000000U) == 0U);\n"
        "  CHECK(porpoise_cr_get_field(&state, 0U) == 8U);\n"
        "  state.gpr[3] = 0x80000500U; state.gpr[4] = 2U; state.gpr[5] = 0x1234U;\n"
        "  porpoise_lifted_remaining_sthbrx_semantics(&state);\n"
        "  CHECK(!porpoise_state_has_fault(&state) && porpoise_load_u16(&state, 0x80000502U) == 0x3412U);\n"
        "  porpoise_lifted_remaining_branch_hint_semantics(&state);\n"
        "  CHECK(state.gpr[4] == 0U);\n"
        "  state.gpr[3] = 0xA1U; porpoise_cr_set_field(&state, 0U, 2U);\n"
        "  porpoise_lifted_remaining_beqlr_semantics(&state); CHECK(state.gpr[3] == 0xA1U);\n"
        "  state.gpr[3] = 0xA2U; porpoise_cr_set_field(&state, 1U, 0U);\n"
        "  porpoise_lifted_remaining_bnelr_semantics(&state); CHECK(state.gpr[3] == 0xA2U);\n"
        "  state.gpr[3] = 0xA3U; porpoise_cr_set_field(&state, 2U, 4U);\n"
        "  porpoise_lifted_remaining_bgelr_semantics(&state); CHECK(state.gpr[3] == 0xA3U);\n"
        "  state.gpr[3] = 0xA4U; porpoise_cr_set_field(&state, 3U, 2U);\n"
        "  porpoise_lifted_remaining_blelr_semantics(&state); CHECK(state.gpr[3] == 0xA4U);\n"
        "  state.gpr[3] = 0xA5U; porpoise_cr_set_field(&state, 4U, 4U);\n"
        "  porpoise_lifted_remaining_bgtlr_semantics(&state); CHECK(state.gpr[3] == 0xA5U);\n"
        "  state.gpr[3] = 0xA6U; porpoise_cr_set_field(&state, 5U, 8U);\n"
        "  porpoise_lifted_remaining_bltlr_semantics(&state); CHECK(state.gpr[3] == 0xA6U);\n"
        "  state.gpr[3] = 0U; porpoise_lifted_remaining_blrl_semantics(&state);\n"
        "  CHECK(!porpoise_state_has_fault(&state) && state.gpr[3] == 77U);\n"
        "  porpoise_lifted_extended_carry_semantics(&state);\n"
        "  CHECK(state.gpr[5] == 0U && state.gpr[6] == 5U && state.gpr[7] == 4U);\n"
        "  CHECK(state.gpr[8] == 0xFFFFFFFFU && state.gpr[9] == 4U);\n"
        "  CHECK(state.gpr[10] == 6U && state.gpr[12] == 6U);\n"
        "  CHECK(state.gpr[13] == 0xFFFFFFFBU && state.gpr[14] == 0xFFFFFFFBU);\n"
        "  CHECK(state.gpr[15] == 0U && (state.xer & 0x20000000U) != 0U);\n"
        "  CHECK(porpoise_cr_get_field(&state, 0U) == 8U);\n"
        "  state.gpr[3] = 0xFFFFFFFFU; state.xer = 0x80000000U;\n"
        "  porpoise_lifted_extended_addic_record_semantics(&state);\n"
        "  CHECK(state.gpr[5] == 0U && (state.xer & 0x20000000U) != 0U);\n"
        "  CHECK(porpoise_cr_get_field(&state, 0U) == 3U);\n"
        "  state.gpr[3] = 0U; state.xer = 0x20000000U;\n"
        "  porpoise_lifted_extended_subic_carry_semantics(&state);\n"
        "  CHECK(state.gpr[5] == 0xFFFFFFFFU && (state.xer & 0x20000000U) == 0U);\n"
        "  state.gpr[3] = 6U; state.gpr[4] = 0U; state.xer = 0x20000000U;\n"
        "  porpoise_lifted_extended_subfc_carry_semantics(&state);\n"
        "  CHECK(state.gpr[5] == 0xFFFFFFFAU && (state.xer & 0x20000000U) == 0U);\n"
        "  state.gpr[3] = 0xFFFFFFFFU; state.gpr[4] = 0U; state.xer = 0x20000000U;\n"
        "  porpoise_lifted_extended_adde_carry_semantics(&state);\n"
        "  CHECK(state.gpr[5] == 0U && (state.xer & 0x20000000U) != 0U);\n"
        "  state.gpr[3] = 0xFFFFFFFFU; state.xer = 0x20000000U;\n"
        "  porpoise_lifted_extended_addze_carry_semantics(&state);\n"
        "  CHECK(state.gpr[5] == 0U && (state.xer & 0x20000000U) != 0U);\n"
        "  state.gpr[3] = 0U; state.gpr[4] = 0U; state.xer = 0U;\n"
        "  porpoise_lifted_extended_subfe_carry_semantics(&state);\n"
        "  CHECK(state.gpr[5] == 0xFFFFFFFFU && (state.xer & 0x20000000U) == 0U);\n"
        "  state.gpr[3] = 0x80000100U; state.gpr[4] = 4U; state.gpr[5] = 0x12345678U;\n"
        "  state.gpr[7] = 8U; state.gpr[9] = 10U; state.gpr[11] = 12U; state.gpr[12] = 0x40U;\n"
        "  state.gpr[28] = 0x11111111U; state.gpr[29] = 0x22222222U;\n"
        "  state.gpr[30] = 0x33333333U; state.gpr[31] = 0x44444444U;\n"
        "  porpoise_fpr_set_f64(&state, 1U, 0U, 1.000000000931322574615478515625);\n"
        "  porpoise_store_u32(&state, 0x80000110U, 0xA5A5A5A5U);\n"
        "  porpoise_lifted_extended_memory_semantics(&state);\n"
        "  CHECK(!porpoise_state_has_fault(&state));\n"
        "  CHECK(state.gpr[6] == 0x12345678U && state.gpr[8] == 0x78U);\n"
        "  CHECK(state.gpr[10] == 0x5678U && porpoise_fpr_get_f64(&state, 2U, 0U) == 1.0);\n"
        "  CHECK(porpoise_load_u32(&state, 0x80000110U) == 0xA5A5A5A5U);\n"
        "  CHECK(state.gpr[28] == 0x11111111U && state.gpr[31] == 0x44444444U);\n"
        "  CHECK(state.gpr[20] == 0x80000140U && state.gpr[21] == 0x80000140U);\n"
        "  CHECK(state.gpr[22] == 0x12345678U);\n"
        "  state.gpr[3] = 0x80000600U; state.gpr[4] = 24U; state.gpr[5] = 28U; state.gpr[6] = 32U; state.gpr[7] = 40U;\n"
        "  porpoise_store_u32(&state, 0x80000600U, 0x7FC12345U);\n"
        "  porpoise_store_u64(&state, 0x80000608U, UINT64_C(0x7FF8000000001234));\n"
        "  porpoise_store_u32(&state, 0x80000618U, 0xFFC54321U);\n"
        "  porpoise_store_u64(&state, 0x80000620U, UINT64_C(0xFFF0000000000001));\n"
        "  porpoise_fpr_set_bits(&state, 2U, 1U, UINT64_C(0x2222222222222222));\n"
        "  porpoise_fpr_set_bits(&state, 4U, 1U, UINT64_C(0x4444444444444444));\n"
        "  porpoise_lifted_remaining_raw_float_memory_semantics(&state);\n"
        "  CHECK(!porpoise_state_has_fault(&state));\n"
        "  CHECK(porpoise_load_u32(&state, 0x80000604U) == 0x7FC12345U);\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 1U, 0U) == porpoise_fpr_get_bits(&state, 1U, 1U));\n"
        "  CHECK(porpoise_load_u64(&state, 0x80000610U) == UINT64_C(0x7FF8000000001234));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 2U, 0U) == UINT64_C(0x7FF8000000001234));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 2U, 1U) == UINT64_C(0x2222222222222222));\n"
        "  CHECK(porpoise_load_u32(&state, 0x8000061CU) == 0xFFC54321U);\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 3U, 0U) == porpoise_fpr_get_bits(&state, 3U, 1U));\n"
        "  CHECK(porpoise_load_u64(&state, 0x80000628U) == UINT64_C(0xFFF0000000000001));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 4U, 1U) == UINT64_C(0x4444444444444444));\n"
        "  state.hid2 = PORPOISE_HID2_PSE | PORPOISE_HID2_LSQE; state.msr |= PORPOISE_MSR_FP;\n"
        "  state.gqr[1] = UINT32_C(0x00040004); state.gqr[2] = UINT32_C(0x00050005);\n"
        "  state.gpr[3] = UINT32_C(0x80001800); state.gpr[4] = UINT32_C(0x80000811);\n"
        "  porpoise_store_u8(&state, UINT32_C(0x80001000), 2U); porpoise_store_u8(&state, UINT32_C(0x80001001), 3U); porpoise_store_u16(&state, UINT32_C(0x80001010), 4U);\n"
        "  CHECK(!porpoise_state_has_fault(&state)); porpoise_lifted_psq_d_load_semantics(&state);\n"
        "  CHECK(!porpoise_state_has_fault(&state));\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 20U, 0U) == 2.0 && porpoise_fpr_get_f64(&state, 20U, 1U) == 3.0);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 21U, 0U) == 4.0 && porpoise_fpr_get_f64(&state, 21U, 1U) == 1.0);\n"
        "  state.gpr[5] = UINT32_C(0x80001820); state.gpr[6] = UINT32_C(0x80000831);\n"
        "  porpoise_store_u8(&state, UINT32_C(0x80001020), 5U); porpoise_store_u8(&state, UINT32_C(0x80001021), 6U); porpoise_store_u16(&state, UINT32_C(0x80001030), 7U);\n"
        "  porpoise_lifted_psq_d_load_update_semantics(&state); CHECK(!porpoise_state_has_fault(&state));\n"
        "  CHECK(state.gpr[5] == UINT32_C(0x80001020) && state.gpr[6] == UINT32_C(0x80001030));\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 22U, 0U) == 5.0 && porpoise_fpr_get_f64(&state, 22U, 1U) == 6.0);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 23U, 0U) == 7.0 && porpoise_fpr_get_f64(&state, 23U, 1U) == 1.0);\n"
        "  state.gpr[7] = UINT32_C(0x80001840); state.gpr[8] = UINT32_C(0x80000851);\n"
        "  porpoise_fpr_set_f64(&state, 24U, 0U, 8.0); porpoise_fpr_set_f64(&state, 24U, 1U, 9.0); porpoise_fpr_set_f64(&state, 25U, 0U, 10.0);\n"
        "  porpoise_lifted_psq_d_store_semantics(&state); CHECK(!porpoise_state_has_fault(&state));\n"
        "  CHECK(porpoise_load_u8(&state, UINT32_C(0x80001040)) == 8U && porpoise_load_u8(&state, UINT32_C(0x80001041)) == 9U);\n"
        "  CHECK(porpoise_load_u16(&state, UINT32_C(0x80001050)) == 10U);\n"
        "  state.gpr[9] = UINT32_C(0x80001860); state.gpr[10] = UINT32_C(0x80000871);\n"
        "  porpoise_fpr_set_f64(&state, 26U, 0U, 11.0); porpoise_fpr_set_f64(&state, 26U, 1U, 12.0); porpoise_fpr_set_f64(&state, 27U, 0U, 13.0);\n"
        "  porpoise_lifted_psq_d_store_update_semantics(&state); CHECK(!porpoise_state_has_fault(&state));\n"
        "  CHECK(state.gpr[9] == UINT32_C(0x80001060) && state.gpr[10] == UINT32_C(0x80001070));\n"
        "  CHECK(porpoise_load_u8(&state, UINT32_C(0x80001060)) == 11U && porpoise_load_u8(&state, UINT32_C(0x80001061)) == 12U);\n"
        "  CHECK(porpoise_load_u16(&state, UINT32_C(0x80001070)) == 13U);\n"
        "  state.gpr[0] = UINT32_C(0xDEADBEEF); state.gpr[11] = UINT32_C(0x80001080); state.gpr[12] = UINT32_C(0x80001000); state.gpr[13] = 0x90U;\n"
        "  porpoise_store_u8(&state, UINT32_C(0x80001080), 14U); porpoise_store_u8(&state, UINT32_C(0x80001081), 15U); porpoise_store_u16(&state, UINT32_C(0x80001090), 16U);\n"
        "  porpoise_lifted_psq_indexed_load_semantics(&state); CHECK(!porpoise_state_has_fault(&state));\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 28U, 0U) == 14.0 && porpoise_fpr_get_f64(&state, 28U, 1U) == 15.0);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 29U, 0U) == 16.0 && porpoise_fpr_get_f64(&state, 29U, 1U) == 1.0);\n"
        "  state.gpr[14] = UINT32_C(0x800010A0); state.gpr[15] = UINT32_C(0x80001000); state.gpr[16] = 0xB0U;\n"
        "  porpoise_fpr_set_f64(&state, 30U, 0U, 17.0); porpoise_fpr_set_f64(&state, 30U, 1U, 18.0); porpoise_fpr_set_f64(&state, 31U, 0U, 19.0);\n"
        "  porpoise_lifted_psq_indexed_store_semantics(&state); CHECK(!porpoise_state_has_fault(&state));\n"
        "  CHECK(porpoise_load_u8(&state, UINT32_C(0x800010A0)) == 17U && porpoise_load_u8(&state, UINT32_C(0x800010A1)) == 18U);\n"
        "  CHECK(porpoise_load_u16(&state, UINT32_C(0x800010B0)) == 19U);\n"
        "  state.gpr[18] = UINT32_C(0x80001000); state.gpr[19] = 0xD0U; state.gpr[20] = UINT32_C(0x80001000); state.gpr[21] = 0xE0U;\n"
        "  porpoise_store_u8(&state, UINT32_C(0x800010D0), 21U); porpoise_store_u8(&state, UINT32_C(0x800010D1), 22U); porpoise_store_u16(&state, UINT32_C(0x800010E0), 23U);\n"
        "  porpoise_lifted_psq_indexed_load_update_semantics(&state); CHECK(!porpoise_state_has_fault(&state));\n"
        "  CHECK(state.gpr[18] == UINT32_C(0x800010D0) && state.gpr[20] == UINT32_C(0x800010E0));\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 18U, 0U) == 21.0 && porpoise_fpr_get_f64(&state, 18U, 1U) == 22.0);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 17U, 0U) == 23.0 && porpoise_fpr_get_f64(&state, 17U, 1U) == 1.0);\n"
        "  state.gpr[22] = UINT32_C(0x80001000); state.gpr[23] = 0xF0U; state.gpr[24] = UINT32_C(0x80001000); state.gpr[25] = 0x100U;\n"
        "  porpoise_fpr_set_f64(&state, 16U, 0U, 24.0); porpoise_fpr_set_f64(&state, 16U, 1U, 25.0); porpoise_fpr_set_f64(&state, 15U, 0U, 26.0);\n"
        "  porpoise_lifted_psq_indexed_store_update_semantics(&state); CHECK(!porpoise_state_has_fault(&state));\n"
        "  CHECK(state.gpr[22] == UINT32_C(0x800010F0) && state.gpr[24] == UINT32_C(0x80001100));\n"
        "  CHECK(porpoise_load_u8(&state, UINT32_C(0x800010F0)) == 24U && porpoise_load_u8(&state, UINT32_C(0x800010F1)) == 25U);\n"
        "  CHECK(porpoise_load_u16(&state, UINT32_C(0x80001100)) == 26U);\n"
        "  state.gpr[17] = UINT32_C(0x800010C0); porpoise_store_u8(&state, UINT32_C(0x800010C0), 20U);\n"
        "  porpoise_lifted_psq_empty_displacement_semantics(&state); CHECK(!porpoise_state_has_fault(&state));\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 19U, 0U) == 20.0 && porpoise_fpr_get_f64(&state, 19U, 1U) == 1.0);\n"
        "  state.gpr[5] = UINT32_C(0x70000800); state.gpr[6] = UINT32_C(0xA5A5A5A5); porpoise_fpr_set_bits(&state, 22U, 0U, UINT64_C(0x1122334455667788));\n"
        "  porpoise_lifted_psq_d_load_update_semantics(&state); CHECK(porpoise_state_has_fault(&state));\n"
        "  CHECK(state.gpr[5] == UINT32_C(0x70000800) && state.gpr[6] == UINT32_C(0xA5A5A5A5) && porpoise_fpr_get_bits(&state, 22U, 0U) == UINT64_C(0x1122334455667788));\n"
        "  porpoise_state_clear_fault(&state); state.gpr[9] = UINT32_C(0x70000800); state.gpr[10] = UINT32_C(0x5A5A5A5A);\n"
        "  porpoise_lifted_psq_d_store_update_semantics(&state); CHECK(porpoise_state_has_fault(&state));\n"
        "  CHECK(state.gpr[9] == UINT32_C(0x70000800) && state.gpr[10] == UINT32_C(0x5A5A5A5A)); porpoise_state_clear_fault(&state);\n"
        "  state.gpr[18] = UINT32_C(0x70000000); state.gpr[19] = 0U; state.gpr[20] = UINT32_C(0xA5A5A5A5); porpoise_fpr_set_bits(&state, 18U, 0U, UINT64_C(0x8877665544332211));\n"
        "  porpoise_lifted_psq_indexed_load_update_semantics(&state); CHECK(porpoise_state_has_fault(&state));\n"
        "  CHECK(state.gpr[18] == UINT32_C(0x70000000) && state.gpr[20] == UINT32_C(0xA5A5A5A5) && porpoise_fpr_get_bits(&state, 18U, 0U) == UINT64_C(0x8877665544332211));\n"
        "  porpoise_state_clear_fault(&state); state.gpr[22] = UINT32_C(0x70000000); state.gpr[23] = 0U; state.gpr[24] = UINT32_C(0x5A5A5A5A);\n"
        "  porpoise_lifted_psq_indexed_store_update_semantics(&state); CHECK(porpoise_state_has_fault(&state));\n"
        "  CHECK(state.gpr[22] == UINT32_C(0x70000000) && state.gpr[24] == UINT32_C(0x5A5A5A5A)); porpoise_state_clear_fault(&state);\n"
        "  state.gpr[21] = 0x70000000U; state.gpr[22] = 0xDEADBEEFU; state.gpr[12] = 0U;\n"
        "  porpoise_lifted_extended_lwzux_fault_semantics(&state);\n"
        "  CHECK(porpoise_state_has_fault(&state));\n"
        "  CHECK(state.gpr[21] == 0x70000000U && state.gpr[22] == 0xDEADBEEFU);\n"
        "  porpoise_state_clear_fault(&state);\n"
        "  state.gpr[20] = 0x70000000U; state.gpr[5] = 0x12345678U; state.gpr[12] = 0U;\n"
        "  porpoise_lifted_extended_stwux_fault_semantics(&state);\n"
        "  CHECK(porpoise_state_has_fault(&state));\n"
        "  CHECK(state.gpr[20] == 0x70000000U && state.gpr[5] == 0x12345678U);\n"
        "  porpoise_state_clear_fault(&state);\n"
        "  state.gpr[3] = 0x70000000U; state.gpr[28] = 0x11111111U; state.gpr[31] = 0x44444444U;\n"
        "  porpoise_lifted_extended_stmw_fault_semantics(&state);\n"
        "  CHECK(porpoise_state_has_fault(&state));\n"
        "  CHECK(state.gpr[28] == 0x11111111U && state.gpr[31] == 0x44444444U);\n"
        "  porpoise_state_clear_fault(&state);\n"
        "  porpoise_cr_set_field(&state, 1U, 2U);\n"
        "  porpoise_lifted_extended_cr_semantics(&state);\n"
        "  CHECK(porpoise_cr_get_field(&state, 0U) == 6U);\n"
        "  CHECK(porpoise_cr_get_field(&state, 1U) == 4U);\n"
        "  porpoise_lifted_extended_rlwinm_record_semantics(&state);\n"
        "  CHECK(state.gpr[4] == 0xFFFFFFFFU && porpoise_cr_get_field(&state, 0U) == 8U);\n"
        "  porpoise_lifted_extended_alias_record_semantics(&state);\n"
        "  CHECK(state.gpr[4] == 1U && porpoise_cr_get_field(&state, 0U) == 4U);\n"
        "  porpoise_lifted_extended_sraw_record_semantics(&state);\n"
        "  CHECK(state.gpr[5] == 0xFFFFFFFFU && porpoise_cr_get_field(&state, 0U) == 8U);\n"
        "  CHECK((state.xer & 0x20000000U) != 0U);\n"
        "  state.gpr[0] = 0U; state.gpr[13] = 0x80000200U;\n"
        "  porpoise_lifted_relocation_immediate_semantics(&state);\n"
        "  CHECK(state.gpr[3] == 0x80010000U && state.gpr[4] == 0x8000FFFCU);\n"
        "  CHECK(state.gpr[5] == 0x7FFF0000U && state.gpr[6] == 0x800001F0U);\n"
        "  CHECK(state.gpr[7] == 0x80000200U && state.gpr[8] == 0x1234U);\n"
        "  state.gpr[2] = 0x80000300U; state.gpr[3] = 0x80000400U;\n"
        "  porpoise_store_u32(&state, 0x80000420U, 0xDEADBEEFU);\n"
        "  porpoise_store_f32(&state, 0x80000424U, 1.5F);\n"
        "  porpoise_store_f64(&state, 0x80000340U, -2.25);\n"
        "  porpoise_store_u32(&state, 0x80000428U, 0xCAFEBABEU);\n"
        "  CHECK(!porpoise_state_has_fault(&state));\n"
        "  porpoise_lifted_relocation_memory_semantics(&state);\n"
        "  CHECK(!porpoise_state_has_fault(&state));\n"
        "  CHECK(state.gpr[9] == 0xDEADBEEFU && state.gpr[10] == 0xDEADBEEFU);\n"
        "  CHECK(porpoise_load_u32(&state, 0x80000230U) == 0xDEADBEEFU);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 1U, 0U) == 1.5 && porpoise_fpr_get_f64(&state, 2U, 0U) == -2.25);\n"
        "  CHECK(porpoise_load_f32(&state, 0x80000350U) == 1.5F);\n"
        "  CHECK(state.gpr[11] == 0xCAFEBABEU && state.gpr[3] == 0x80000428U);\n"
        "  porpoise_lifted_fault_propagation(&state);\n"
        "  CHECK(porpoise_state_has_fault(&state));\n"
        "  CHECK(state.gpr[5] == 1U);\n"
        "  porpoise_libporpoise_adapter_shutdown(&host);\n"
        "  return 0;\n"
        "}\n",
        encoding="utf-8",
    )
    with (opcodes / "meson.build").open("a", encoding="utf-8") as meson_file:
        meson_file.write(
            "\nsemantic_harness = executable('semantic_harness', "
            "'tests/semantic_harness.c', dependencies: porpoise_lifted_dep)\n"
        )
    add_stub(opcodes)
    run("meson", "setup", "build", "--wrap-mode=forcefallback", *CHILD_MESON_ARGS, cwd=opcodes)
    run("meson", "compile", "-C", "build", cwd=opcodes)
    semantic_executable = opcodes / "build" / ("semantic_harness.exe" if os.name == "nt" else "semantic_harness")
    run(semantic_executable, cwd=opcodes)

    with_entry = temporary / "with-entry"
    run(TOOL, FIXTURES / "inputs" / "with_main", "--output", with_entry)
    assert (with_entry / "src" / "porpoise_entry.c").exists()
    entry_source = (with_entry / "src" / "porpoise_entry.c").read_text(encoding="utf-8")
    assert "DolphinMain" in entry_source
    assert "PorpoiseHostPrepareTitleEntryV1" in entry_source
    assert "porpoise_state_prepare_title_entry(&state)" in entry_source
    assert "__start" not in entry_source
    entry_meson = (with_entry / "meson.build").read_text(encoding="utf-8")
    assert "dependency('porpoise-title-host', fallback:" in entry_meson
    lifted_entry_source = (
        with_entry / "src" / "lifted" / "main.c"
    ).read_text(encoding="utf-8")
    assert "porpoise_psq_load" in lifted_entry_source
    add_stub(with_entry)
    run("meson", "setup", "build", "--wrap-mode=forcefallback", *CHILD_MESON_ARGS, cwd=with_entry)
    run("meson", "compile", "-C", "build", cwd=with_entry)
    executable = with_entry / "build" / ("porpoise_title.exe" if os.name == "nt" else "porpoise_title")
    entry_result = run(executable, cwd=with_entry)
    assert entry_result.stderr == ""

    unknown_output = temporary / "unknown-output"
    run(TOOL, FIXTURES / "inputs" / "invalid" / "unknown.s", "--output", unknown_output, expected=3)
    assert not unknown_output.exists()
    malformed_output = temporary / "malformed-output"
    run(TOOL, FIXTURES / "inputs" / "invalid" / "malformed.s", "--output", malformed_output, expected=3)
    assert not malformed_output.exists()

    metadata_cases = {
        "duplicate": ["80006000", "80006000"],
        "descending": ["80006004", "80006000"],
        "unaligned": ["80006002"],
    }
    for case_name, addresses in metadata_cases.items():
        metadata_input = temporary / f"metadata-{case_name}.s"
        lines = [".text", f".fn metadata_{case_name}, global"]
        for instruction_index, address in enumerate(addresses):
            lines.append(
                f"/* {address} {instruction_index * 4:08X}  60 00 00 00 */ nop"
            )
        lines.append(f".endfn metadata_{case_name}")
        metadata_input.write_text("\n".join(lines) + "\n", encoding="utf-8")
        run(
            TOOL,
            metadata_input,
            "--output",
            temporary / f"metadata-{case_name}-output",
            expected=3,
        )

    invalid_noop = temporary / "invalid-noop.s"
    invalid_noop.write_text(
        ".text\n.fn invalid_noop, global\n"
        "/* 80006100 00000000  7C 00 04 AC */ sync definitely, not_valid\n"
        ".endfn invalid_noop\n",
        encoding="utf-8",
    )
    run(TOOL, invalid_noop, "--output", temporary / "invalid-noop-output", expected=3)

    invalid_opcode_cases = {
        "slwi-width": "slwi r3, r4, 32",
        "extrwi-zero": "extrwi r3, r4, 0, 0",
        "extrwi-range": "extrwi r3, r4, 8, 28",
        "clrlslwi-range": "clrlslwi r3, r4, 3, 4",
        "subi-range": "subi r3, r4, 0x8001",
        "lmw-overlap": "lmw r3, 0(r4)",
        "lmw-zero-overlap": "lmw r0, 0(r0)",
        "lwzux-zero-base": "lwzux r3, r0, r4",
        "lwzux-destination-base": "lwzux r3, r3, r4",
        "rlwnm-mask-range": "rlwnm r3, r4, r5, 32, 0",
        "conditional-return-operands": "beqlr cr0, extra",
        "paired-madd-arity": "ps_madd f3, f1, f2",
        "paired-compare-field": "ps_cmpo0 cr8, f1, f2",
        "frsp-arity": "frsp f2",
        "mtfsf-mask-range": "mtfsf 256, f1",
        "mtfsb1-bit-range": "mtfsb1 32",
        "mtfsb1-field-range": "mtfsb1 cr8gt",
        "scalar-fma-arity": "fmadd f4, f1, f2",
        "scalar-fma-register-kind": "fmadd f4, f1, f2, r3",
        "psq-d-arity": "psq_l f1, 0(r3), 0",
        "psq-d-displacement-low": "psq_l f1, -2049(r3), 0, qr1",
        "psq-d-displacement-high": "psq_l f1, 2048(r3), 0, qr1",
        "psq-d-w-range": "psq_l f1, 0(r3), 2, qr1",
        "psq-d-gqr-range": "psq_l f1, 0(r3), 0, qr8",
        "psq-d-gqr-syntax": "psq_l f1, 0(r3), 0, r1",
        "psq-update-zero-base": "psq_lu f1, 0(r0), 0, qr1",
        "psq-indexed-update-zero-base": "psq_lux f1, r0, r3, 0, qr1",
        "psq-indexed-arity": "psq_lx f1, r0, r3, 0",
        "psq-indexed-w-range": "psq_lx f1, r0, r3, 2, qr1",
        "psq-indexed-gqr-range": "psq_stx f1, r0, r3, 0, qr8",
        "cr-bit-range": "crclr cr8eq",
        "relocation-wrong-context": "lis r3, synthetic@l",
        "relocation-unknown-suffix": "addi r3, r4, synthetic@bogus",
        "relocation-empty-symbol": "addi r3, r4, @l",
        "memory-relocation-wrong-context": "lwz r3, synthetic@ha(r4)",
        "quoted-branch-unterminated": 'bl "unterminated,target',
        "quoted-branch-dangling-escape": 'bl "unterminated,target\\',
    }
    for case_name, instruction in invalid_opcode_cases.items():
        invalid_opcode = temporary / f"invalid-opcode-{case_name}.s"
        invalid_opcode.write_text(
            ".text\n.fn invalid_opcode, global\n"
            f"/* 80006100 00000000  60 00 00 00 */ {instruction}\n"
            ".endfn invalid_opcode\n",
            encoding="utf-8",
        )
        invalid_result = run(
            TOOL,
            invalid_opcode,
            "--output",
            temporary / f"invalid-opcode-{case_name}-output",
            expected=3,
        )
        if case_name.startswith("quoted-branch-"):
            assert "invalid operands for bl" in invalid_result.stderr

    psq_encoding_mismatches = {
        "d-displacement": ("E0 23 10 00", "psq_l f1, 1(r3), 0, qr1"),
        "d-w": ("E0 23 10 00", "psq_l f1, 0(r3), 1, qr1"),
        "d-gqr": ("E0 23 10 00", "psq_l f1, 0(r3), 0, qr2"),
        "indexed-w": ("10 A0 18 8C", "psq_lx f5, r0, r3, 1, qr1"),
        "indexed-gqr": ("10 A0 18 8C", "psq_lx f5, r0, r3, 0, qr2"),
        "indexed-update-xo": ("12 52 98 8C", "psq_lux f18, r18, r19, 0, qr1"),
    }
    for case_name, (word, instruction) in psq_encoding_mismatches.items():
        mismatch_input = temporary / f"psq-encoding-mismatch-{case_name}.s"
        mismatch_input.write_text(
            ".text\n.fn psq_encoding_mismatch, global\n"
            f"/* 80006100 00000000  {word} */ {instruction}\n"
            ".endfn psq_encoding_mismatch\n",
            encoding="utf-8",
        )
        run(
            TOOL,
            mismatch_input,
            "--output",
            temporary / f"psq-encoding-mismatch-{case_name}-output",
            expected=3,
        )

    relocation_base_mismatch = temporary / "relocation-base-mismatch.s"
    relocation_base_mismatch.write_text(
        ".text\n.fn relocation_base_mismatch, global\n"
        "/* 80006100 00000000  80 64 00 20 */ lwz r3, synthetic@l(r5)\n"
        ".endfn relocation_base_mismatch\n",
        encoding="utf-8",
    )
    relocation_base_result = run(
        TOOL,
        relocation_base_mismatch,
        "--output",
        temporary / "relocation-base-mismatch-output",
        expected=3,
    )
    assert "invalid operands for lwz" in relocation_base_result.stderr

    escaped_quote_branch = temporary / "escaped-quote-branch.s"
    escaped_quote_branch.write_text(
        ".text\n.fn escaped_quote_branch, global\n"
        r'/* 80006100 00000000  48 00 00 01 */ bl "missing\"quoted,target"' "\n"
        ".endfn escaped_quote_branch\n",
        encoding="utf-8",
    )
    escaped_quote_result = run(
        TOOL,
        escaped_quote_branch,
        "--output",
        temporary / "escaped-quote-branch-output",
        expected=3,
    )
    assert 'branch target missing"quoted,target is neither' in escaped_quote_result.stderr
    assert "invalid operands for bl" not in escaped_quote_result.stderr

    malformed_annotation = temporary / "malformed-annotation.s"
    malformed_annotation.write_text(
        ".text\n.fn malformed_annotation, global\n"
        "/* 80006200 junk 4 E 0 20 trailing */ blr\n"
        ".endfn malformed_annotation\n",
        encoding="utf-8",
    )
    run(
        TOOL,
        malformed_annotation,
        "--output",
        temporary / "malformed-annotation-output",
        expected=3,
    )

    malformed_directive = temporary / "malformed-directive.s"
    malformed_directive.write_text(
        ".text\n.fn malformed_directive, global trailing\n"
        "/* 80006300 00000000  60 00 00 00 */ nop\n"
        ".endfn malformed_directive\n",
        encoding="utf-8",
    )
    run(
        TOOL,
        malformed_directive,
        "--output",
        temporary / "malformed-directive-output",
        expected=3,
    )

    atomic_output = temporary / "atomic-output"
    atomic_output.mkdir()
    atomic_marker = atomic_output / "original.txt"
    atomic_marker.write_text("original", encoding="utf-8")
    run(
        TOOL,
        FIXTURES / "inputs" / "invalid" / "unknown.s",
        "--output",
        atomic_output,
        "--force",
        expected=3,
    )
    assert atomic_marker.read_text(encoding="utf-8") == "original"

    undeclared_external = temporary / "undeclared-external"
    run(TOOL, FIXTURES / "inputs" / "abi", "--output", undeclared_external, expected=3)
    assert not undeclared_external.exists()

    imported = temporary / "imported"
    run(
        TOOL,
        FIXTURES / "inputs" / "abi",
        "--output",
        imported,
        "--abi",
        FIXTURES / "abi" / "imports.json",
    )
    imports_source = (imported / "src" / "porpoise_imports.c").read_text(encoding="utf-8")
    assert "porpoise_decode_pointer" in imports_source
    assert "PorpoiseStubReportAdapter(state)" in imports_source
    abi_harness = imported / "tests" / "abi_harness.c"
    abi_harness.parent.mkdir(parents=True)
    abi_harness.write_text(
        "#include <stdlib.h>\n"
        "#include \"porpoise_generated.h\"\n"
        "#include \"porpoise_libporpoise_adapter.h\"\n"
        "#include <porpoise/stub.h>\n"
        "#define CHECK(condition) do { if (!(condition)) abort(); } while (0)\n"
        "int main(void) {\n"
        "  PorpoiseHostAdapter host; PorpoisePpcState state;\n"
        "  CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);\n"
        "  porpoise_state_init(&state, &host);\n"
        "  state.gpr[3] = 1U; state.gpr[4] = 2U;\n"
        "  porpoise_fpr_set_f64(&state, 1U, 0U, 1.5); porpoise_fpr_set_f64(&state, 2U, 0U, 2.25);\n"
        "  porpoise_lifted_call_imports(&state);\n"
        "  CHECK(!porpoise_state_has_fault(&state));\n"
        "  CHECK(state.gpr[3] == 0x80000003U);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 1U, 0U) == 3.75);\n"
        "  CHECK(PorpoiseStubReportCount() == 1U);\n"
        "  porpoise_libporpoise_adapter_shutdown(&host);\n"
        "  return 0;\n"
        "}\n",
        encoding="utf-8",
    )
    with (imported / "meson.build").open("a", encoding="utf-8") as meson_file:
        meson_file.write(
            "\nabi_harness = executable('abi_harness', "
            "'tests/abi_harness.c', dependencies: porpoise_lifted_dep)\n"
        )
    add_stub(imported)
    run("meson", "setup", "build", "--wrap-mode=forcefallback", *CHILD_MESON_ARGS, cwd=imported)
    run("meson", "compile", "-C", "build", cwd=imported)
    abi_executable = imported / "build" / ("abi_harness.exe" if os.name == "nt" else "abi_harness")
    run(abi_executable, cwd=imported)

    terminal_import_input = temporary / "terminal-import.s"
    terminal_import_input.write_text(
        ".text\n.global terminal_import_caller\n"
        ".fn terminal_import_caller, global\n"
        "/* 80004100 00000000  48 00 00 01 */ bl TerminalHostCall\n"
        "/* 80004104 00000004  38 60 00 63 */ li r3, 99\n"
        "/* 80004108 00000008  4E 80 00 20 */ blr\n"
        ".endfn terminal_import_caller\n",
        encoding="utf-8",
    )
    terminal_import_abi = temporary / "terminal-import.json"
    terminal_import_abi.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "functions": [
                    {
                        "kind": "import",
                        "symbol": "TerminalHostCall",
                        "adapter": "TerminalImportAdapter",
                        "header": "terminal_import_adapter.h",
                        "return": {"type": "void"},
                        "arguments": [],
                    }
                ],
            }
        ),
        encoding="utf-8",
    )
    terminal_imported = temporary / "terminal-imported"
    run(
        TOOL,
        terminal_import_input,
        "--output",
        terminal_imported,
        "--abi",
        terminal_import_abi,
    )
    (terminal_imported / "include" / "terminal_import_adapter.h").write_text(
        "#ifndef TERMINAL_IMPORT_ADAPTER_H\n"
        "#define TERMINAL_IMPORT_ADAPTER_H\n"
        "#include \"porpoise_lifted.h\"\n"
        "void TerminalImportAdapter(PorpoisePpcState *state);\n"
        "#endif\n",
        encoding="utf-8",
    )
    terminal_harness = terminal_imported / "tests" / "terminal_harness.c"
    terminal_harness.parent.mkdir(parents=True)
    terminal_harness.write_text(
        "#include <stdlib.h>\n"
        "#include \"porpoise_generated.h\"\n"
        "#include \"porpoise_libporpoise_adapter.h\"\n"
        "#include \"terminal_import_adapter.h\"\n"
        "#define CHECK(condition) do { if (!(condition)) abort(); } while (0)\n"
        "void TerminalImportAdapter(PorpoisePpcState *state) { state->status = PORPOISE_EXECUTION_RETURNED; }\n"
        "int main(void) {\n"
        "  PorpoiseHostAdapter host; PorpoisePpcState state;\n"
        "  CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);\n"
        "  porpoise_state_init(&state, &host); state.status = PORPOISE_EXECUTION_RUNNING; state.gpr[3] = 7U;\n"
        "  porpoise_lifted_terminal_import_caller(&state);\n"
        "  CHECK(!porpoise_state_has_fault(&state)); CHECK(state.status == PORPOISE_EXECUTION_RETURNED);\n"
        "  CHECK(state.gpr[3] == 7U); porpoise_libporpoise_adapter_shutdown(&host); return 0;\n"
        "}\n",
        encoding="utf-8",
    )
    with (terminal_imported / "meson.build").open("a", encoding="utf-8") as meson_file:
        meson_file.write(
            "\nterminal_harness = executable('terminal_harness', "
            "'tests/terminal_harness.c', dependencies: porpoise_lifted_dep)\n"
        )
    add_stub(terminal_imported)
    run(
        "meson",
        "setup",
        "build",
        "--wrap-mode=forcefallback",
        *CHILD_MESON_ARGS,
        cwd=terminal_imported,
    )
    run("meson", "compile", "-C", "build", cwd=terminal_imported)
    terminal_executable = terminal_imported / "build" / (
        "terminal_harness.exe" if os.name == "nt" else "terminal_harness"
    )
    run(terminal_executable, cwd=terminal_imported)

    exported = temporary / "exported"
    run(
        TOOL,
        FIXTURES / "inputs" / "abi_exports",
        "--output",
        exported,
        "--abi",
        FIXTURES / "abi" / "exports.json",
    )
    assert "PorpoiseAddOne" in (exported / "src" / "porpoise_exports.c").read_text(encoding="utf-8")
    export_harness = exported / "tests" / "export_harness.c"
    export_harness.parent.mkdir(parents=True)
    export_harness.write_text(
        "#include <stdlib.h>\n"
        "#include \"porpoise_exports.h\"\n"
        "#include \"porpoise_libporpoise_adapter.h\"\n"
        "#define CHECK(condition) do { if (!(condition)) abort(); } while (0)\n"
        "int main(void) {\n"
        "  PorpoiseHostAdapter host; PorpoisePpcState state;\n"
        "  CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);\n"
        "  porpoise_state_init(&state, &host); porpoise_bind_export_state(&state);\n"
        "  CHECK(PorpoiseAddOne(41U) == 42U);\n"
        "  CHECK(PorpoiseAddFloat(1.25F, 2.5F) == 3.75F);\n"
        "  CHECK(PorpoiseAddDouble(1.25, 2.5) == 3.75);\n"
        "  state.status = PORPOISE_EXECUTION_RETURNED; CHECK(PorpoiseAddOne(41U) == 0U);\n"
        "  state.status = PORPOISE_EXECUTION_READY; porpoise_state_set_fault(&state, PORPOISE_FAULT_INVALID_STATE, 0U, 0);\n"
        "  CHECK(PorpoiseAddOne(41U) == 0U); porpoise_state_clear_fault(&state);\n"
        "  porpoise_bind_export_state(0);\n"
        "  porpoise_libporpoise_adapter_shutdown(&host);\n"
        "  return 0;\n"
        "}\n",
        encoding="utf-8",
    )
    with (exported / "meson.build").open("a", encoding="utf-8") as meson_file:
        meson_file.write(
            "\nexport_harness = executable('export_harness', "
            "'tests/export_harness.c', dependencies: porpoise_lifted_dep)\n"
        )
    add_stub(exported)
    run("meson", "setup", "build", "--wrap-mode=forcefallback", *CHILD_MESON_ARGS, cwd=exported)
    run("meson", "compile", "-C", "build", cwd=exported)
    export_executable = exported / "build" / ("export_harness.exe" if os.name == "nt" else "export_harness")
    run(export_executable, cwd=exported)

    run(
        TOOL,
        FIXTURES / "inputs" / "basic",
        "--output",
        temporary / "invalid-abi-output",
        "--abi",
        FIXTURES / "abi" / "invalid_unknown_key.json",
        expected=2,
    )

    new_runtime_helpers = (
        "porpoise_sign_extend8",
        "porpoise_sign_extend16",
        "porpoise_count_leading_zeros32",
        "porpoise_add_with_carry32",
        "porpoise_state_prepare_title_entry",
        "porpoise_state_should_stop",
        "porpoise_future_reserved",
        "PORPOISE_FUTURE_RESERVED",
        "porpoise_load_multiple_words",
        "porpoise_store_multiple_words",
        "porpoise_fpr_get_bits",
        "porpoise_fpr_set_bits",
        "porpoise_fpr_get_f64",
        "porpoise_fpr_set_f64",
        "porpoise_fpscr_recompute_summaries",
        "porpoise_fpscr_raise_exceptions",
        "porpoise_fpscr_update_cr1",
        "porpoise_fcmpo",
        "porpoise_fcmpu",
        "porpoise_fsel_bits",
        "PORPOISE_FPSCR_FX",
        "PORPOISE_FAULT_FLOATING_POINT_EXCEPTION",
        "PORPOISE_FAULT_FLOATING_POINT_UNAVAILABLE",
        "PORPOISE_HID2_LSQE",
        "PORPOISE_HID2_PSE",
        "PORPOISE_MSR_FP",
        "PorpoiseTitleHostResultV1",
        "PorpoiseHostPrepareTitleEntryV1",
    )
    for helper_name in new_runtime_helpers:
        reserved_abi = temporary / f"reserved-{helper_name}.json"
        reserved_abi.write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "functions": [
                        {
                            "kind": "import",
                            "symbol": helper_name,
                            "header": "porpoise/stub.h",
                            "return": {"type": "void"},
                            "arguments": [],
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )
        run(
            TOOL,
            FIXTURES / "inputs" / "basic",
            "--output",
            temporary / f"reserved-{helper_name}-output",
            "--abi",
            reserved_abi,
            expected=2,
        )

    protected = temporary / "protected"
    protected.mkdir()
    marker = protected / "keep.txt"
    marker.write_text("keep", encoding="utf-8")
    run(TOOL, FIXTURES / "inputs" / "basic", "--output", protected, expected=2)
    assert marker.read_text(encoding="utf-8") == "keep"
    run(TOOL, FIXTURES / "inputs" / "basic", "--output", protected, "--force")
    assert not marker.exists()

    overlapping_input = temporary / "overlapping-input"
    shutil.copytree(FIXTURES / "inputs" / "basic", overlapping_input)
    original_input = (overlapping_input / "no_entry.s").read_bytes()
    run(TOOL, overlapping_input, "--output", overlapping_input, "--force", expected=2)
    assert (overlapping_input / "no_entry.s").read_bytes() == original_input
    run(
        TOOL,
        overlapping_input,
        "--output",
        overlapping_input / "generated-child",
        expected=2,
    )
    assert not (overlapping_input / "generated-child").exists()

    overlapping_alias = temporary / "overlapping-alias"
    alias_created = False
    try:
        overlapping_alias.symlink_to(overlapping_input, target_is_directory=True)
        alias_created = True
    except OSError:
        if os.name == "nt":
            junction = subprocess.run(
                ["cmd", "/c", "mklink", "/J", str(overlapping_alias), str(overlapping_input)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            alias_created = junction.returncode == 0
    if alias_created:
        run(TOOL, overlapping_alias, "--output", overlapping_input, "--force", expected=2)
        assert (overlapping_input / "no_entry.s").read_bytes() == original_input
        if overlapping_alias.is_symlink():
            overlapping_alias.unlink()
        elif os.name == "nt":
            os.rmdir(overlapping_alias)

    cwd_guard_root = temporary / "cwd-guard-root"
    cwd_guard_work = cwd_guard_root / "working"
    cwd_guard_work.mkdir(parents=True)
    cwd_guard_marker = cwd_guard_root / "must-survive.txt"
    cwd_guard_marker.write_text("safe", encoding="utf-8")
    run(
        TOOL,
        FIXTURES / "inputs" / "basic",
        "--output",
        cwd_guard_root,
        "--force",
        cwd=cwd_guard_work,
        expected=2,
    )
    assert cwd_guard_marker.read_text(encoding="utf-8") == "safe"

    trailing_output = str(temporary / "trailing-output") + os.sep
    run(TOOL, FIXTURES / "inputs" / "basic", "--output", trailing_output)
    assert (temporary / "trailing-output" / "porpoise-report.json").exists()

    outside = temporary / "outside"
    outside.mkdir()
    outside_marker = outside / "must-survive.txt"
    outside_marker.write_text("safe", encoding="utf-8")
    linked_output = temporary / "linked-output"
    linked_output.mkdir()
    try:
        (linked_output / "external-link").symlink_to(outside, target_is_directory=True)
    except OSError:
        pass
    else:
        run(TOOL, FIXTURES / "inputs" / "basic", "--output", linked_output, "--force")
        assert outside_marker.read_text(encoding="utf-8") == "safe"

    deterministic = temporary / "deterministic"
    run(TOOL, FIXTURES / "inputs" / "nested", "--output", deterministic)
    first_digest = tree_digest(deterministic)
    run(TOOL, FIXTURES / "inputs" / "nested", "--output", deterministic, "--force")
    assert tree_digest(deterministic) == first_digest
    assert (deterministic / "src" / "lifted" / "alpha" / "shared.c").exists()
    assert (deterministic / "src" / "lifted" / "beta" / "shared.c").exists()
    deterministic_report = json.loads(
        (deterministic / "porpoise-report.json").read_text(encoding="utf-8")
    )
    deterministic_files = [item["input"] for item in deterministic_report["files"]]
    assert deterministic_files == sorted(deterministic_files)

    partial_skip = temporary / "partial-skip.txt"
    partial_skip.write_text("alpha_shared\n", encoding="utf-8")
    partial_skip_output = temporary / "partial-skip-output"
    run(
        TOOL,
        FIXTURES / "inputs" / "nested",
        "--output",
        partial_skip_output,
        "--skip-list",
        partial_skip,
    )
    partial_report = json.loads(
        (partial_skip_output / "porpoise-report.json").read_text(encoding="utf-8")
    )
    statuses = {item["symbol"]: item["status"] for item in partial_report["functions"]}
    assert statuses["alpha_shared"] == "skipped"
    assert statuses["beta_shared"] == "lifted"
    partial_source = (partial_skip_output / "src" / "lifted" / "alpha" / "shared.c").read_text(
        encoding="utf-8"
    )
    assert "porpoise_lifted_alpha_shared" not in partial_source

    collision_input = temporary / "collision-input"
    collision_input.mkdir()
    collision_text = (
        ".text\n.fn {name}, global\n"
        "/* {address} 00000000  4E 80 00 20 */ blr\n.endfn {name}\n"
    )
    (collision_input / "a-b.s").write_text(
        collision_text.format(name="one", address="80007000"), encoding="utf-8"
    )
    (collision_input / "a_b.s").write_text(
        collision_text.format(name="two", address="80007100"), encoding="utf-8"
    )
    run(TOOL, collision_input, "--output", temporary / "collision-output", expected=3)

    approximate = temporary / "approximate.s"
    approximate.write_text(
        ".text\n.fn estimate, global\n"
        "/* 80000000 00000000  EC 21 00 30 */ fres f1, f1\n"
        "/* 80000004 00000004  4E 80 00 20 */ blr\n.endfn estimate\n",
        encoding="utf-8",
    )
    run(TOOL, approximate, "--output", temporary / "approximate-output")
    run(TOOL, approximate, "--output", temporary / "strict-output", "--strict", expected=3)

    paired_approximate = temporary / "paired-approximate.s"
    paired_approximate.write_text(
        ".text\n.fn paired_approximate, global\n"
        "/* 80000000 00000000  10 61 10 FA */ ps_madd f3, f1, f3, f2\n"
        ".endfn paired_approximate\n",
        encoding="utf-8",
    )
    paired_approximate_output = temporary / "paired-approximate-output"
    run(TOOL, paired_approximate, "--output", paired_approximate_output)
    paired_approximate_report = json.loads(
        (paired_approximate_output / "porpoise-report.json").read_text(encoding="utf-8")
    )
    assert paired_approximate_report["instructions"][0]["status"] == "approximate"
    assert paired_approximate_report["instructions"][0]["detail"] == paired_fused_detail
    paired_strict_result = run(
        TOOL,
        paired_approximate,
        "--output",
        temporary / "paired-strict-output",
        "--strict",
        expected=3,
    )
    assert "ps_madd instruction uses approximate host semantics" in paired_strict_result.stderr

    scalar_approximate = temporary / "scalar-approximate.s"
    scalar_approximate.write_text(
        ".text\n.fn scalar_approximate, global\n"
        "/* 80000000 00000000  FC 40 08 18 */ frsp f2, f1\n"
        "/* 80000004 00000004  FC 81 18 BA */ fmadd f4, f1, f2, f3\n"
        ".endfn scalar_approximate\n",
        encoding="utf-8",
    )
    scalar_approximate_output = temporary / "scalar-approximate-output"
    run(TOOL, scalar_approximate, "--output", scalar_approximate_output)
    scalar_approximate_report = json.loads(
        (scalar_approximate_output / "porpoise-report.json").read_text(encoding="utf-8")
    )
    scalar_instructions = scalar_approximate_report["instructions"]
    assert [instruction["status"] for instruction in scalar_instructions] == [
        "approximate", "approximate"
    ]
    assert [instruction["detail"] for instruction in scalar_instructions] == [
        scalar_frsp_detail, scalar_fma_detail
    ]
    scalar_strict_result = run(
        TOOL,
        scalar_approximate,
        "--output",
        temporary / "scalar-strict-output",
        "--strict",
        expected=3,
    )
    assert "frsp instruction uses approximate host semantics" in scalar_strict_result.stderr
    assert "fmadd instruction uses approximate host semantics" in scalar_strict_result.stderr

    psq_approximate = temporary / "psq-approximate.s"
    psq_approximate.write_text(
        ".text\n.fn psq_approximate, global\n"
        "/* 80000000 00000000  E0 23 10 00 */ psq_l f1, 0(r3), 0, qr1\n"
        "/* 80000004 00000004  E4 43 A0 00 */ psq_lu f2, 0(r3), 1, qr2\n"
        "/* 80000008 00000008  F0 63 10 00 */ psq_st f3, 0(r3), 0, qr1\n"
        "/* 8000000C 0000000C  F4 83 A0 00 */ psq_stu f4, 0(r3), 1, qr2\n"
        "/* 80000010 00000010  10 A0 18 8C */ psq_lx f5, r0, r3, 0, qr1\n"
        "/* 80000014 00000014  10 C0 1D 0E */ psq_stx f6, r0, r3, 1, qr2\n"
        "/* 80000018 00000018  12 52 98 CC */ psq_lux f18, r18, r19, 0, qr1\n"
        "/* 8000001C 0000001C  12 16 B8 CE */ psq_stux f16, r22, r23, 0, qr1\n"
        ".endfn psq_approximate\n",
        encoding="utf-8",
    )
    psq_approximate_output = temporary / "psq-approximate-output"
    run(TOOL, psq_approximate, "--output", psq_approximate_output)
    psq_approximate_report = json.loads(
        (psq_approximate_output / "porpoise-report.json").read_text(encoding="utf-8")
    )
    assert {instruction["mnemonic"] for instruction in psq_approximate_report["instructions"]} == {
        "psq_l", "psq_lu", "psq_st", "psq_stu", "psq_lx", "psq_lux",
        "psq_stx", "psq_stux",
    }
    assert all(
        instruction["status"] == "approximate" and instruction["detail"] == psq_detail
        for instruction in psq_approximate_report["instructions"]
    )
    psq_strict_result = run(
        TOOL,
        psq_approximate,
        "--output",
        temporary / "psq-strict-output",
        "--strict",
        expected=3,
    )
    for mnemonic in {
        "psq_l", "psq_lu", "psq_st", "psq_stu",
        "psq_lx", "psq_lux", "psq_stx", "psq_stux",
    }:
        assert f"{mnemonic} instruction uses approximate host semantics" in psq_strict_result.stderr

    console_start = temporary / "console-start.s"
    console_start.write_text(
        ".text\n.fn __start, global\n"
        "/* 80008000 00000000  4E 80 00 20 */ blr\n.endfn __start\n",
        encoding="utf-8",
    )
    run(
        TOOL,
        console_start,
        "--output",
        temporary / "console-start-output",
        "--entry",
        "__start",
        expected=2,
    )

    config_dir = temporary / "config"
    config_dir.mkdir()
    skip = config_dir / "skip.txt"
    skip.write_text("add_one\n", encoding="utf-8")
    config = config_dir / "porpoise.json"
    config.write_text(json.dumps({"schema_version": 1, "skip_list": "skip.txt", "strict": True}), encoding="utf-8")
    run(TOOL, FIXTURES / "inputs" / "basic", "--config", config, "--output", temporary / "skipped-output", expected=3)

    bad_config = config_dir / "bad.json"
    bad_config.write_text('{"schema_version":1,"mystery":true}', encoding="utf-8")
    run(TOOL, FIXTURES / "inputs" / "basic", "--config", bad_config, "--output", temporary / "bad-config-output", expected=2)

    precedence_config = config_dir / "precedence.json"
    precedence_config.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "abi": "does-not-exist.json",
                "entry": "does_not_exist",
                "verbosity": "quiet",
            }
        ),
        encoding="utf-8",
    )
    run(
        TOOL,
        FIXTURES / "inputs" / "basic",
        "--config",
        precedence_config,
        "--abi",
        FIXTURES / "abi" / "imports.json",
        "--entry",
        "add_one",
        "--verbose",
        "--output",
        temporary / "precedence-output",
    )
