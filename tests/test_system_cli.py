#!/usr/bin/env python3
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import textwrap


TOOL = Path(sys.argv[1]).resolve()
ROOT = Path(sys.argv[2]).resolve()
FIXTURES = ROOT / "tests" / "fixtures"
SYSTEM_FIXTURE = FIXTURES / "inputs" / "system" / "system_semantics.s"
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
            f"expected exit {expected}, got {completed.returncode}: "
            f"{' '.join(map(str, arguments))}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    return completed


def add_stub(project):
    target = project / "subprojects" / "libPorpoise"
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(FIXTURES / "libporpoise_stub", target)


RAW_DETAIL = "raw architectural state transfer"
EXPECTED_INSTRUCTIONS = {
    0x80008000: ("mfxer", "lowered", True, RAW_DETAIL),
    0x80008004: ("mtxer", "lowered", True, RAW_DETAIL),
    0x80008008: ("mfspr", "lowered", True, RAW_DETAIL),
    0x8000800C: ("mtspr", "lowered", True, RAW_DETAIL),
    0x80008010: ("mfmsr", "lowered", True, RAW_DETAIL),
    0x80008014: ("mfsr", "lowered", True, RAW_DETAIL),
    0x80008018: ("mtcrf", "lowered", True, RAW_DETAIL),
    0x8000801C: (
        "mtspr",
        "host-equivalent-no-op",
        True,
        "the target architecture defines this write as a privileged no-op",
    ),
    0x80008020: (
        "mftb",
        "approximate",
        True,
        "host-backed time-base and decrementer behavior is approximate",
    ),
    0x80008024: (
        "dcbz",
        "approximate",
        True,
        "guest memory is zeroed atomically without modeling host cache state",
    ),
    0x80008028: (
        "dcbi",
        "host-equivalent-no-op",
        True,
        "the coherent host needs no cache invalidation after the privilege check",
    ),
    0x8000802C: ("mfibatu", "lowered", True, RAW_DETAIL),
    0x80008030: ("mfibatl", "lowered", True, RAW_DETAIL),
    0x80008034: ("mfdbatu", "lowered", True, RAW_DETAIL),
    0x80008038: ("mfdbatl", "lowered", True, RAW_DETAIL),
    0x8000803C: (
        "mtibatu",
        "approximate",
        True,
        "state is preserved but MMU and translation side effects are not modeled",
    ),
    0x80008040: (
        "mtibatl",
        "approximate",
        True,
        "state is preserved but MMU and translation side effects are not modeled",
    ),
    0x80008044: (
        "mtdbatu",
        "approximate",
        True,
        "state is preserved but MMU and translation side effects are not modeled",
    ),
    0x80008048: (
        "mtdbatl",
        "approximate",
        True,
        "state is preserved but MMU and translation side effects are not modeled",
    ),
    0x8000804C: (
        "mftb",
        "approximate",
        True,
        "host-backed time-base and decrementer behavior is approximate",
    ),
    0x80008050: (
        "mftb",
        "approximate",
        True,
        "host-backed time-base and decrementer behavior is approximate",
    ),
    0x80008054: (
        "mtspr",
        "approximate",
        True,
        "performance-monitor timing and side effects are not modeled",
    ),
    0x80008058: ("blr", "lowered", False, ""),
    0x80008100: (
        "twui",
        "approximate",
        True,
        "the trap is delegated to the embedding host adapter",
    ),
    0x80008104: (
        "sc",
        "approximate",
        True,
        "the system call is delegated to the embedding host adapter",
    ),
    0x80008108: ("blr", "lowered", False, ""),
    0x80008200: (
        "rfi",
        "approximate",
        True,
        "interrupt-state restoration is modeled but host interrupt machinery is not",
    ),
    0x80008280: (
        "mcrxr",
        "lowered",
        True,
        "XER summary, overflow, and carry bits are transferred to the selected CR field and cleared",
    ),
    0x80008284: (
        "mfspr",
        "approximate",
        True,
        "opaque special-purpose register state is preserved but hardware side effects are not modeled",
    ),
    0x80008288: (
        "mtspr",
        "approximate",
        True,
        "opaque special-purpose register state is preserved but hardware side effects are not modeled",
    ),
    0x8000828C: (
        "mfspr",
        "approximate",
        True,
        "state is preserved but hardware control side effects are not modeled",
    ),
    0x80008290: (
        "mtspr",
        "approximate",
        True,
        "state is preserved but hardware control side effects are not modeled",
    ),
    0x80008294: ("blr", "lowered", False, ""),
}


