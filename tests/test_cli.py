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


def run(*arguments, cwd=None, expected=0, env=None):
    completed = subprocess.run(
        [str(argument) for argument in arguments],
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        env=env,
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


def assert_generated_consumer_command(command):
    tokens = {
        token.strip("\"'") for token in shlex.split(command, posix=False)
    }
    assert "-w" not in tokens
    assert "-DLIBPORPOISE_PORT=1" not in tokens
    assert "-DLIBPORPOISE_PORT" in tokens
    if os.name == "nt":
        assert "-DLIBPORPOISE_BUILD_WIN64" in tokens
        assert "-DLIBPORPOISE_BUILD_LINUX" not in tokens
        assert "-D_POSIX_C_SOURCE=200112L" not in tokens
    else:
        assert "-DLIBPORPOISE_BUILD_LINUX" in tokens
        assert "-D_POSIX_C_SOURCE=200112L" in tokens
        assert "-DLIBPORPOISE_BUILD_WIN64" not in tokens


with tempfile.TemporaryDirectory(prefix="porpoise-tests-", ignore_cleanup_errors=True) as temporary:
    temporary = Path(temporary)

    assert "Usage:" in run(TOOL, "--help").stdout
    assert "0.2.0" in run(TOOL, "--version").stdout
    run(TOOL, expected=2)
    run(TOOL, FIXTURES / "inputs" / "basic", expected=2)
    run(
        TOOL,
        FIXTURES / "inputs" / "basic",
        "--output",
        temporary / "invalid-sdk-policy",
        "--sdk-policy",
        "fuzzy",
        expected=2,
    )
    run(
        TOOL,
        FIXTURES / "inputs" / "basic",
        "--output",
        temporary / "unpaired-dtk-splits",
        "--dtk-splits",
        temporary / "splits.txt",
        expected=2,
    )
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
    assert data_report["summary"]["functions"] == 3
    assert data_report["summary"]["data_words"] == 4
    data_statuses = {
        function["symbol"]: function["status"]
        for function in data_report["functions"]
    }
    assert data_statuses == {
        "data_user": "lifted",
        "gap_01_80300100_text": "data",
        "gap_02_80300200_text": "lifted",
        "gap_helper": "lifted",
    }
    data_sources = "\n".join(
        path.read_text(encoding="utf-8")
        for path in sorted((data_output / "src" / "data").glob("porpoise_data_*.c"))
    )
    assert "0x80300000" in data_sources
    assert "0x80300100" in data_sources
    assert "0x4D, 0x65, 0x74, 0x72" in data_sources
    assert "porpoise_lifted_gap_01_80300100_text" not in (
        data_output / "include" / "porpoise_generated.h"
    ).read_text(encoding="utf-8")

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
    no_entry_meson = (no_entry / "meson.build").read_text(encoding="utf-8")
    assert (
        "libporpoise_raw_dep = dependency('libPorpoise', "
        "fallback: ['libPorpoise','libporpoise_dep'])"
    ) in no_entry_meson
    assert "libporpoise_raw_dep.partial_dependency(" in no_entry_meson
    assert "compile_args: false" in no_entry_meson
    assert ").as_system('system')" in no_entry_meson
    assert "'-DLIBPORPOISE_BUILD_WIN64'" in no_entry_meson
    assert "'-DLIBPORPOISE_BUILD_LINUX'" in no_entry_meson
    assert "'-D_POSIX_C_SOURCE=200112L'" in no_entry_meson
    assert no_entry_meson.count("c_args: porpoise_consumer_c_args") == 1
    assert "cpp_args: porpoise_consumer_c_args" not in no_entry_meson
    assert "'src/porpoise_libporpoise_presentation.cpp'" not in no_entry_meson
    registry_source = (
        no_entry / "src" / "porpoise_function_registry.c"
    ).read_text(encoding="utf-8")
    dispatch_header = (
        no_entry / "src" / "porpoise_dispatch_private.h"
    ).read_text(encoding="utf-8")
    assert "enum porpoise_dispatch_kind" in dispatch_header
    assert "struct porpoise_dispatch_target" in dispatch_header
    assert "PORPOISE_GUEST_ARQ_CALLBACK_HACK_ADDRESS" not in dispatch_header
    public_generated_header = (
        no_entry / "include" / "porpoise_generated.h"
    ).read_text(encoding="utf-8")
    public_adapter_header = (
        no_entry / "include" / "porpoise_libporpoise_adapter.h"
    ).read_text(encoding="utf-8")
    assert "porpoise_generated_bind" in public_generated_header
    assert "porpoise_call_address" not in public_generated_header
    assert "porpoise_lifted_" not in public_generated_header
    assert "_adapter(" not in public_adapter_header
    assert "porpoise_libporpoise_configure_title_arena" in public_adapter_header
    assert (
        no_entry / "src" / "porpoise_libporpoise_builtins_private.h"
    ).exists()
    assert (no_entry / "src" / "porpoise_libporpoise_gx.c").exists()
    assert (
        no_entry / "src" / "porpoise_libporpoise_gx_headers.h"
    ).exists()
    assert (
        no_entry / "src" / "porpoise_libporpoise_gx_objects.c"
    ).exists()
    assert (
        no_entry / "src" / "porpoise_libporpoise_gx_values.c"
    ).exists()
    assert not (
        no_entry / "include" / "porpoise_libporpoise_builtins_private.h"
    ).exists()
    assert (no_entry / "src" / "generated" / "no_entry.h").exists()
    assert not (no_entry / "include" / "porpoise" / "generated").exists()
    for public_header_path in (no_entry / "include").rglob("*.h"):
        public_header = public_header_path.read_text(encoding="utf-8")
        assert "porpoise_call_address" not in public_header
        assert "void porpoise_import_" not in public_header
        assert "void porpoise_lifted_" not in public_header
        assert "porpoise_initialize_data" not in public_header
    registry_shards = sorted(
        path.name
        for path in (no_entry / "src").glob("porpoise_function_registry_*.c")
    )
    assert registry_shards == ["porpoise_function_registry_8000.c"]
    assert "switch (address >> 16U)" in registry_source
    assert (
        "case UINT32_C(0x8000): return "
        "porpoise_resolve_address_8000(address);"
    ) in registry_source
    assert "porpoise_lifted_add_one" not in registry_source
    assert (
        "state->lifted_call_depth >= PORPOISE_LIFTED_CALL_STACK_CAPACITY"
        in registry_source
    )
    assert "state->lifted_return_stack[state->lifted_call_depth]" in registry_source
    assert "porpoise_branch_address" in registry_source
    assert "state->lifted_call_depth++" in registry_source
    assert "state->lifted_call_depth--" in registry_source
    assert "porpoise_poll_host_events(state, address)" in registry_source
    registry_shard_source = (
        no_entry / "src" / registry_shards[0]
    ).read_text(encoding="utf-8")
    assert (
        "void porpoise_lifted_add_one(PorpoisePpcState *state);"
        in registry_shard_source
    )
    assert (
        "case UINT32_C(0x80001000): return "
        "(struct porpoise_dispatch_target){porpoise_lifted_add_one, "
        'PORPOISE_DISPATCH_LIFTED, "add_one"};'
    ) in registry_shard_source
    public_consumer = no_entry / "tests" / "public_consumer.c"
    public_consumer.parent.mkdir(parents=True)
    public_consumer.write_text(
        "#include <porpoise_generated.h>\n"
        "#include <stdlib.h>\n"
        "#define CHECK(condition) do { if (!(condition)) abort(); } while (0)\n"
        "int main(void) {\n"
        "  PorpoiseHostAdapter host;\n"
        "  CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);\n"
        "  CHECK(porpoise_generated_bind(&host) == PORPOISE_HOST_OK);\n"
        "  porpoise_libporpoise_adapter_shutdown(&host);\n"
        "  return 0;\n"
        "}\n",
        encoding="utf-8",
    )
    private_adapter_consumer = (
        no_entry / "tests" / "private_adapter_consumer.c"
    )
    private_adapter_consumer.write_text(
        "#include <porpoise_generated.h>\n"
        "int main(void) {\n"
        "  porpoise_libporpoise_os_report_adapter((PorpoisePpcState *)0);\n"
        "  return 0;\n"
        "}\n",
        encoding="utf-8",
    )
    with (no_entry / "meson.build").open("a", encoding="utf-8") as meson_file:
        meson_file.write(
            "\npublic_consumer = executable('public_consumer', "
            "'tests/public_consumer.c', dependencies: porpoise_lifted_dep)\n"
            "private_adapter_consumer = executable('private_adapter_consumer', "
            "'tests/private_adapter_consumer.c', "
            "dependencies: porpoise_lifted_dep)\n"
        )
    add_stub(no_entry)
    assert "fallback: ['libPorpoise','libporpoise_dep']" in no_entry_meson
    assert "porpoise-title-host" not in (no_entry / "meson.build").read_text(encoding="utf-8")
    assert not any(no_entry.glob("subprojects/*.wrap"))
    run("meson", "setup", "build", "--wrap-mode=forcefallback", *CHILD_MESON_ARGS, cwd=no_entry)
    compile_commands = json.loads(
        (no_entry / "build" / "compile_commands.json").read_text(encoding="utf-8")
    )
    lifted_compile_commands = [
        item["command"]
        for item in compile_commands
        if Path(item["file"]).as_posix().endswith("src/porpoise_lifted.c")
    ]
    assert len(lifted_compile_commands) == 1
    assert_generated_consumer_command(lifted_compile_commands[0])
    run("meson", "compile", "-C", "build", "public_consumer", cwd=no_entry)
    private_adapter_compile = run(
        "meson",
        "compile",
        "-C",
        "build",
        "private_adapter_consumer",
        cwd=no_entry,
        expected=1,
    )
    assert "porpoise_libporpoise_os_report_adapter" in (
        private_adapter_compile.stdout + private_adapter_compile.stderr
    )

    sharded_input = temporary / "registry-shards-input"
    sharded_input.mkdir()
    (sharded_input / "registry.s").write_text(
        ".include \"macros.inc\"\n\n"
        ".text\n"
        ".global registry_low\n"
        ".fn registry_low, global\n"
        "/* 80001000 00000000  38 63 00 01 */ addi r3, r3, 1\n"
        "/* 80001004 00000004  4E 80 00 20 */ blr\n"
        ".endfn registry_low\n\n"
        ".global registry_high\n"
        ".fn registry_high, global\n"
        "/* 80101000 00100000  38 63 00 02 */ addi r3, r3, 2\n"
        "/* 80101004 00100004  4E 80 00 20 */ blr\n"
        ".endfn registry_high\n\n"
        ".global __ARQCallbackHack\n"
        ".fn __ARQCallbackHack, global\n"
        "/* 80102000 00101000  4E 80 00 20 */ blr\n"
        ".endfn __ARQCallbackHack\n",
        encoding="utf-8",
    )
    sharded_output = temporary / "registry-shards-output"
    run(TOOL, sharded_input, "--output", sharded_output)
    assert sorted(
        path.name
        for path in (sharded_output / "src").glob(
            "porpoise_function_registry_*.c"
        )
    ) == [
        "porpoise_function_registry_8000.c",
        "porpoise_function_registry_8010.c",
    ]
    sharded_master = (
        sharded_output / "src" / "porpoise_function_registry.c"
    ).read_text(encoding="utf-8")
    assert sharded_master.index("porpoise_resolve_address_8000") < (
        sharded_master.index("porpoise_resolve_address_8010")
    )
    sharded_meson = (sharded_output / "meson.build").read_text(
        encoding="utf-8"
    )
    assert sharded_meson.index("porpoise_function_registry_8000.c") < (
        sharded_meson.index("porpoise_function_registry_8010.c")
    )
    sharded_dispatch = (
        sharded_output / "src" / "porpoise_dispatch_private.h"
    ).read_text(encoding="utf-8")
    assert (
        "#define PORPOISE_GUEST_ARQ_CALLBACK_HACK_ADDRESS "
        "UINT32_C(0x80102000)"
    ) in sharded_dispatch

    opcodes = temporary / "opcodes"
    run(TOOL, FIXTURES / "inputs" / "opcodes", "--output", opcodes)
    opcode_report = json.loads((opcodes / "porpoise-report.json").read_text(encoding="utf-8"))
    semantic_mnemonics = {
        instruction["mnemonic"]
        for instruction in opcode_report["instructions"]
        if instruction["semantic_test"]
    }
    assert semantic_mnemonics == {
        ".4byte",
        "add", "addc", "adde", "addi", "addic.", "addze", "andc",
        "bctrl", "bdz", "beq+", "beqlr", "bgelr", "bgtlr", "bl", "bla",
        "ble+", "blelr", "blrl", "bltlr", "bne", "bne+", "bnelr",
        "clrlslwi", "clrlwi", "clrrwi",
        "cmpwi", "cntlzw", "crclr", "cror", "crset", "divw", "divw.",
        "divwu", "divwu.", "eqv", "extlwi", "extrwi", "extsb", "extsh", "fabs.",
        "fadd", "fadds", "fcmpo", "fcmpu", "fctiw", "fctiw.", "fctiwz", "fctiwz.", "fmadd",
        "fmadd.", "fmadds", "fmadds.", "fmr.", "fmsub", "fmsubs", "fnabs.",
        "fneg.", "fnmadd", "fnmadds", "fnmsub", "fnmsubs", "frsp", "frsp.",
        "fsel", "fsel.", "lbzx", "lfsx", "lhbrx", "lhzx", "li", "lis", "lmw", "mffs",
        "mffs.",
        "lwz", "lwzux", "lwzx", "mtctr", "mulhw", "mulhwu", "mullw", "neg",
        "mtfsb1", "mtfsb1.", "mtfsf", "mtfsf.", "nor", "orc", "ori",
        "ps_add", "ps_cmpo0", "ps_div", "ps_madd",
        "ps_madds0", "ps_madds1", "ps_merge00", "ps_merge01", "ps_merge10",
        "ps_merge11", "ps_mr", "ps_msub", "ps_muls0", "ps_muls1",
        "ps_neg", "ps_nmadd", "ps_nmsub", "ps_sel", "ps_sum0", "ps_sum1",
        "psq_l", "psq_lu", "psq_lux", "psq_lx", "psq_st", "psq_stu",
        "psq_stux", "psq_stx",
        "rlwinm", "rlwinm.", "rlwnm", "rotlw", "rotlwi", "rotrwi", "slw", "slwi", "sraw.", "srawi", "srwi",
        "srwi.", "stbx", "stfiwx", "stfsx", "sthbrx", "sthx", "stmw", "stw", "stwux",
        "stwx", "subfc", "subfe", "subfic", "subfze", "subfze.", "subi",
        "subic", "subic.", "subis", "sync",
    }
    assert opcode_report["summary"]["unsupported"] == 0
    for mnemonic in {"beqlr", "bnelr", "bgelr", "blelr", "bgtlr", "bltlr"}:
        entries = [
            instruction
            for instruction in opcode_report["instructions"]
            if instruction["mnemonic"] == mnemonic
        ]
        assert entries and all(instruction["status"] == "lowered" for instruction in entries)
        assert all(instruction["detail"] == "" for instruction in entries)
        assert all(instruction["semantic_test"] for instruction in entries)
    for mnemonic in {
        "bdz", "beq+", "bla", "ble+", "blrl", "bne+", "divw", "divw.", "divwu", "divwu.",
        "fctiw", "fctiw.", "fctiwz", "fctiwz.", "lhbrx", "mffs", "mffs.", "mtfsb1", "mtfsb1.",
        "eqv", "mtfsf", "mtfsf.", "mulhw", "mulhwu", "orc", "ps_cmpo0", "ps_merge00", "ps_merge01",
        "ps_merge10", "ps_merge11", "ps_mr", "ps_neg", "ps_sel", "rlwnm", "rotlw",
        "stfiwx", "sthbrx", "subfze", "subfze.",
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
    raw_entries = [
        instruction
        for instruction in opcode_report["instructions"]
        if instruction["mnemonic"] == ".4byte"
    ]
    assert [(entry["status"], entry["semantic_test"]) for entry in raw_entries] == [
        ("approximate", True),
        ("approximate", True),
    ]
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
    for mnemonic in {"ps_muls0", "ps_muls1", "ps_madds0", "ps_madds1"}:
        entries = [
            instruction
            for instruction in opcode_report["instructions"]
            if instruction["mnemonic"] == mnemonic
        ]
        assert entries and all(instruction["status"] == "lowered" for instruction in entries)
        assert all(instruction["detail"] == "" for instruction in entries)
        assert all(instruction["semantic_test"] for instruction in entries)
    paired_fused_detail = (
        "host arithmetic does not reproduce PPC paired-single Force25, fused rounding, "
        "exception, and FPSCR semantics"
    )
    for mnemonic in {
        "ps_madd", "ps_msub", "ps_nmadd", "ps_nmsub",
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
    assert extended_lifted_source.count(
        "porpoise_psq_transfer_is_exact("
    ) == 17
    assert (
        "porpoise_trace_approximate(state, UINT32_C(0x80006D00), \"psq_l\"); "
        "if (porpoise_state_has_fault(state)) return;"
    ) in extended_lifted_source
    assert (
        "if (!porpoise_fp_binary_single_try_exact(state, 3U, 1U, 2U, "
        "PORPOISE_FP_BINARY_ADD, 0)) {\n"
        "        porpoise_trace_approximate(state, UINT32_C(0x80006980), \"fadds\");\n"
        "        if (porpoise_state_has_fault(state)) return;"
    ) in extended_lifted_source
    assert extended_lifted_source.count("porpoise_ps_madds_scalar(") == 2
    assert extended_lifted_source.count("porpoise_ps_muls_scalar(") == 2
    for address, mnemonic in {
        "0x80006A10": "ps_madds0",
        "0x80006A14": "ps_madds1",
        "0x80006A18": "ps_muls0",
        "0x80006A1C": "ps_muls1",
    }.items():
        assert (
            f'porpoise_trace_approximate(state, UINT32_C({address}), "{mnemonic}")'
            not in extended_lifted_source
        )
    assert extended_lifted_source.count("porpoise_store_gx_fifo_u8(") == 1
    assert (
        "if (ea == UINT32_C(0xCC008000)) "
        "porpoise_store_gx_fifo_u8(state, (uint8_t)state->gpr[3]); else "
        "porpoise_store_u8(state, ea, (uint8_t)state->gpr[3]);"
    ) in extended_lifted_source
    assert (
        "porpoise_store_u8(state, ea, (uint8_t)state->gpr[4]);"
    ) in extended_lifted_source
    harness = opcodes / "tests" / "semantic_harness.c"
    harness.parent.mkdir(parents=True)
    harness.write_text(
        "#include <stdlib.h>\n"
        "#include \"porpoise_generated.h\"\n"
        "#include \"porpoise_libporpoise_adapter.h\"\n"
        "#include \"generated/extended_semantics.h\"\n"
        "#include \"generated/fp_integer_memory_semantics.h\"\n"
        "#include \"generated/raw_word_semantics.h\"\n"
        "#include \"generated/semantics.h\"\n"
        "#include <porpoise/stub.h>\n"
        "#define CHECK(condition) do { if (!(condition)) abort(); } while (0)\n"
        "int main(void) {\n"
        "  PorpoiseHostAdapter host; PorpoisePpcState state; unsigned int raw_index;\n"
        "  CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);\n"
        "  CHECK(porpoise_generated_bind(&host) == PORPOISE_HOST_OK);\n"
        "  porpoise_state_init(&state, &host); state.msr |= PORPOISE_MSR_FP; state.hid2 |= PORPOISE_HID2_PSE;\n"
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
        "  porpoise_fpr_set_bits(&state, 1U, 0U, UINT64_C(0x7FF8000020000000));\n"
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
        "  state.fpscr = 0U; porpoise_fpr_set_f64(&state, 1U, 0U, 1.5);\n"
        "  porpoise_fpr_set_bits(&state, 2U, 1U, UINT64_C(0x1020304050607080)); porpoise_fpr_set_bits(&state, 3U, 1U, UINT64_C(0x8070605040302010));\n"
        "  porpoise_lifted_scalar_fctiw_semantics(&state);\n"
        "  CHECK(!porpoise_state_has_fault(&state));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 2U, 0U) == UINT64_C(0xFFF8000000000002) && porpoise_fpr_get_bits(&state, 2U, 1U) == UINT64_C(0x1020304050607080));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 3U, 0U) == UINT64_C(0xFFF8000000000002) && porpoise_fpr_get_bits(&state, 3U, 1U) == UINT64_C(0x8070605040302010));\n"
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
        "  state.gpr[3] = 10U; CHECK(porpoise_libporpoise_run_guest(&state, UINT32_C(0x80006B04)));\n"
        "  CHECK(!porpoise_state_has_fault(&state) && state.gpr[3] == 14U);\n"
        "  porpoise_lifted_alias_branch_caller(&state); CHECK(state.gpr[3] == 25U);\n"
        "  state.gpr[3] = 0U; CHECK(porpoise_libporpoise_run_guest(&state, UINT32_C(0x80006B20)));\n"
        "  CHECK(!porpoise_state_has_fault(&state) && state.gpr[3] == 7U);\n"
        "  state.gpr[3] = 0U; porpoise_lifted_cross_label_owner(&state);\n"
        "  CHECK(state.gpr[3] == 105U);\n"
        "  state.gpr[3] = 40U; CHECK(porpoise_libporpoise_run_guest(&state, UINT32_C(0x80006BA4)));\n"
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
        "  state.gpr[3] = 0x12345678U; state.gpr[4] = 0x00FF00FFU; state.gpr[8] = 8U;\n"
        "  porpoise_lifted_integer_alias_semantics(&state);\n"
        "  CHECK(state.gpr[5] == 0xED34A978U && state.gpr[7] == 0x34567812U);\n"
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
        "  state.gpr[0] = UINT32_C(0xDEADBEEF); state.gpr[3] = UINT32_C(0x80000700); state.gpr[5] = 1U; state.gpr[7] = 7U; state.gpr[9] = UINT32_C(0x80000710);\n"
        "  porpoise_fpr_set_bits(&state, 4U, 0U, UINT64_C(0xAABBCCDD11223344)); porpoise_fpr_set_bits(&state, 4U, 1U, UINT64_C(0x5566778899AABBCC));\n"
        "  porpoise_store_u16(&state, UINT32_C(0x80000707), UINT16_C(0x1234)); porpoise_store_u16(&state, UINT32_C(0x80000710), UINT16_C(0xABCD));\n"
        "  porpoise_lifted_integer_word_memory_semantics(&state);\n"
        "  CHECK(!porpoise_state_has_fault(&state) && porpoise_load_u32(&state, UINT32_C(0x80000701)) == UINT32_C(0x11223344));\n"
        "  CHECK(state.gpr[6] == UINT32_C(0x3412) && state.gpr[8] == UINT32_C(0xCDAB));\n"
        "  CHECK(porpoise_fpr_get_bits(&state, 4U, 0U) == UINT64_C(0xAABBCCDD11223344) && porpoise_fpr_get_bits(&state, 4U, 1U) == UINT64_C(0x5566778899AABBCC));\n"
        "  porpoise_lifted_remaining_branch_hint_semantics(&state);\n"
        "  CHECK(state.gpr[4] == 0U);\n"
        "  state.pc = 0U; state.gpr[3] = 1U;\n"
        "  porpoise_lifted_predicted_not_equal_branch_semantics(&state);\n"
        "  CHECK(state.gpr[4] == 0U);\n"
        "  state.pc = 0U; state.gpr[3] = 0U;\n"
        "  porpoise_lifted_predicted_not_equal_branch_semantics(&state);\n"
        "  CHECK(state.gpr[4] == 1U);\n"
        "  state.pc = 0U; state.ctr = 1U;\n"
        "  porpoise_lifted_counter_zero_branch_semantics(&state);\n"
        "  CHECK(state.ctr == 0U && state.gpr[5] == 1U);\n"
        "  state.pc = 0U; state.ctr = 2U;\n"
        "  porpoise_lifted_counter_zero_branch_semantics(&state);\n"
        "  CHECK(state.ctr == 1U && state.gpr[5] == 2U);\n"
        "  state.pc = 0U;\n"
        "  porpoise_lifted_absolute_link_branch_semantics(&state);\n"
        "  CHECK(!porpoise_state_has_fault(&state) && state.gpr[6] == 0x33U);\n"
        "  CHECK(state.gpr[7] == 0x80006E88U && state.lr == 0x80006E88U);\n"
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
        "  porpoise_state_init(&state, &host);\n"
        "  for (raw_index = 0U; raw_index < 32U; raw_index++)\n"
        "    porpoise_store_u32(&state, 0x80000600U + raw_index * 4U, 0xA0000000U + raw_index);\n"
        "  CHECK(!porpoise_state_has_fault(&state)); state.gpr[3] = 0x80000600U;\n"
        "  porpoise_lifted_raw_lmw_overlap_semantics(&state);\n"
        "  CHECK(!porpoise_state_has_fault(&state));\n"
        "  for (raw_index = 0U; raw_index < 32U; raw_index++)\n"
        "    CHECK(state.gpr[raw_index] == 0xA0000000U + raw_index);\n"
        "  CHECK(state.pc == 0x80019014U);\n"
        "  porpoise_state_init(&state, &host);\n"
        "  porpoise_lifted_raw_invalid_encoding_semantics(&state);\n"
        "  CHECK(state.fault == PORPOISE_FAULT_ILLEGAL_INSTRUCTION);\n"
        "  CHECK(state.fault_address == 0x80019000U);\n"
        "  porpoise_libporpoise_adapter_shutdown(&host);\n"
        "  return 0;\n"
        "}\n",
        encoding="utf-8",
    )
    with (opcodes / "meson.build").open("a", encoding="utf-8") as meson_file:
        meson_file.write(
            "\nsemantic_harness = executable('semantic_harness', "
            "'tests/semantic_harness.c', "
            "include_directories: generated_private_inc, "
            "dependencies: porpoise_lifted_dep)\n"
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
    assert "PorpoiseHostPrepareRuntimeV1" in entry_source
    assert "PorpoiseHostPrepareTitleEntryV3" in entry_source
    assert entry_source.index("PorpoiseHostPrepareRuntimeV1") < (
        entry_source.index("porpoise_libporpoise_adapter_init_for_title")
    )
    assert entry_source.index("porpoise_libporpoise_adapter_init_for_title") < (
        entry_source.index("porpoise_generated_bind")
    )
    assert entry_source.index("porpoise_generated_bind") < (
        entry_source.index("porpoise_state_init(&state, &host)")
    )
    assert "porpoise_libporpoise_bind_guest_dispatch" not in entry_source
    assert "porpoise_call_address" not in entry_source
    assert "host.call_guest = porpoise_call_address" not in entry_source
    assert "title_state.startup_function_count" in entry_source
    assert "PORPOISE_TITLE_HOST_STARTUP_FUNCTION_CAPACITY" in entry_source
    assert "title_state.startup_functions[startup_function_index]" in entry_source
    assert ".guest_address" in entry_source
    assert ".flags" in entry_source
    assert "Porpoise title host returned a null startup function" in entry_source
    assert "Porpoise title host returned unknown startup-function flags" in entry_source
    assert "PORPOISE_TITLE_STARTUP_ESTABLISH_GUEST_MAIN_THREAD_AFTER" in entry_source
    assert "porpoise_libporpoise_bind_guest_main_thread(&state)" in entry_source
    assert entry_source.count("(void)porpoise_libporpoise_run_guest(") == 2
    assert entry_source.index("porpoise_initialize_data(&state)") < (
        entry_source.index("PorpoiseHostPrepareTitleEntryV3")
    )
    assert "porpoise_state_prepare_title_entry(&state)" in entry_source
    assert "__start" not in entry_source
    entry_meson = (with_entry / "meson.build").read_text(encoding="utf-8")
    assert "dependency('porpoise-title-host', fallback:" in entry_meson
    assert entry_meson.count("c_args: porpoise_consumer_c_args") == 2
    lifted_entry_source = (
        with_entry / "src" / "lifted" / "main.c"
    ).read_text(encoding="utf-8")
    assert "porpoise_psq_load" in lifted_entry_source
    add_stub(with_entry)
    run("meson", "setup", "build", "--wrap-mode=forcefallback", *CHILD_MESON_ARGS, cwd=with_entry)
    entry_compile_commands = json.loads(
        (with_entry / "build" / "compile_commands.json").read_text(
            encoding="utf-8"
        )
    )
    title_compile_commands = [
        item["command"]
        for item in entry_compile_commands
        if Path(item["file"]).as_posix().endswith("src/porpoise_entry.c")
    ]
    assert len(title_compile_commands) == 1
    assert_generated_consumer_command(title_compile_commands[0])
    run("meson", "compile", "-C", "build", cwd=with_entry)
    executable = with_entry / "build" / ("porpoise_title.exe" if os.name == "nt" else "porpoise_title")
    entry_environment = os.environ.copy()
    entry_environment["PORPOISE_STUB_ORDERED_STARTUP"] = "1"
    entry_result = run(executable, cwd=with_entry, env=entry_environment)
    assert entry_result.stderr == ""

    invalid_startup_modes = (
        ("too-many", "too many startup functions"),
        ("null", "null startup function"),
        ("unknown-flags", "unknown startup-function flags"),
    )
    for startup_mode, expected_message in invalid_startup_modes:
        invalid_startup_environment = os.environ.copy()
        invalid_startup_environment["PORPOISE_STUB_ORDERED_STARTUP"] = startup_mode
        invalid_startup_result = run(
            executable,
            cwd=with_entry,
            env=invalid_startup_environment,
            expected=2,
        )
        assert expected_message in invalid_startup_result.stderr

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

    unsupported_raw = temporary / "unsupported-raw.s"
    unsupported_raw.write_text(
        ".text\n.fn unsupported_raw, global\n"
        "/* 80006080 00000000  FF FF FF FF */ .4byte 0xFFFFFFFF\n"
        ".endfn unsupported_raw\n",
        encoding="utf-8",
    )
    unsupported_raw_result = run(
        TOOL,
        unsupported_raw,
        "--output",
        temporary / "unsupported-raw-output",
        expected=3,
    )
    assert "unsupported raw-word directive .4byte" in unsupported_raw_result.stderr

    mismatched_raw = temporary / "mismatched-raw.s"
    mismatched_raw.write_text(
        ".text\n.fn mismatched_raw, global\n"
        "/* 80006080 00000000  FF FF FF FF */ .4byte 0x00000000 /* invalid */\n"
        ".endfn mismatched_raw\n",
        encoding="utf-8",
    )
    mismatched_raw_result = run(
        TOOL,
        mismatched_raw,
        "--output",
        temporary / "mismatched-raw-output",
        expected=3,
    )
    assert "invalid raw-word directive .4byte" in mismatched_raw_result.stderr

    approximate_raw = temporary / "approximate-raw.s"
    approximate_raw.write_text(
        ".text\n.fn approximate_raw, global\n"
        "/* 80006080 00000000  B8 03 00 00 */ .4byte 0xB8030000 /* illegal: lmw r0, 0x0(r3) */\n"
        "/* 80006084 00000004  4E 80 00 20 */ blr\n"
        ".endfn approximate_raw\n",
        encoding="utf-8",
    )
    approximate_raw_output = temporary / "approximate-raw-output"
    approximate_raw_result = run(
        TOOL,
        approximate_raw,
        "--output",
        approximate_raw_output,
    )
    assert ".4byte directive uses approximate host semantics" in approximate_raw_result.stderr
    approximate_raw_strict = temporary / "approximate-raw-strict"
    run(
        TOOL,
        approximate_raw,
        "--output",
        approximate_raw_strict,
        "--strict",
        expected=3,
    )
    assert not approximate_raw_strict.exists()

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
        "rotlw-register-kind": "rotlw r3, r4, 8",
        "eqv-arity": "eqv r3, r4",
        "bdz-arity": "bdz cr0, target",
        "bla-misaligned-target": "bla 3",
        "conditional-return-operands": "beqlr cr0, extra",
        "paired-madd-arity": "ps_madd f3, f1, f2",
        "paired-compare-field": "ps_cmpo0 cr8, f1, f2",
        "frsp-arity": "frsp f2",
        "fctiw-arity": "fctiw f2",
        "stfiwx-register-kind": "stfiwx r3, r4, r5",
        "lhbrx-arity": "lhbrx r3, r4",
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
    imported_lifted_source = (
        imported / "src" / "lifted" / "calls.c"
    ).read_text(encoding="utf-8")
    assert (
        'porpoise_trace_call_enter(state, UINT32_C(0x80004000), '
        '"imported", "PorpoiseStubAdd");'
        in imported_lifted_source
    )
    assert (
        'porpoise_trace_call_exit(state, UINT32_C(0x80004000), '
        '"imported", "PorpoiseStubAdd");'
        in imported_lifted_source
    )
    abi_harness = imported / "tests" / "abi_harness.c"
    abi_harness.parent.mkdir(parents=True)
    abi_harness.write_text(
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include \"porpoise_generated.h\"\n"
        "#include \"porpoise_libporpoise_adapter.h\"\n"
        "#include \"generated/calls.h\"\n"
        "#include <porpoise/stub.h>\n"
        "#define CHECK(condition) do { if (!(condition)) abort(); } while (0)\n"
        "int main(void) {\n"
        "  PorpoiseHostAdapter host; PorpoisePpcState state; FILE *trace;\n"
        "  char trace_contents[8192]; size_t trace_size;\n"
        "  CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);\n"
        "  CHECK(porpoise_generated_bind(&host) == PORPOISE_HOST_OK);\n"
        "  porpoise_state_init(&state, &host);\n"
        "  trace = tmpfile(); CHECK(trace != NULL); state.trace_file = trace;\n"
        "  state.gpr[3] = 1U; state.gpr[4] = 2U;\n"
        "  porpoise_fpr_set_f64(&state, 1U, 0U, 1.5); porpoise_fpr_set_f64(&state, 2U, 0U, 2.25);\n"
        "  porpoise_lifted_call_imports(&state);\n"
        "  CHECK(!porpoise_state_has_fault(&state));\n"
        "  CHECK(state.gpr[3] == 0x80000003U);\n"
        "  CHECK(porpoise_fpr_get_f64(&state, 1U, 0U) == 3.75);\n"
        "  CHECK(PorpoiseStubReportCount() == 1U);\n"
        "  CHECK(state.trace_call_depth == 0U); CHECK(fflush(trace) == 0);\n"
        "  CHECK(fseek(trace, 0L, SEEK_SET) == 0);\n"
        "  trace_size = fread(trace_contents, 1U, sizeof(trace_contents) - 1U, trace);\n"
        "  trace_contents[trace_size] = '\\0';\n"
        "  CHECK(strstr(trace_contents, \"\\\"event\\\":\\\"call\\\",\\\"pc\\\":\\\"0x80004000\\\",\\\"function\\\":\\\"PorpoiseStubAdd\\\"\") != NULL);\n"
        "  CHECK(strstr(trace_contents, \"\\\"phase\\\":\\\"enter\\\",\\\"address\\\":\\\"0x80004000\\\",\\\"kind\\\":\\\"imported\\\"\") != NULL);\n"
        "  CHECK(strstr(trace_contents, \"\\\"phase\\\":\\\"exit\\\",\\\"address\\\":\\\"0x80004000\\\",\\\"kind\\\":\\\"imported\\\"\") != NULL);\n"
        "  CHECK(strstr(trace_contents, \"\\\"stack\\\":[\\\"0x80004000\\\"]\") != NULL);\n"
        "  porpoise_trace_close(&state);\n"
        "  porpoise_libporpoise_adapter_shutdown(&host);\n"
        "  return 0;\n"
        "}\n",
        encoding="utf-8",
    )
    with (imported / "meson.build").open("a", encoding="utf-8") as meson_file:
        meson_file.write(
            "\nabi_harness = executable('abi_harness', "
            "'tests/abi_harness.c', "
            "include_directories: generated_private_inc, "
            "dependencies: porpoise_lifted_dep)\n"
        )
    add_stub(imported)
    run("meson", "setup", "build", "--wrap-mode=forcefallback", *CHILD_MESON_ARGS, cwd=imported)
    run("meson", "compile", "-C", "build", cwd=imported)
    abi_executable = imported / "build" / ("abi_harness.exe" if os.name == "nt" else "abi_harness")
    run(abi_executable, cwd=imported)

    skipped_import_input = temporary / "skipped-import.s"
    skipped_import_input.write_text(
        ".text\n.global HostAdd\n"
        ".fn HostAdd, global\n"
        "/* 80007000 00000000  38 63 00 63 */ addi r3, r3, 99\n"
        "/* 80007004 00000004  4E 80 00 20 */ blr\n"
        ".endfn HostAdd\n\n"
        ".global call_host_by_name\n.fn call_host_by_name, global\n"
        "/* 80007100 00000100  7C 08 02 A6 */ mflr r0\n"
        "/* 80007104 00000104  38 60 00 04 */ li r3, 4\n"
        "/* 80007108 00000108  38 80 00 05 */ li r4, 5\n"
        "/* 8000710C 0000010C  4B FF FE F5 */ bl HostAdd\n"
        "/* 80007110 00000110  7C 08 03 A6 */ mtlr r0\n"
        "/* 80007114 00000114  4E 80 00 20 */ blr\n"
        ".endfn call_host_by_name\n\n"
        ".global call_host_by_numeric\n.fn call_host_by_numeric, global\n"
        "/* 80007200 00000200  7C 08 02 A6 */ mflr r0\n"
        "/* 80007204 00000204  38 60 00 06 */ li r3, 6\n"
        "/* 80007208 00000208  38 80 00 07 */ li r4, 7\n"
        "/* 8000720C 0000020C  4B FF FD F5 */ bl 0x80007000\n"
        "/* 80007210 00000210  7C 08 03 A6 */ mtlr r0\n"
        "/* 80007214 00000214  4E 80 00 20 */ blr\n"
        ".endfn call_host_by_numeric\n\n"
        ".global call_host_by_ctr\n.fn call_host_by_ctr, global\n"
        "/* 80007300 00000300  7C 08 02 A6 */ mflr r0\n"
        "/* 80007304 00000304  38 60 00 08 */ li r3, 8\n"
        "/* 80007308 00000308  38 80 00 09 */ li r4, 9\n"
        "/* 8000730C 0000030C  3D 80 80 00 */ lis r12, -32768\n"
        "/* 80007310 00000310  61 8C 70 00 */ ori r12, r12, 0x7000\n"
        "/* 80007314 00000314  7D 89 03 A6 */ mtctr r12\n"
        "/* 80007318 00000318  4E 80 04 21 */ bctrl\n"
        "/* 8000731C 0000031C  7C 08 03 A6 */ mtlr r0\n"
        "/* 80007320 00000320  4E 80 00 20 */ blr\n"
        ".endfn call_host_by_ctr\n",
        encoding="utf-8",
    )
    skipped_import_abi = temporary / "skipped-import.json"
    skipped_import_abi.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "functions": [
                    {
                        "kind": "import",
                        "symbol": "HostAdd",
                        "wrapper": "PorpoiseStubAdd",
                        "header": "porpoise/stub.h",
                        "return": {"type": "u32", "register": "r3"},
                        "arguments": [
                            {"name": "left", "type": "u32", "register": "r3"},
                            {"name": "right", "type": "u32", "register": "r4"},
                        ],
                    }
                ],
            }
        ),
        encoding="utf-8",
    )
    skipped_import_list = temporary / "skipped-import.txt"
    skipped_import_list.write_text("HostAdd\n", encoding="utf-8")
    skipped_imported = temporary / "skipped-imported"
    run(
        TOOL,
        skipped_import_input,
        "--output",
        skipped_imported,
        "--abi",
        skipped_import_abi,
        "--skip-list",
        skipped_import_list,
    )
    skipped_registry = (
        skipped_imported / "src" / "porpoise_function_registry_8000.c"
    ).read_text(encoding="utf-8")
    assert (
        "case UINT32_C(0x80007000): return "
        "(struct porpoise_dispatch_target){porpoise_import_HostAdd, "
        'PORPOISE_DISPATCH_IMPORT, "HostAdd"};'
        in skipped_registry
    )
    assert "porpoise_lifted_HostAdd" not in skipped_registry
    skipped_report = json.loads(
        (skipped_imported / "porpoise-report.json").read_text(encoding="utf-8")
    )
    assert next(
        function["status"]
        for function in skipped_report["functions"]
        if function["symbol"] == "HostAdd"
    ) == "imported"
    skipped_harness = skipped_imported / "tests" / "skipped_import_harness.c"
    skipped_harness.parent.mkdir(parents=True)
    skipped_harness.write_text(
        "#include <stdlib.h>\n"
        "#include \"porpoise_generated.h\"\n"
        "#include \"porpoise_libporpoise_adapter.h\"\n"
        "#include \"generated/skipped_import.h\"\n"
        "#define CHECK(condition) do { if (!(condition)) abort(); } while (0)\n"
        "static unsigned int poll_count;\n"
        "static int poll_should_fail;\n"
        "static PorpoiseHostResult count_events(void *context, PorpoisePpcState *state) {\n"
        "  (void)context; CHECK(state->lifted_call_depth == 0U); poll_count++;\n"
        "  return poll_should_fail ? PORPOISE_HOST_IO_ERROR : PORPOISE_HOST_OK;\n"
        "}\n"
        "int main(void) {\n"
        "  PorpoiseHostAdapter host; PorpoisePpcState state;\n"
        "  CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);\n"
        "  CHECK(porpoise_generated_bind(&host) == PORPOISE_HOST_OK);\n"
        "  porpoise_state_init(&state, &host);\n"
        "  porpoise_lifted_call_host_by_name(&state); CHECK(state.gpr[3] == 9U);\n"
        "  porpoise_lifted_call_host_by_numeric(&state); CHECK(state.gpr[3] == 13U);\n"
        "  porpoise_lifted_call_host_by_ctr(&state); CHECK(state.gpr[3] == 17U);\n"
        "  CHECK(!porpoise_state_has_fault(&state)); CHECK(poll_count == 0U);\n"
        "  host.poll_events = count_events; state.msr |= PORPOISE_MSR_EE;\n"
        "  CHECK(porpoise_libporpoise_run_guest(&state, UINT32_C(0x80007100)));\n"
        "  CHECK(poll_count == 1U && state.lifted_call_depth == 0U);\n"
        "  CHECK(porpoise_libporpoise_run_guest(&state, UINT32_C(0x80007000)));\n"
        "  CHECK(poll_count == 1U && state.lifted_call_depth == 0U);\n"
        "  state.msr &= ~PORPOISE_MSR_EE;\n"
        "  CHECK(porpoise_libporpoise_run_guest(&state, UINT32_C(0x80007100)));\n"
        "  CHECK(poll_count == 1U); state.msr |= PORPOISE_MSR_EE;\n"
        "  poll_should_fail = 1;\n"
        "  CHECK(!porpoise_libporpoise_run_guest(&state, UINT32_C(0x80007100)));\n"
        "  CHECK(state.fault == PORPOISE_FAULT_HOST_IO);\n"
        "  CHECK(state.lifted_call_depth == 0U && poll_count == 2U);\n"
        "  porpoise_state_clear_fault(&state); poll_should_fail = 0;\n"
        "  state.lifted_call_depth = UINT32_MAX;\n"
        "  CHECK(!porpoise_libporpoise_run_guest(&state, UINT32_C(0x80007100)));\n"
        "  CHECK(state.fault == PORPOISE_FAULT_INVALID_STATE);\n"
        "  CHECK(state.lifted_call_depth == UINT32_MAX && poll_count == 2U);\n"
        "  porpoise_state_clear_fault(&state); state.lifted_call_depth = 0U;\n"
        "  porpoise_libporpoise_adapter_shutdown(&host);\n"
        "  return 0;\n"
        "}\n",
        encoding="utf-8",
    )
    with (skipped_imported / "meson.build").open("a", encoding="utf-8") as meson_file:
        meson_file.write(
            "\nskipped_import_harness = executable('skipped_import_harness', "
            "'tests/skipped_import_harness.c', "
            "include_directories: generated_private_inc, "
            "dependencies: porpoise_lifted_dep)\n"
        )
    add_stub(skipped_imported)
    run(
        "meson",
        "setup",
        "build",
        "--wrap-mode=forcefallback",
        *CHILD_MESON_ARGS,
        cwd=skipped_imported,
    )
    run("meson", "compile", "-C", "build", cwd=skipped_imported)
    skipped_executable = skipped_imported / "build" / (
        "skipped_import_harness.exe" if os.name == "nt" else "skipped_import_harness"
    )
    run(skipped_executable, cwd=skipped_imported)

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
        "#include \"generated/terminal_import.h\"\n"
        "#include \"terminal_import_adapter.h\"\n"
        "#define CHECK(condition) do { if (!(condition)) abort(); } while (0)\n"
        "void TerminalImportAdapter(PorpoisePpcState *state) { state->status = PORPOISE_EXECUTION_RETURNED; }\n"
        "int main(void) {\n"
        "  PorpoiseHostAdapter host; PorpoisePpcState state;\n"
        "  CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);\n"
        "  CHECK(porpoise_generated_bind(&host) == PORPOISE_HOST_OK);\n"
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
            "'tests/terminal_harness.c', "
            "include_directories: generated_private_inc, "
            "dependencies: porpoise_lifted_dep)\n"
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

    def gpr(value_type, register):
        return {"type": value_type, "register": f"r{register}"}

    def fpr(value_type, register):
        return {"type": value_type, "register": f"f{register}"}

    builtin_adapter_contracts = (
        (
            "porpoise_libporpoise_ai_init_adapter",
            {"type": "void"},
            [gpr("pointer", 3)],
        ),
        (
            "porpoise_libporpoise_ar_alloc_adapter",
            gpr("u32", 3),
            [gpr("u32", 3)],
        ),
        (
            "porpoise_libporpoise_ar_free_adapter",
            gpr("u32", 3),
            [gpr("pointer", 3)],
        ),
        (
            "porpoise_libporpoise_ar_get_size_adapter",
            gpr("u32", 3),
            [],
        ),
        (
            "porpoise_libporpoise_ar_init_adapter",
            gpr("u32", 3),
            [gpr("pointer", 3), gpr("u32", 4)],
        ),
        (
            "porpoise_libporpoise_ar_reset_adapter",
            {"type": "void"},
            [],
        ),
        (
            "porpoise_libporpoise_arq_post_request_adapter",
            {"type": "void"},
            [gpr("pointer", 3)]
            + [gpr("u32", register) for register in range(4, 11)],
        ),
        (
            "porpoise_libporpoise_card_probe_ex_adapter",
            gpr("s32", 3),
            [gpr("s32", 3), gpr("pointer", 4), gpr("pointer", 5)],
        ),
        (
            "porpoise_libporpoise_dsp_add_task_adapter",
            gpr("pointer", 3),
            [gpr("pointer", 3)],
        ),
        (
            "porpoise_libporpoise_demo_pad_init_adapter",
            {"type": "void"},
            [],
        ),
        (
            "porpoise_libporpoise_gx_init_adapter",
            gpr("pointer", 3),
            [gpr("pointer", 3), gpr("u32", 4)],
        ),
        (
            "porpoise_libporpoise_gx_set_draw_done_callback_adapter",
            gpr("pointer", 3),
            [gpr("pointer", 3)],
        ),
        (
            "porpoise_libporpoise_gx_set_copy_filter_adapter",
            {"type": "void"},
            [
                gpr("u8", 3),
                gpr("pointer", 4),
                gpr("u8", 5),
                gpr("pointer", 6),
            ],
        ),
        (
            "porpoise_libporpoise_gx_set_copy_clear_adapter",
            {"type": "void"},
            [gpr("pointer", 3), gpr("u32", 4)],
        ),
        (
            "porpoise_libporpoise_gx_set_disp_copy_dst_adapter",
            {"type": "void"},
            [gpr("u16", 3), gpr("u16", 4)],
        ),
        (
            "porpoise_libporpoise_gx_set_tex_copy_dst_adapter",
            {"type": "void"},
            [
                gpr("u16", 3),
                gpr("u16", 4),
                gpr("u32", 5),
                gpr("u8", 6),
            ],
        ),
        (
            "porpoise_libporpoise_gx_copy_disp_adapter",
            {"type": "void"},
            [gpr("pointer", 3), gpr("u8", 4)],
        ),
        (
            "porpoise_libporpoise_gx_copy_tex_adapter",
            {"type": "void"},
            [gpr("pointer", 3), gpr("u8", 4)],
        ),
        (
            "porpoise_libporpoise_gx_load_light_obj_imm_adapter",
            {"type": "void"},
            [gpr("pointer", 3), gpr("u32", 4)],
        ),
        (
            "porpoise_libporpoise_gx_set_array_adapter",
            {"type": "void"},
            [gpr("u32", 3), gpr("pointer", 4), gpr("u8", 5)],
        ),
        (
            "porpoise_libporpoise_gx_load_tex_obj_adapter",
            {"type": "void"},
            [gpr("pointer", 3), gpr("u32", 4)],
        ),
        (
            "porpoise_libporpoise_gx_load_tlut_adapter",
            {"type": "void"},
            [gpr("pointer", 3), gpr("u32", 4)],
        ),
        (
            "porpoise_libporpoise_gx_set_chan_amb_color_adapter",
            {"type": "void"},
            [gpr("u32", 3), gpr("pointer", 4)],
        ),
        (
            "porpoise_libporpoise_gx_set_chan_mat_color_adapter",
            {"type": "void"},
            [gpr("u32", 3), gpr("pointer", 4)],
        ),
        (
            "porpoise_libporpoise_gx_call_display_list_adapter",
            {"type": "void"},
            [gpr("pointer", 3), gpr("u32", 4)],
        ),
        (
            "porpoise_libporpoise_gx_set_projection_adapter",
            {"type": "void"},
            [gpr("pointer", 3), gpr("u32", 4)],
        ),
        (
            "porpoise_libporpoise_gx_get_projectionv_adapter",
            {"type": "void"},
            [gpr("pointer", 3)],
        ),
        (
            "porpoise_libporpoise_gx_load_pos_mtx_imm_adapter",
            {"type": "void"},
            [gpr("pointer", 3), gpr("u32", 4)],
        ),
        (
            "porpoise_libporpoise_gx_load_nrm_mtx_imm_adapter",
            {"type": "void"},
            [gpr("pointer", 3), gpr("u32", 4)],
        ),
        (
            "porpoise_libporpoise_gx_load_tex_mtx_imm_adapter",
            {"type": "void"},
            [gpr("pointer", 3), gpr("u32", 4), gpr("u32", 5)],
        ),
        (
            "porpoise_libporpoise_gx_get_viewportv_adapter",
            {"type": "void"},
            [gpr("pointer", 3)],
        ),
        (
            "porpoise_libporpoise_gx_set_ind_tex_mtx_adapter",
            {"type": "void"},
            [gpr("u32", 3), gpr("pointer", 4), gpr("s8", 5)],
        ),
        (
            "porpoise_libporpoise_gx_set_tev_color_adapter",
            {"type": "void"},
            [gpr("u32", 3), gpr("pointer", 4)],
        ),
        (
            "porpoise_libporpoise_gx_set_tev_color_s10_adapter",
            {"type": "void"},
            [gpr("u32", 3), gpr("pointer", 4)],
        ),
        (
            "porpoise_libporpoise_gx_set_tev_kcolor_adapter",
            {"type": "void"},
            [gpr("u32", 3), gpr("pointer", 4)],
        ),
        (
            "porpoise_libporpoise_gx_set_fog_adapter",
            {"type": "void"},
            [
                gpr("u32", 3),
                fpr("f32", 1),
                fpr("f32", 2),
                fpr("f32", 3),
                fpr("f32", 4),
                gpr("pointer", 4),
            ],
        ),
        (
            "porpoise_libporpoise_gx_set_fog_range_adj_adapter",
            {"type": "void"},
            [gpr("u8", 3), gpr("u16", 4), gpr("pointer", 5)],
        ),
        (
            "porpoise_libporpoise_gx_set_tev_indirect_adapter",
            {"type": "void"},
            [gpr("u32", register) for register in range(3, 10)]
            + [gpr("u8", 10)],
        ),
        (
            "porpoise_libporpoise_dvd_cancel_adapter",
            gpr("s32", 3),
            [gpr("pointer", 3)],
        ),
        (
            "porpoise_libporpoise_dvd_close_adapter",
            gpr("s32", 3),
            [gpr("pointer", 3)],
        ),
        (
            "porpoise_libporpoise_dvd_convert_path_to_entry_adapter",
            gpr("s32", 3),
            [gpr("pointer", 3)],
        ),
        (
            "porpoise_libporpoise_dvd_fast_open_adapter",
            gpr("s32", 3),
            [gpr("s32", 3), gpr("pointer", 4)],
        ),
        (
            "porpoise_libporpoise_dvd_get_command_block_status_adapter",
            gpr("s32", 3),
            [gpr("pointer", 3)],
        ),
        (
            "porpoise_libporpoise_dvd_init_adapter",
            {"type": "void"},
            [],
        ),
        (
            "porpoise_libporpoise_dvd_open_adapter",
            gpr("s32", 3),
            [gpr("pointer", 3), gpr("pointer", 4)],
        ),
        (
            "porpoise_libporpoise_dvd_read_prio_adapter",
            gpr("s32", 3),
            [
                gpr("pointer", 3),
                gpr("pointer", 4),
                gpr("s32", 5),
                gpr("s32", 6),
                gpr("s32", 7),
            ],
        ),
        (
            "porpoise_libporpoise_os_alloc_from_arena_hi_adapter",
            gpr("pointer", 3),
            [gpr("u32", 3), gpr("u32", 4)],
        ),
        (
            "porpoise_libporpoise_os_alloc_from_arena_lo_adapter",
            gpr("pointer", 3),
            [gpr("u32", 3), gpr("u32", 4)],
        ),
        (
            "porpoise_libporpoise_os_exit_thread_adapter",
            {"type": "void"},
            [gpr("pointer", 3)],
        ),
        (
            "porpoise_libporpoise_os_get_arena_hi_adapter",
            gpr("pointer", 3),
            [],
        ),
        (
            "porpoise_libporpoise_os_get_arena_lo_adapter",
            gpr("pointer", 3),
            [],
        ),
        (
            "porpoise_libporpoise_os_get_current_thread_adapter",
            gpr("pointer", 3),
            [],
        ),
        (
            "porpoise_libporpoise_os_init_message_queue_adapter",
            {"type": "void"},
            [gpr("pointer", 3), gpr("pointer", 4), gpr("s32", 5)],
        ),
        (
            "porpoise_libporpoise_os_init_adapter",
            {"type": "void"},
            [],
        ),
        (
            "porpoise_libporpoise_os_receive_message_adapter",
            gpr("s32", 3),
            [gpr("pointer", 3), gpr("pointer", 4), gpr("s32", 5)],
        ),
        (
            "porpoise_libporpoise_os_report_adapter",
            {"type": "void"},
            [gpr("pointer", 3)],
        ),
        (
            "porpoise_libporpoise_os_resume_thread_adapter",
            gpr("s32", 3),
            [gpr("pointer", 3)],
        ),
        (
            "porpoise_libporpoise_os_send_message_adapter",
            gpr("s32", 3),
            [gpr("pointer", 3), gpr("pointer", 4), gpr("s32", 5)],
        ),
        (
            "porpoise_libporpoise_os_set_arena_hi_adapter",
            {"type": "void"},
            [gpr("pointer", 3)],
        ),
        (
            "porpoise_libporpoise_os_set_arena_lo_adapter",
            {"type": "void"},
            [gpr("pointer", 3)],
        ),
        (
            "porpoise_libporpoise_os_sleep_thread_adapter",
            {"type": "void"},
            [gpr("pointer", 3)],
        ),
        (
            "porpoise_libporpoise_os_suspend_thread_adapter",
            gpr("s32", 3),
            [gpr("pointer", 3)],
        ),
        (
            "porpoise_libporpoise_os_wakeup_thread_adapter",
            {"type": "void"},
            [gpr("pointer", 3)],
        ),
        (
            "porpoise_libporpoise_vi_configure_adapter",
            {"type": "void"},
            [gpr("pointer", 3)],
        ),
        (
            "porpoise_libporpoise_vi_init_adapter",
            {"type": "void"},
            [],
        ),
        (
            "porpoise_libporpoise_vi_set_next_frame_buffer_adapter",
            {"type": "void"},
            [gpr("pointer", 3)],
        ),
        (
            "porpoise_libporpoise_vi_wait_for_retrace_adapter",
            {"type": "void"},
            [],
        ),
    )
    builtin_contracts_by_adapter = {
        contract[0]: contract for contract in builtin_adapter_contracts
    }
    protected_native_callables = (
        ("AIInit", "porpoise_libporpoise_ai_init_adapter"),
        ("ARAlloc", "porpoise_libporpoise_ar_alloc_adapter"),
        ("ARFree", "porpoise_libporpoise_ar_free_adapter"),
        ("ARGetSize", "porpoise_libporpoise_ar_get_size_adapter"),
        ("ARInit", "porpoise_libporpoise_ar_init_adapter"),
        ("ARReset", "porpoise_libporpoise_ar_reset_adapter"),
        ("ARQPostRequest", "porpoise_libporpoise_arq_post_request_adapter"),
        ("CARDProbeEx", "porpoise_libporpoise_card_probe_ex_adapter"),
        ("DSPAddTask", "porpoise_libporpoise_dsp_add_task_adapter"),
        (
            "DEMOPadInit",
            "porpoise_libporpoise_demo_pad_init_adapter",
        ),
        ("GXInit", "porpoise_libporpoise_gx_init_adapter"),
        (
            "GXSetDrawDoneCallback",
            "porpoise_libporpoise_gx_set_draw_done_callback_adapter",
        ),
        (
            "GXSetCopyFilter",
            "porpoise_libporpoise_gx_set_copy_filter_adapter",
        ),
        (
            "GXSetCopyClear",
            "porpoise_libporpoise_gx_set_copy_clear_adapter",
        ),
        (
            "GXSetDispCopyDst",
            "porpoise_libporpoise_gx_set_disp_copy_dst_adapter",
        ),
        (
            "GXSetTexCopyDst",
            "porpoise_libporpoise_gx_set_tex_copy_dst_adapter",
        ),
        ("GXCopyDisp", "porpoise_libporpoise_gx_copy_disp_adapter"),
        ("GXCopyTex", "porpoise_libporpoise_gx_copy_tex_adapter"),
        (
            "GXLoadLightObjImm",
            "porpoise_libporpoise_gx_load_light_obj_imm_adapter",
        ),
        ("GXSetArray", "porpoise_libporpoise_gx_set_array_adapter"),
        (
            "GXLoadTexObj",
            "porpoise_libporpoise_gx_load_tex_obj_adapter",
        ),
        ("GXLoadTlut", "porpoise_libporpoise_gx_load_tlut_adapter"),
        (
            "GXSetChanAmbColor",
            "porpoise_libporpoise_gx_set_chan_amb_color_adapter",
        ),
        (
            "GXSetChanMatColor",
            "porpoise_libporpoise_gx_set_chan_mat_color_adapter",
        ),
        (
            "GXCallDisplayList",
            "porpoise_libporpoise_gx_call_display_list_adapter",
        ),
        (
            "GXSetProjection",
            "porpoise_libporpoise_gx_set_projection_adapter",
        ),
        (
            "GXGetProjectionv",
            "porpoise_libporpoise_gx_get_projectionv_adapter",
        ),
        (
            "GXLoadPosMtxImm",
            "porpoise_libporpoise_gx_load_pos_mtx_imm_adapter",
        ),
        (
            "GXLoadNrmMtxImm",
            "porpoise_libporpoise_gx_load_nrm_mtx_imm_adapter",
        ),
        (
            "GXLoadTexMtxImm",
            "porpoise_libporpoise_gx_load_tex_mtx_imm_adapter",
        ),
        (
            "GXGetViewportv",
            "porpoise_libporpoise_gx_get_viewportv_adapter",
        ),
        (
            "GXSetIndTexMtx",
            "porpoise_libporpoise_gx_set_ind_tex_mtx_adapter",
        ),
        (
            "GXSetTevColor",
            "porpoise_libporpoise_gx_set_tev_color_adapter",
        ),
        (
            "GXSetTevColorS10",
            "porpoise_libporpoise_gx_set_tev_color_s10_adapter",
        ),
        (
            "GXSetTevKColor",
            "porpoise_libporpoise_gx_set_tev_kcolor_adapter",
        ),
        ("GXSetFog", "porpoise_libporpoise_gx_set_fog_adapter"),
        (
            "GXSetFogRangeAdj",
            "porpoise_libporpoise_gx_set_fog_range_adj_adapter",
        ),
        (
            "GXSetTevIndirect",
            "porpoise_libporpoise_gx_set_tev_indirect_adapter",
        ),
        ("DVDCancel", "porpoise_libporpoise_dvd_cancel_adapter"),
        ("DVDClose", "porpoise_libporpoise_dvd_close_adapter"),
        (
            "DVDConvertPathToEntrynum",
            "porpoise_libporpoise_dvd_convert_path_to_entry_adapter",
        ),
        ("DVDFastOpen", "porpoise_libporpoise_dvd_fast_open_adapter"),
        (
            "DVDGetCommandBlockStatus",
            "porpoise_libporpoise_dvd_get_command_block_status_adapter",
        ),
        ("DVDInit", "porpoise_libporpoise_dvd_init_adapter"),
        ("DVDOpen", "porpoise_libporpoise_dvd_open_adapter"),
        ("DVDReadPrio", "porpoise_libporpoise_dvd_read_prio_adapter"),
        (
            "OSAllocFromArenaHi",
            "porpoise_libporpoise_os_alloc_from_arena_hi_adapter",
        ),
        (
            "OSAllocFromArenaLo",
            "porpoise_libporpoise_os_alloc_from_arena_lo_adapter",
        ),
        ("OSExitThread", "porpoise_libporpoise_os_exit_thread_adapter"),
        ("OSGetArenaHi", "porpoise_libporpoise_os_get_arena_hi_adapter"),
        ("OSGetArenaLo", "porpoise_libporpoise_os_get_arena_lo_adapter"),
        (
            "OSGetCurrentThread",
            "porpoise_libporpoise_os_get_current_thread_adapter",
        ),
        (
            "OSInitMessageQueue",
            "porpoise_libporpoise_os_init_message_queue_adapter",
        ),
        ("OSInit", "porpoise_libporpoise_os_init_adapter"),
        (
            "OSReceiveMessage",
            "porpoise_libporpoise_os_receive_message_adapter",
        ),
        ("OSReport", "porpoise_libporpoise_os_report_adapter"),
        ("OSResumeThread", "porpoise_libporpoise_os_resume_thread_adapter"),
        ("OSSendMessage", "porpoise_libporpoise_os_send_message_adapter"),
        ("OSSetArenaHi", "porpoise_libporpoise_os_set_arena_hi_adapter"),
        ("OSSetArenaLo", "porpoise_libporpoise_os_set_arena_lo_adapter"),
        ("OSSleepThread", "porpoise_libporpoise_os_sleep_thread_adapter"),
        (
            "OSSuspendThread",
            "porpoise_libporpoise_os_suspend_thread_adapter",
        ),
        ("OSWakeupThread", "porpoise_libporpoise_os_wakeup_thread_adapter"),
        ("VIConfigure", "porpoise_libporpoise_vi_configure_adapter"),
        ("VIInit", "porpoise_libporpoise_vi_init_adapter"),
        (
            "VISetNextFrameBuffer",
            "porpoise_libporpoise_vi_set_next_frame_buffer_adapter",
        ),
        (
            "VIWaitForRetrace",
            "porpoise_libporpoise_vi_wait_for_retrace_adapter",
        ),
    )
    assert {adapter for _, adapter in protected_native_callables} == set(
        builtin_contracts_by_adapter
    )
    for adapter in builtin_contracts_by_adapter:
        assert adapter not in public_adapter_header

    def builtin_manifest(adapter, result, arguments):
        return {
            "schema_version": 1,
            "functions": [
                {
                    "kind": "import",
                    # Built-in adapters bind by adapter name; guest symbols may
                    # use any valid alias declared by the input assembly.
                    "symbol": "TerminalHostCall",
                    "adapter": adapter,
                    "header": "porpoise_libporpoise_builtins_private.h",
                    "return": result,
                    "arguments": arguments,
                }
            ],
        }

    for adapter_index, (adapter, result, arguments) in enumerate(
        builtin_adapter_contracts
    ):
        adapter_abi = temporary / f"builtin-adapter-{adapter_index}.json"
        adapter_abi.write_text(
            json.dumps(builtin_manifest(adapter, result, arguments)),
            encoding="utf-8",
        )
        adapter_output = temporary / f"builtin-adapter-{adapter_index}-output"
        run(
            TOOL,
            terminal_import_input,
            "--output",
            adapter_output,
            "--abi",
            adapter_abi,
        )
        import_source = (
            adapter_output / "src" / "porpoise_imports.c"
        ).read_text(encoding="utf-8")
        assert f"{adapter}(state)" in import_source
        assert import_source.count(
            '#include "porpoise_libporpoise_builtins_private.h"'
        ) == 1
        assert "#include <porpoise_libporpoise_adapter.h>" not in import_source
        adapter_meson = (adapter_output / "meson.build").read_text(
            encoding="utf-8"
        )
        if adapter == "porpoise_libporpoise_vi_wait_for_retrace_adapter":
            assert "'c', 'cpp'" in adapter_meson
            assert "'src/porpoise_libporpoise_presentation.cpp'" in adapter_meson
            assert "cpp_args: porpoise_consumer_c_args" in adapter_meson
        else:
            assert "'src/porpoise_libporpoise_presentation.cpp'" not in adapter_meson

    def reject_builtin_manifest(
        label, manifest, diagnostic=None, input_path=terminal_import_input
    ):
        manifest_path = temporary / f"builtin-adapter-{label}.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        completed = run(
            TOOL,
            input_path,
            "--output",
            temporary / f"builtin-adapter-{label}-output",
            "--abi",
            manifest_path,
            expected=2,
        )
        if diagnostic is not None:
            assert diagnostic in completed.stderr
        return completed

    def write_import_caller(path, function_name, symbols):
        lines = [
            ".text\n",
            f".global {function_name}\n",
            f".fn {function_name}, global\n",
        ]
        start_address = 0x80004200
        for symbol_index, symbol in enumerate(symbols):
            address = start_address + symbol_index * 4
            offset = symbol_index * 4
            lines.append(
                f"/* {address:08X} {offset:08X}  48 00 00 01 */ bl {symbol}\n"
            )
        final_index = len(symbols)
        lines.append(
            f"/* {start_address + final_index * 4:08X} "
            f"{final_index * 4:08X}  4E 80 00 20 */ blr\n"
        )
        lines.append(f".endfn {function_name}\n")
        path.write_text("".join(lines), encoding="utf-8")

    protected_native_input = temporary / "protected-native-imports.s"
    write_import_caller(
        protected_native_input,
        "protected_native_caller",
        [native for native, _ in protected_native_callables],
    )

    canonical_native_manifest = {"schema_version": 1, "functions": []}
    for native, adapter in protected_native_callables:
        _, result, arguments = builtin_contracts_by_adapter[adapter]
        canonical_native_manifest["functions"].append(
            {
                "kind": "import",
                "symbol": native,
                "adapter": adapter,
                "header": "porpoise_libporpoise_builtins_private.h",
                "return": result,
                "arguments": arguments,
            }
        )
    canonical_native_abi = temporary / "canonical-native-imports.json"
    canonical_native_abi.write_text(
        json.dumps(canonical_native_manifest), encoding="utf-8"
    )
    canonical_native_output = temporary / "canonical-native-imports-output"
    run(
        TOOL,
        protected_native_input,
        "--output",
        canonical_native_output,
        "--abi",
        canonical_native_abi,
    )
    canonical_native_source = (
        canonical_native_output / "src" / "porpoise_imports.c"
    ).read_text(encoding="utf-8")
    for _, adapter in protected_native_callables:
        assert f"{adapter}(state)" in canonical_native_source

    default_native_manifest = {"schema_version": 1, "functions": []}
    for native, adapter in protected_native_callables:
        _, result, arguments = builtin_contracts_by_adapter[adapter]
        default_native_manifest["functions"].append(
            {
                "kind": "import",
                "symbol": native,
                "header": "porpoise/stub.h",
                "return": result,
                "arguments": arguments,
            }
        )
    default_native_result = reject_builtin_manifest(
        "protected-default-native-callables",
        default_native_manifest,
        input_path=protected_native_input,
    )
    for native, adapter in protected_native_callables:
        assert (
            f"ABI import {native} must use built-in adapter {adapter}"
            in default_native_result.stderr
        )

    protected_aliases = [
        f"ProtectedNativeAlias{index}"
        for index in range(len(protected_native_callables))
    ]
    protected_alias_input = temporary / "protected-native-aliases.s"
    write_import_caller(
        protected_alias_input,
        "protected_native_alias_caller",
        protected_aliases,
    )

    explicit_native_manifest = {"schema_version": 1, "functions": []}
    for alias, (native, adapter) in zip(
        protected_aliases, protected_native_callables
    ):
        _, result, arguments = builtin_contracts_by_adapter[adapter]
        explicit_native_manifest["functions"].append(
            {
                "kind": "import",
                "symbol": alias,
                "wrapper": native,
                "header": "porpoise/stub.h",
                "return": result,
                "arguments": arguments,
            }
        )
    explicit_native_result = reject_builtin_manifest(
        "protected-explicit-native-wrappers",
        explicit_native_manifest,
        input_path=protected_alias_input,
    )
    for alias, (native, adapter) in zip(
        protected_aliases, protected_native_callables
    ):
        assert (
            f"ABI import {alias} cannot use protected native callable {native} "
            f"as its typed wrapper; use built-in adapter {adapter}"
            in explicit_native_result.stderr
        )

    native_adapter_manifest = {"schema_version": 1, "functions": []}
    for alias, (native, adapter) in zip(
        protected_aliases, protected_native_callables
    ):
        _, result, arguments = builtin_contracts_by_adapter[adapter]
        native_adapter_manifest["functions"].append(
            {
                "kind": "import",
                "symbol": alias,
                "adapter": native,
                "header": "porpoise/stub.h",
                "return": result,
                "arguments": arguments,
            }
        )
    native_adapter_result = reject_builtin_manifest(
        "protected-native-adapter-identifiers",
        native_adapter_manifest,
        input_path=protected_alias_input,
    )
    for alias, (native, adapter) in zip(
        protected_aliases, protected_native_callables
    ):
        assert (
            f"ABI import {alias} cannot use protected native callable {native} "
            f"as its adapter; use built-in adapter {adapter}"
            in native_adapter_result.stderr
        )

    wrong_adapter_manifest = {"schema_version": 1, "functions": []}
    for native_index, (native, expected_adapter) in enumerate(
        protected_native_callables
    ):
        wrong_adapter = protected_native_callables[
            (native_index + 1) % len(protected_native_callables)
        ][1]
        _, result, arguments = builtin_contracts_by_adapter[wrong_adapter]
        wrong_adapter_manifest["functions"].append(
            {
                "kind": "import",
                "symbol": native,
                "adapter": wrong_adapter,
                "header": "porpoise_libporpoise_builtins_private.h",
                "return": result,
                "arguments": arguments,
            }
        )
    wrong_adapter_result = reject_builtin_manifest(
        "protected-wrong-built-in-adapters",
        wrong_adapter_manifest,
        input_path=protected_native_input,
    )
    for native, expected_adapter in protected_native_callables:
        assert (
            f"ABI import {native} must use built-in adapter {expected_adapter}"
            in wrong_adapter_result.stderr
        )

    custom_adapter_manifest = {"schema_version": 1, "functions": []}
    for native_index, (native, expected_adapter) in enumerate(
        protected_native_callables
    ):
        _, result, arguments = builtin_contracts_by_adapter[expected_adapter]
        custom_adapter_manifest["functions"].append(
            {
                "kind": "import",
                "symbol": native,
                "adapter": f"ProtectedCustomAdapter{native_index}",
                "header": "porpoise/stub.h",
                "return": result,
                "arguments": arguments,
            }
        )
    custom_adapter_result = reject_builtin_manifest(
        "protected-custom-adapters",
        custom_adapter_manifest,
        input_path=protected_native_input,
    )
    for native, expected_adapter in protected_native_callables:
        assert (
            f"ABI import {native} must use built-in adapter {expected_adapter}"
            in custom_adapter_result.stderr
        )

    convert_contract = builtin_contracts_by_adapter[
        "porpoise_libporpoise_dvd_convert_path_to_entry_adapter"
    ]
    wrong_return = builtin_manifest(*convert_contract)
    wrong_return["functions"][0]["return"] = gpr("u32", 3)
    reject_builtin_manifest("wrong-return", wrong_return, "return mapping")

    open_contract = builtin_contracts_by_adapter[
        "porpoise_libporpoise_dvd_open_adapter"
    ]
    missing_argument = builtin_manifest(*open_contract)
    missing_argument["functions"][0]["arguments"] = [gpr("pointer", 3)]
    reject_builtin_manifest("missing-argument", missing_argument, "1 ABI arguments")

    extra_argument = builtin_manifest(*convert_contract)
    extra_argument["functions"][0]["arguments"] = [
        gpr("pointer", 3),
        gpr("pointer", 4),
    ]
    reject_builtin_manifest("extra-argument", extra_argument, "2 ABI arguments")

    fast_open_contract = builtin_contracts_by_adapter[
        "porpoise_libporpoise_dvd_fast_open_adapter"
    ]
    reordered_arguments = builtin_manifest(*fast_open_contract)
    reordered_arguments["functions"][0]["arguments"] = [
        gpr("pointer", 3),
        gpr("s32", 4),
    ]
    reject_builtin_manifest(
        "reordered-arguments", reordered_arguments, "argument 1 mapping"
    )

    init_message_contract = builtin_contracts_by_adapter[
        "porpoise_libporpoise_os_init_message_queue_adapter"
    ]
    wrong_argument_kind = builtin_manifest(*init_message_contract)
    wrong_argument_kind["functions"][0]["arguments"] = [
        gpr("pointer", 3),
        gpr("pointer", 4),
        gpr("u32", 5),
    ]
    reject_builtin_manifest(
        "wrong-argument-kind", wrong_argument_kind, "argument 3 mapping"
    )

    wrong_register = builtin_manifest(*convert_contract)
    wrong_register["functions"][0]["arguments"] = [gpr("pointer", 4)]
    reject_builtin_manifest(
        "wrong-argument-register", wrong_register, "argument 1 mapping"
    )

    wrong_header = builtin_manifest(*convert_contract)
    wrong_header["functions"][0]["header"] = "porpoise/stub.h"
    reject_builtin_manifest(
        "wrong-header",
        wrong_header,
        "must use header porpoise_libporpoise_builtins_private.h",
    )

    old_public_header = builtin_manifest(*convert_contract)
    old_public_header["functions"][0]["header"] = (
        "porpoise_libporpoise_adapter.h"
    )
    reject_builtin_manifest(
        "old-public-header",
        old_public_header,
        "must use header porpoise_libporpoise_builtins_private.h",
    )

    wrong_function_kind = builtin_manifest(*convert_contract)
    wrong_function_kind["functions"][0]["kind"] = "export"
    wrong_function_kind["functions"][0]["wrapper"] = "TerminalExport"
    reject_builtin_manifest("wrong-function-kind", wrong_function_kind)

    ai_init_contract = builtin_contracts_by_adapter[
        "porpoise_libporpoise_ai_init_adapter"
    ]
    wrong_ai_init_return = builtin_manifest(*ai_init_contract)
    wrong_ai_init_return["functions"][0]["return"] = gpr("pointer", 3)
    reject_builtin_manifest(
        "ai-init-wrong-return", wrong_ai_init_return, "return mapping"
    )

    missing_ai_init_stack = builtin_manifest(*ai_init_contract)
    missing_ai_init_stack["functions"][0]["arguments"] = []
    reject_builtin_manifest(
        "ai-init-missing-stack",
        missing_ai_init_stack,
        "has 0 ABI arguments; expected 1",
    )

    wrong_ai_init_stack = builtin_manifest(*ai_init_contract)
    wrong_ai_init_stack["functions"][0]["arguments"] = [gpr("u32", 3)]
    reject_builtin_manifest(
        "ai-init-wrong-stack",
        wrong_ai_init_stack,
        "argument 1 mapping u32 r3; expected pointer r3",
    )

    gx_init_contract = builtin_contracts_by_adapter[
        "porpoise_libporpoise_gx_init_adapter"
    ]
    wrong_gx_init_return = builtin_manifest(*gx_init_contract)
    wrong_gx_init_return["functions"][0]["return"] = gpr("u32", 3)
    reject_builtin_manifest(
        "gx-init-wrong-return", wrong_gx_init_return, "return mapping"
    )

    missing_gx_init_size = builtin_manifest(*gx_init_contract)
    missing_gx_init_size["functions"][0]["arguments"] = [gpr("pointer", 3)]
    reject_builtin_manifest(
        "gx-init-missing-size",
        missing_gx_init_size,
        "has 1 ABI arguments; expected 2",
    )

    wrong_gx_init_size = builtin_manifest(*gx_init_contract)
    wrong_gx_init_size["functions"][0]["arguments"] = [
        gpr("pointer", 3),
        gpr("pointer", 4),
    ]
    reject_builtin_manifest(
        "gx-init-wrong-size",
        wrong_gx_init_size,
        "argument 2 mapping pointer r4; expected u32 r4",
    )

    gx_set_array_contract = builtin_contracts_by_adapter[
        "porpoise_libporpoise_gx_set_array_adapter"
    ]
    wrong_gx_set_array_stride = builtin_manifest(*gx_set_array_contract)
    wrong_gx_set_array_stride["functions"][0]["arguments"] = [
        gpr("u32", 3),
        gpr("pointer", 4),
        gpr("u32", 5),
    ]
    reject_builtin_manifest(
        "gx-set-array-wrong-stride",
        wrong_gx_set_array_stride,
        "argument 3 mapping u32 r5; expected u8 r5",
    )

    os_report_contract = builtin_contracts_by_adapter[
        "porpoise_libporpoise_os_report_adapter"
    ]
    wrong_report_return = builtin_manifest(*os_report_contract)
    wrong_report_return["functions"][0]["return"] = gpr("u32", 3)
    reject_builtin_manifest(
        "os-report-wrong-return", wrong_report_return, "return mapping"
    )

    missing_report_format = builtin_manifest(*os_report_contract)
    missing_report_format["functions"][0]["arguments"] = []
    reject_builtin_manifest(
        "os-report-missing-format",
        missing_report_format,
        "has 0 ABI arguments; expected 1",
    )

    wrong_report_format_type = builtin_manifest(*os_report_contract)
    wrong_report_format_type["functions"][0]["arguments"] = [gpr("u32", 3)]
    reject_builtin_manifest(
        "os-report-wrong-format-type",
        wrong_report_format_type,
        "argument 1 mapping u32 r3; expected pointer r3",
    )

    wrong_report_format_register = builtin_manifest(*os_report_contract)
    wrong_report_format_register["functions"][0]["arguments"] = [
        gpr("pointer", 4)
    ]
    reject_builtin_manifest(
        "os-report-wrong-format-register",
        wrong_report_format_register,
        "argument 1 mapping pointer r4; expected pointer r3",
    )

    extra_report_argument = builtin_manifest(*os_report_contract)
    extra_report_argument["functions"][0]["arguments"] = [
        gpr("pointer", 3),
        gpr("u32", 4),
    ]
    reject_builtin_manifest(
        "os-report-extra-argument",
        extra_report_argument,
        "has 2 ABI arguments; expected 1",
    )

    resume_contract = builtin_contracts_by_adapter[
        "porpoise_libporpoise_os_resume_thread_adapter"
    ]
    get_current_thread_contract = builtin_contracts_by_adapter[
        "porpoise_libporpoise_os_get_current_thread_adapter"
    ]
    suspend_contract = builtin_contracts_by_adapter[
        "porpoise_libporpoise_os_suspend_thread_adapter"
    ]
    exit_contract = builtin_contracts_by_adapter[
        "porpoise_libporpoise_os_exit_thread_adapter"
    ]

    wrong_thread_return = builtin_manifest(*resume_contract)
    wrong_thread_return["functions"][0]["return"] = gpr("u32", 3)
    reject_builtin_manifest(
        "thread-wrong-return", wrong_thread_return, "return mapping"
    )

    wrong_get_current_thread_return = builtin_manifest(
        *get_current_thread_contract
    )
    wrong_get_current_thread_return["functions"][0]["return"] = gpr(
        "u32", 3
    )
    reject_builtin_manifest(
        "get-current-thread-wrong-return",
        wrong_get_current_thread_return,
        "return mapping",
    )

    wrong_get_current_thread_register = builtin_manifest(
        *get_current_thread_contract
    )
    wrong_get_current_thread_register["functions"][0]["return"] = gpr(
        "pointer", 4
    )
    reject_builtin_manifest(
        "get-current-thread-wrong-register",
        wrong_get_current_thread_register,
        "return must map integer/pointer returns to r3",
    )

    extra_get_current_thread_argument = builtin_manifest(
        *get_current_thread_contract
    )
    extra_get_current_thread_argument["functions"][0]["arguments"] = [
        gpr("pointer", 3)
    ]
    reject_builtin_manifest(
        "get-current-thread-extra-argument",
        extra_get_current_thread_argument,
        "has 1 ABI arguments; expected 0",
    )

    wrong_exit_return = builtin_manifest(*exit_contract)
    wrong_exit_return["functions"][0]["return"] = gpr("s32", 3)
    reject_builtin_manifest(
        "thread-exit-wrong-return", wrong_exit_return, "return mapping"
    )

    missing_thread_argument = builtin_manifest(*resume_contract)
    missing_thread_argument["functions"][0]["arguments"] = []
    reject_builtin_manifest(
        "thread-missing-argument",
        missing_thread_argument,
        "has 0 ABI arguments; expected 1",
    )

    extra_thread_argument = builtin_manifest(*suspend_contract)
    extra_thread_argument["functions"][0]["arguments"] = [
        gpr("pointer", 3),
        gpr("pointer", 4),
    ]
    reject_builtin_manifest(
        "thread-extra-argument",
        extra_thread_argument,
        "has 2 ABI arguments; expected 1",
    )

    wrong_thread_argument_type = builtin_manifest(*exit_contract)
    wrong_thread_argument_type["functions"][0]["arguments"] = [gpr("u32", 3)]
    reject_builtin_manifest(
        "thread-wrong-argument-type",
        wrong_thread_argument_type,
        "argument 1 mapping u32 r3; expected pointer r3",
    )

    wrong_thread_argument_register = builtin_manifest(*suspend_contract)
    wrong_thread_argument_register["functions"][0]["arguments"] = [
        gpr("pointer", 4)
    ]
    reject_builtin_manifest(
        "thread-wrong-argument-register",
        wrong_thread_argument_register,
        "argument 1 mapping pointer r4; expected pointer r3",
    )

    # Every thread contract has one argument. A noncanonical first slot must
    # fail even if the canonical r3 mapping appears later in the array.
    reordered_thread_arguments = builtin_manifest(*resume_contract)
    reordered_thread_arguments["functions"][0]["arguments"] = [
        gpr("pointer", 4),
        gpr("pointer", 3),
    ]
    reject_builtin_manifest(
        "thread-reordered-arguments",
        reordered_thread_arguments,
        "has an out-of-order GPR mapping",
    )

    wrong_thread_header = builtin_manifest(*resume_contract)
    wrong_thread_header["functions"][0]["header"] = "porpoise/stub.h"
    reject_builtin_manifest(
        "thread-wrong-header",
        wrong_thread_header,
        "must use header porpoise_libporpoise_builtins_private.h",
    )

    wrong_thread_function_kind = builtin_manifest(*exit_contract)
    wrong_thread_function_kind["functions"][0]["kind"] = "export"
    wrong_thread_function_kind["functions"][0]["wrapper"] = "TerminalExport"
    reject_builtin_manifest(
        "thread-wrong-function-kind",
        wrong_thread_function_kind,
        "must not declare an adapter",
    )

    exported = temporary / "exported"
    run(
        TOOL,
        FIXTURES / "inputs" / "abi_exports",
        "--output",
        exported,
        "--abi",
        FIXTURES / "abi" / "exports.json",
    )
    export_source = (exported / "src" / "porpoise_exports.c").read_text(
        encoding="utf-8"
    )
    assert "PorpoiseAddOne" in export_source
    assert "static __thread PorpoisePpcState *porpoise_export_state;" in export_source
    assert "porpoise_libporpoise_run_guest(state," in export_source
    assert "porpoise_call_address(state," not in export_source
    export_harness = exported / "tests" / "export_harness.c"
    export_harness.parent.mkdir(parents=True)
    export_harness.write_text(
        "#include <stdlib.h>\n"
        "#include \"porpoise_exports.h\"\n"
        "#include \"porpoise_generated.h\"\n"
        "#include \"porpoise_libporpoise_adapter.h\"\n"
        "#define CHECK(condition) do { if (!(condition)) abort(); } while (0)\n"
        "int main(void) {\n"
        "  PorpoiseHostAdapter host; PorpoisePpcState state;\n"
        "  CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);\n"
        "  CHECK(porpoise_generated_bind(&host) == PORPOISE_HOST_OK);\n"
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
        "PorpoiseTitleHostResultV3",
        "PorpoiseTitleInitialWordV3",
        "PorpoiseTitleRuntimeConfigV1",
        "PorpoiseTitleEntryStateV3",
        "PorpoiseHostPrepareRuntimeV1",
        "PorpoiseHostPrepareTitleEntryV3",
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

    exact_estimate = temporary / "exact-estimate.s"
    exact_estimate.write_text(
        ".text\n.fn exact_estimate, global\n"
        "/* 80000000 00000000  FC 40 08 34 */ frsqrte f2, f1\n"
        "/* 80000004 00000004  FC 60 08 35 */ frsqrte. f3, f1\n"
        "/* 80000008 00000008  4E 80 00 20 */ blr\n.endfn exact_estimate\n",
        encoding="utf-8",
    )
    exact_estimate_output = temporary / "exact-estimate-output"
    run(TOOL, exact_estimate, "--output", exact_estimate_output, "--strict")
    exact_estimate_report = json.loads(
        (exact_estimate_output / "porpoise-report.json").read_text(encoding="utf-8")
    )
    estimate_entries = [
        instruction
        for instruction in exact_estimate_report["instructions"]
        if instruction["mnemonic"] in {"frsqrte", "frsqrte."}
    ]
    assert len(estimate_entries) == 2
    assert all(instruction["status"] == "lowered" for instruction in estimate_entries)
    assert all(instruction["semantic_test"] for instruction in estimate_entries)
    exact_estimate_source = "\n".join(
        source.read_text(encoding="utf-8")
        for source in (exact_estimate_output / "src" / "lifted").rglob("*.c")
    )
    assert "porpoise_frsqrte(state, 2U, 1U, 0)" in exact_estimate_source
    assert "porpoise_frsqrte(state, 3U, 1U, 1)" in exact_estimate_source
    assert 'porpoise_trace_approximate(state, UINT32_C(0x80000000), "frsqrte")' not in exact_estimate_source

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