with tempfile.TemporaryDirectory(
    prefix="porpoise-system-cli-", ignore_cleanup_errors=True
) as temporary:
    temporary = Path(temporary)

    output = temporary / "system-output"
    run(TOOL, SYSTEM_FIXTURE, "--output", output)
    report = json.loads((output / "porpoise-report.json").read_text(encoding="utf-8"))
    by_address = {instruction["address"]: instruction for instruction in report["instructions"]}
    assert set(by_address) == set(EXPECTED_INSTRUCTIONS)
    for address, expected in EXPECTED_INSTRUCTIONS.items():
        instruction = by_address[address]
        assert (
            instruction["mnemonic"],
            instruction["status"],
            instruction["semantic_test"],
            instruction["detail"],
        ) == expected

    assert report["summary"] == {
        "files": 1,
        "functions": 4,
        "data_words": 0,
        "data_objects": 0,
        "anonymous_contributions": 0,
        "anonymous_explicit_bytes": 0,
        "data_fixups": 0,
        "data_spans": 0,
        "initialized_data_bytes": 0,
        "zero_fill_data_bytes": 0,
        "data_chunks": 0,
        "lowered": 15,
        "host_equivalent_noop": 2,
        "approximate": 16,
        "unsupported": 0,
    }
    approximation_addresses = {
        address
        for address, expected in EXPECTED_INSTRUCTIONS.items()
        if expected[1] == "approximate"
    }
    assert {item["address"] for item in report["approximations"]} == approximation_addresses
    assert {item["address"] for item in report["diagnostics"]} == approximation_addresses
    assert all(item["severity"] == "warning" for item in report["diagnostics"])

    lifted_source = (output / "src" / "lifted" / "system_semantics.c").read_text(
        encoding="utf-8"
    )
    for fragment in (
        "porpoise_require_supervisor",
        "porpoise_time_base_read",
        "porpoise_cache_block_zero",
        "porpoise_data_cache_block_invalidate",
        "porpoise_trap_event",
        "porpoise_system_call_event",
        "porpoise_dispatch_available(porpoise_rfi_target)",
        "porpoise_call_address(state, porpoise_rfi_target)",
        "state->gpr[12] = state->ibat_upper[2]",
        "state->ibat_lower[4] = state->gpr[17]",
        "state->gpr[14] = state->dbat_upper[3]",
        "state->dbat_lower[7] = state->gpr[19]",
        "state->sia = state->gpr[24]",
        "porpoise_cr_set_field(state, 3U",
        "state->gpr[22] = state->opaque_spr[976]",
        "state->opaque_spr[976] = state->gpr[23]",
        "state->gpr[24] = state->thermal_management[0]",
        "state->thermal_management[1] = state->gpr[25]",
    ):
        assert fragment in lifted_source
    dispatch_header = (output / "src" / "porpoise_dispatch_private.h").read_text(
        encoding="utf-8"
    )
    assert "int porpoise_dispatch_available(uint32_t address);" in dispatch_header

    strict_output = temporary / "strict-output"
    strict_result = run(
        TOOL,
        SYSTEM_FIXTURE,
        "--output",
        strict_output,
        "--strict",
        expected=3,
    )
    assert not strict_output.exists()
    for mnemonic in ("mftb", "dcbz", "mtibatu", "mtdbatl", "mfspr", "mtspr", "twui", "sc", "rfi"):
        assert f"{mnemonic} instruction uses approximate host semantics" in strict_result.stderr

    invalid_word = temporary / "invalid-word.s"
    invalid_word.write_text(
        textwrap.dedent(
            """\
            .text
            .global invalid_system_word
            .fn invalid_system_word, global
            /* 80009000 00000000  7C 81 02 A6 */ mfxer r3
            .endfn invalid_system_word
            """
        ),
        encoding="utf-8",
    )
    invalid_output = temporary / "invalid-output"
    invalid_result = run(TOOL, invalid_word, "--output", invalid_output, expected=3)
    assert not invalid_output.exists()
    assert "invalid operands or annotated word for mfxer" in invalid_result.stderr

    invalid_bat_word = temporary / "invalid-bat-word.s"
    invalid_bat_word.write_text(
        textwrap.dedent(
            """\
            .text
            .global invalid_bat_word
            .fn invalid_bat_word, global
            /* 80009010 00000010  7C 75 82 A6 */ mfibatu r3, 2
            .endfn invalid_bat_word
            """
        ),
        encoding="utf-8",
    )
    invalid_bat_output = temporary / "invalid-bat-output"
    invalid_bat_result = run(
        TOOL,
        invalid_bat_word,
        "--output",
        invalid_bat_output,
        expected=3,
    )
    assert not invalid_bat_output.exists()
    assert "invalid operands or annotated word for mfibatu" in invalid_bat_result.stderr

    unknown_spr = temporary / "unknown-spr.s"
    unknown_spr.write_text(
        textwrap.dedent(
            """\
            .text
            .global unknown_spr
            .fn unknown_spr, global
            /* 80009020 00000020  7C 78 92 A6 */ mfspr r3, 600
            .endfn unknown_spr
            """
        ),
        encoding="utf-8",
    )
    unknown_output = temporary / "unknown-output"
    unknown_result = run(TOOL, unknown_spr, "--output", unknown_output, expected=3)
    assert not unknown_output.exists()
    assert "mfspr" in unknown_result.stderr
    assert "unknown special-purpose register" in unknown_result.stderr

    build_input = temporary / "build-input"
    build_input.mkdir()
    shutil.copyfile(SYSTEM_FIXTURE, build_input / SYSTEM_FIXTURE.name)
    (build_input / "nested_status.s").write_text(
        textwrap.dedent(
            """\
            .text
            .global nested_event_caller
            .fn nested_event_caller, global
            /* 80008300 00000300  48 00 00 21 */ bl nested_event_callee
            /* 80008304 00000304  38 60 00 63 */ li r3, 99
            /* 80008308 00000308  4E 80 00 20 */ blr
            .endfn nested_event_caller

            .global nested_event_callee
            .fn nested_event_callee, global
            /* 80008320 00000320  44 00 00 02 */ sc
            /* 80008324 00000324  4E 80 00 20 */ blr
            .endfn nested_event_callee
            """
        ),
        encoding="utf-8",
    )

    build_output = temporary / "build-output"
    run(TOOL, build_input, "--output", build_output)
    nested_source = (build_output / "src" / "lifted" / "nested_status.c").read_text(
        encoding="utf-8"
    )
    assert "porpoise_call_address(state, UINT32_C(0x80008320))" in nested_source

    harness = build_output / "tests" / "system_harness.c"
    harness.parent.mkdir(parents=True)
    harness.write_text(
        textwrap.dedent(
            """\
            #include <stddef.h>
            #include <stdint.h>
            #include <stdlib.h>
            #include <string.h>

            #include "porpoise_generated.h"
            #include "porpoise_libporpoise_adapter.h"
            #include "generated/nested_status.h"
            #include "generated/system_semantics.h"

            #define CHECK(condition) do { if (!(condition)) abort(); } while (0)

            static unsigned int system_call_count;

            static PorpoiseHostResult stop_from_system_call(
                void *context,
                PorpoisePpcState *state,
                uint32_t instruction_address)
            {
                (void)context;
                (void)instruction_address;
                system_call_count++;
                state->status = PORPOISE_EXECUTION_RETURNED;
                return PORPOISE_HOST_OK;
            }

            int main(void)
            {
                PorpoiseHostAdapter host;
                PorpoisePpcState state;
                uint8_t write_block[32];
                uint8_t read_block[32];
                size_t index;

                CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);
                CHECK(porpoise_generated_bind(&host) == PORPOISE_HOST_OK);
                memset(write_block, 0xA5, sizeof(write_block));
                CHECK(host.write_bytes(
                    host.context,
                    UINT32_C(0x80000100),
                    write_block,
                    sizeof(write_block)) == PORPOISE_HOST_OK);

                porpoise_state_init(&state, &host);
                state.gpr[1] = UINT32_C(0x817FF000);
                state.gpr[2] = UINT32_C(0x80001000);
                state.gpr[13] = UINT32_C(0x80002000);
                CHECK(porpoise_state_prepare_title_entry(&state));
                state.xer = UINT32_C(0x2468ACE0);
                state.gqr[0] = UINT32_C(0x00040004);
                state.segment_register[0] = UINT32_C(0x99AABBCC);
                state.cr = UINT32_C(0x01234567);
                state.pvr = UINT32_C(0x11223344);
                state.gpr[7] = UINT32_C(0xDEADBEEF);
                state.gpr[8] = UINT32_C(0x80000113);
                state.gpr[11] = UINT32_C(0xA0000005);
                state.ibat_upper[2] = UINT32_C(0x11112222);
                state.ibat_lower[5] = UINT32_C(0x33334444);
                state.dbat_upper[3] = UINT32_C(0x55556666);
                state.dbat_lower[6] = UINT32_C(0x77778888);
                state.gpr[16] = UINT32_C(0x9999AAAA);
                state.gpr[17] = UINT32_C(0xBBBBCCCC);
                state.gpr[18] = UINT32_C(0xDDDDEEEE);
                state.gpr[19] = UINT32_C(0xFFFF0001);
                state.gpr[24] = UINT32_C(0x80203040);
                porpoise_lifted_system_state_semantics(&state);

                CHECK(!porpoise_state_has_fault(&state));
                CHECK(state.gpr[3] == UINT32_C(0x2468ACE0));
                CHECK(state.xer == UINT32_C(0x2468ACE0));
                CHECK(state.gqr[1] == UINT32_C(0x00040004));
                CHECK(state.gpr[5] == (PORPOISE_MSR_EE | PORPOISE_MSR_FP));
                CHECK(state.gpr[6] == UINT32_C(0x99AABBCC));
                CHECK(state.cr == UINT32_C(0xA1234565));
                CHECK(state.pvr == UINT32_C(0x11223344));
                CHECK(state.gpr[10] != 0U);
                CHECK(state.gpr[12] == UINT32_C(0x11112222));
                CHECK(state.gpr[13] == UINT32_C(0x33334444));
                CHECK(state.gpr[14] == UINT32_C(0x55556666));
                CHECK(state.gpr[15] == UINT32_C(0x77778888));
                CHECK(state.ibat_upper[1] == UINT32_C(0x9999AAAA));
                CHECK(state.ibat_lower[4] == UINT32_C(0xBBBBCCCC));
                CHECK(state.dbat_upper[2] == UINT32_C(0xDDDDEEEE));
                CHECK(state.dbat_lower[7] == UINT32_C(0xFFFF0001));
                CHECK(state.sia == UINT32_C(0x80203040));
                CHECK(state.gpr[20] != 0U);
                CHECK(state.gpr[21] == 0U);
                CHECK(state.pc == UINT32_C(0x80008058));
                CHECK(host.read_bytes(
                    host.context,
                    UINT32_C(0x80000100),
                    read_block,
                    sizeof(read_block)) == PORPOISE_HOST_OK);
                for (index = 0U; index < sizeof(read_block); index++) {
                    CHECK(read_block[index] == 0U);
                }

                porpoise_state_init(&state, &host);
                state.xer = UINT32_C(0xE1234567);
                state.cr = UINT32_C(0x89ABCDEF);
                state.opaque_spr[976] = UINT32_C(0x11112222);
                state.gpr[23] = UINT32_C(0x33334444);
                state.thermal_management[0] = UINT32_C(0x55556666);
                state.gpr[25] = UINT32_C(0x77778888);
                porpoise_lifted_system_extended_register_semantics(&state);
                CHECK(!porpoise_state_has_fault(&state));
                CHECK(state.xer == UINT32_C(0x01234567));
                CHECK(state.cr == UINT32_C(0x89AECDEF));
                CHECK(state.gpr[22] == UINT32_C(0x11112222));
                CHECK(state.opaque_spr[976] == UINT32_C(0x33334444));
                CHECK(state.gpr[24] == UINT32_C(0x55556666));
                CHECK(state.thermal_management[1] == UINT32_C(0x77778888));
                CHECK(state.pc == UINT32_C(0x80008294));

                porpoise_state_init(&state, &host);
                state.gpr[1] = UINT32_C(0x817FF000);
                state.gpr[2] = UINT32_C(0x80001000);
                state.gpr[13] = UINT32_C(0x80002000);
                CHECK(porpoise_state_prepare_title_entry(&state));
                host.system_call = stop_from_system_call;
                state.status = PORPOISE_EXECUTION_RUNNING;
                state.gpr[3] = 7U;
                porpoise_lifted_nested_event_caller(&state);
                CHECK(!porpoise_state_has_fault(&state));
                CHECK(state.status == PORPOISE_EXECUTION_RETURNED);
                CHECK(system_call_count == 1U);
                CHECK(state.gpr[3] == 7U);
                CHECK(state.pc == UINT32_C(0x80008320));

                porpoise_libporpoise_adapter_shutdown(&host);
                return 0;
            }
            """
        ),
        encoding="utf-8",
    )
    with (build_output / "meson.build").open("a", encoding="utf-8") as meson_file:
        meson_file.write(
            "\nsystem_harness = executable('system_harness', "
            "'tests/system_harness.c', "
            "include_directories: generated_private_inc, "
            "dependencies: porpoise_lifted_dep)\n"
        )
    add_stub(build_output)
    run(
        "meson",
        "setup",
        "build",
        "--wrap-mode=forcefallback",
        *CHILD_MESON_ARGS,
        cwd=build_output,
    )
    run("meson", "compile", "-C", "build", cwd=build_output)
    executable = build_output / "build" / (
        "system_harness.exe" if os.name == "nt" else "system_harness"
    )
    run(executable, cwd=build_output)
