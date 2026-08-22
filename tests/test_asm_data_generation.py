#!/usr/bin/env python3
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
            f"expected exit {expected}, got {completed.returncode}: "
            f"{' '.join(map(str, arguments))}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    return completed


def add_stub(project):
    target = project / "subprojects" / "libPorpoise"
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(FIXTURES / "libporpoise_stub", target)


with tempfile.TemporaryDirectory(
    prefix="porpoise-asm-data-tests-", ignore_cleanup_errors=True
) as temporary:
    temporary = Path(temporary)
    generated = temporary / "generated"

    help_text = run(TOOL, "--help").stdout
    assert "--guest-image" not in help_text
    run(
        TOOL,
        FIXTURES / "inputs" / "asm_data",
        "--output",
        temporary / "removed-option-output",
        "--guest-image",
        temporary / "payload.elf",
        expected=2,
    )

    run(
        TOOL,
        FIXTURES / "inputs" / "asm_data",
        "--output",
        generated,
    )

    generated_files = sorted(path for path in generated.rglob("*") if path.is_file())
    assert generated_files
    assert not any("guest_image" in path.as_posix() for path in generated_files)
    assert not any(path.suffix.lower() == ".elf" for path in generated_files)
    for path in generated_files:
        if path.suffix.lower() in {".c", ".h", ".json", ".md", ".build"}:
            assert "guest_image" not in path.read_text(encoding="utf-8")

    report = json.loads(
        (generated / "porpoise-report.json").read_text(encoding="utf-8")
    )
    assert "guest_image" not in report
    assert report["data_model"]["source"] == "annotated_assembly"
    assert [item["symbol"] for item in report["data_objects"]] == [
        "scalar_blob",
        "pointer_blob",
        "local_blob",
        "zero_blob",
    ]
    assert [item["kind"] for item in report["data_spans"]] == [
        "initialized",
        "initialized",
        "initialized",
        "zero_fill",
    ]
    assert [item["size"] for item in report["data_spans"]] == [24, 12, 8, 16]
    pointer_fixups = report["data_objects"][1]["fixups"]
    assert [item["target"] for item in pointer_fixups] == [
        "scalar_blob",
        "scalar_blob",
        ".L_after",
    ]
    assert pointer_fixups[1]["target_addend"] == 2
    assert pointer_fixups[2]["kind"] == "rel_target_32"
    assert pointer_fixups[2]["base"] == "entry_fn"
    assert report["summary"]["data_objects"] == 4
    assert report["summary"]["anonymous_contributions"] == 1
    assert report["summary"]["anonymous_explicit_bytes"] == 0
    assert report["summary"]["data_fixups"] == 4
    assert report["summary"]["initialized_data_bytes"] == 44
    assert report["summary"]["zero_fill_data_bytes"] == 16

    data_chunks = sorted((generated / "src" / "data").glob("porpoise_data_*.c"))
    assert data_chunks

    harness = generated / "tests" / "asm_data_harness.c"
    harness.parent.mkdir(parents=True)
    harness.write_text(
        "#include <stdint.h>\n"
        "#include <stdlib.h>\n"
        "#include <porpoise_generated.h>\n"
        "#include \"porpoise_data_private.h\"\n"
        "#define CHECK(condition) do { if (!(condition)) abort(); } while (0)\n"
        "int main(void) {\n"
        "  static const uint8_t expected[] = {\n"
        "    0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,\n"
        "    0x3F, 0x80, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00,\n"
        "    0x00, 0x00, 0x00, 0x00, 0x48, 0x69, 0x0A, 0x00,\n"
        "    0x80, 0x01, 0x00, 0x00, 0x80, 0x01, 0x00, 0x02,\n"
        "    0x80, 0x00, 0x80, 0x00, 0xAA, 0xBB, 0xCC, 0xDD,\n"
        "    0x80, 0x01, 0x00, 0x24\n"
        "  };\n"
        "  PorpoiseHostAdapter host;\n"
        "  PorpoisePpcState state;\n"
        "  uint32_t index;\n"
        "  CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);\n"
        "  CHECK(porpoise_generated_bind(&host) == PORPOISE_HOST_OK);\n"
        "  porpoise_state_init(&state, &host);\n"
        "  for (index = 0U; index < UINT32_C(0x3C); index++)\n"
        "    porpoise_store_u8(&state, UINT32_C(0x80010000) + index, UINT8_C(0xCC));\n"
        "  CHECK(!porpoise_state_has_fault(&state));\n"
        "  porpoise_initialize_data(&state);\n"
        "  CHECK(!porpoise_state_has_fault(&state));\n"
        "  for (index = 0U; index < (uint32_t)sizeof(expected); index++)\n"
        "    CHECK(porpoise_load_u8(&state, UINT32_C(0x80010000) + index) == expected[index]);\n"
        "  for (index = (uint32_t)sizeof(expected); index < UINT32_C(0x3C); index++)\n"
        "    CHECK(porpoise_load_u8(&state, UINT32_C(0x80010000) + index) == 0U);\n"
        "  CHECK(!porpoise_state_has_fault(&state));\n"
        "  porpoise_libporpoise_adapter_shutdown(&host);\n"
        "  return 0;\n"
        "}\n",
        encoding="utf-8",
    )
    with (generated / "meson.build").open("a", encoding="utf-8") as meson_file:
        meson_file.write(
            "\nasm_data_harness = executable(\n"
            "  'asm_data_harness',\n"
            "  'tests/asm_data_harness.c',\n"
            "  dependencies: porpoise_lifted_dep,\n"
            "  include_directories: include_directories('src'),\n"
            ")\n"
        )

    add_stub(generated)
    run(
        "meson",
        "setup",
        "build",
        "--wrap-mode=forcefallback",
        *CHILD_MESON_ARGS,
        cwd=generated,
    )
    run("meson", "compile", "-C", "build", cwd=generated)
    executable = generated / "build" / (
        "asm_data_harness.exe" if os.name == "nt" else "asm_data_harness"
    )
    run(executable, cwd=generated)

    jump_generated = temporary / "address-taken-generated"
    run(
        TOOL,
        FIXTURES / "inputs" / "address_taken_jump_table",
        "--output",
        jump_generated,
    )
    jump_source = (
        jump_generated / "src" / "lifted" / "code.c"
    ).read_text(encoding="utf-8")
    registry_source = "\n".join(
        path.read_text(encoding="utf-8")
        for path in sorted(
            (jump_generated / "src").glob("porpoise_function_registry_*.c")
        )
    )
    # The first table destination is deliberately duplicated.  It must still
    # produce exactly one resume case and one global registry entry.
    assert jump_source.count("case UINT32_C(0x80008014)") == 1
    assert jump_source.count("case UINT32_C(0x8000801C)") == 1
    assert registry_source.count("case UINT32_C(0x80008014)") == 1
    assert registry_source.count("case UINT32_C(0x8000801C)") == 1

    jump_harness = jump_generated / "tests" / "address_taken_harness.c"
    jump_harness.parent.mkdir(parents=True)
    jump_harness.write_text(
        "#include <stdint.h>\n"
        "#include <stdlib.h>\n"
        "#include <porpoise_generated.h>\n"
        "#include \"porpoise_data_private.h\"\n"
        "#include \"porpoise_dispatch_private.h\"\n"
        "#define CHECK(condition) do { if (!(condition)) abort(); } while (0)\n"
        "static void run_case(PorpoisePpcState *state, uint32_t index, uint32_t expected) {\n"
        "  state->gpr[3] = index;\n"
        "  CHECK(porpoise_call_address(state, UINT32_C(0x80008000)));\n"
        "  CHECK(!porpoise_state_has_fault(state));\n"
        "  CHECK(state->gpr[3] == expected);\n"
        "}\n"
        "int main(void) {\n"
        "  PorpoiseHostAdapter host;\n"
        "  PorpoisePpcState state;\n"
        "  CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);\n"
        "  CHECK(porpoise_generated_bind(&host) == PORPOISE_HOST_OK);\n"
        "  porpoise_state_init(&state, &host);\n"
        "  porpoise_initialize_data(&state);\n"
        "  CHECK(!porpoise_state_has_fault(&state));\n"
        "  run_case(&state, UINT32_C(0), UINT32_C(17));\n"
        "  run_case(&state, UINT32_C(1), UINT32_C(17));\n"
        "  run_case(&state, UINT32_C(2), UINT32_C(34));\n"
        "  porpoise_libporpoise_adapter_shutdown(&host);\n"
        "  return 0;\n"
        "}\n",
        encoding="utf-8",
    )
    with (jump_generated / "meson.build").open("a", encoding="utf-8") as meson_file:
        meson_file.write(
            "\naddress_taken_harness = executable(\n"
            "  'address_taken_harness',\n"
            "  'tests/address_taken_harness.c',\n"
            "  dependencies: porpoise_lifted_dep,\n"
            "  include_directories: include_directories('src'),\n"
            ")\n"
        )
    add_stub(jump_generated)
    run(
        "meson",
        "setup",
        "build",
        "--wrap-mode=forcefallback",
        *CHILD_MESON_ARGS,
        cwd=jump_generated,
    )
    run("meson", "compile", "-C", "build", cwd=jump_generated)
    jump_executable = jump_generated / "build" / (
        "address_taken_harness.exe"
        if os.name == "nt"
        else "address_taken_harness"
    )
    run(jump_executable, cwd=jump_generated)

    invalid_fixups = {
        "misaligned": (
            "/* 80009000 00000000  4E 80 00 20 */ blr\n",
            "jump_owner+0x1",
            "misaligned code address",
        ),
        "out-of-range": (
            "/* 80009000 00000000  4E 80 00 20 */ blr\n",
            "jump_owner+0x4",
            "outside its owning function",
        ),
        "not-an-instruction": (
            "/* 80009000 00000000  60 00 00 00 */ nop\n"
            "/* 80009008 00000008  4E 80 00 20 */ blr\n",
            "jump_owner+0x4",
            "does not resolve to a real instruction",
        ),
    }
    for case_name, (instructions, expression, expected_message) in invalid_fixups.items():
        invalid = temporary / f"invalid-address-taken-{case_name}.s"
        invalid.write_text(
            ".text\n"
            ".fn jump_owner, global\n"
            f"{instructions}"
            ".endfn jump_owner\n"
            "# 0x80011000..0x80011004 | size: 0x4\n"
            ".data\n"
            "# .data:0x0 | 0x80011000 | size: 0x4\n"
            ".obj bad_jump_table, global\n"
            f"  .4byte {expression}\n"
            ".endobj bad_jump_table\n",
            encoding="utf-8",
        )
        invalid_result = run(
            TOOL,
            invalid,
            "--output",
            temporary / f"invalid-address-taken-{case_name}-output",
            expected=3,
        )
        assert expected_message in invalid_result.stderr

    jump_skip_list = temporary / "address-taken-skip.txt"
    jump_skip_list.write_text("jump_dispatch\n", encoding="utf-8")
    omitted_result = run(
        TOOL,
        FIXTURES / "inputs" / "address_taken_jump_table",
        "--output",
        temporary / "address-taken-omitted-output",
        "--skip-list",
        jump_skip_list,
        expected=3,
    )
    assert "interior entry points must remain lifted" in omitted_result.stderr

    jump_abi = temporary / "address-taken-import.json"
    jump_abi.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "functions": [
                    {
                        "kind": "import",
                        "symbol": "jump_dispatch",
                        "header": "porpoise/stub.h",
                        "return": {"type": "void"},
                        "arguments": [],
                    }
                ],
            }
        ),
        encoding="utf-8",
    )
    imported_result = run(
        TOOL,
        FIXTURES / "inputs" / "address_taken_jump_table",
        "--output",
        temporary / "address-taken-imported-output",
        "--skip-list",
        jump_skip_list,
        "--abi",
        jump_abi,
        expected=3,
    )
    assert "interior entry points must remain lifted" in imported_result.stderr

    unresolved = temporary / "unresolved.s"
    unresolved.write_text(
        "# 0x80011000..0x80011004 | size: 0x4\n"
        ".data\n"
        "# .data:0x0 | 0x80011000 | size: 0x4\n"
        ".obj unresolved, global\n"
        "  .4byte missing_symbol\n"
        ".endobj unresolved\n",
        encoding="utf-8",
    )
    unresolved_result = run(
        TOOL,
        unresolved,
        "--output",
        temporary / "unresolved-output",
        expected=3,
    )
    assert "missing_symbol" in unresolved_result.stderr

    unsupported = temporary / "unsupported.s"
    unsupported.write_text(
        "# 0x80012000..0x80012004 | size: 0x4\n"
        ".data\n"
        "# .data:0x0 | 0x80012000 | size: 0x4\n"
        ".obj unsupported, global\n"
        "  .incbin \"payload.bin\"\n"
        ".endobj unsupported\n",
        encoding="utf-8",
    )
    unsupported_result = run(
        TOOL,
        unsupported,
        "--output",
        temporary / "unsupported-output",
        expected=3,
    )
    assert ".incbin" in unsupported_result.stderr

    uncovered = temporary / "uncovered.s"
    uncovered.write_text(
        "# 0x80013000..0x80013008 | size: 0x8\n"
        ".data\n"
        "# .data:0x0 | 0x80013000 | size: 0x4\n"
        ".obj partial_object, global\n"
        "  .4byte 0x12345678\n"
        ".endobj partial_object\n",
        encoding="utf-8",
    )
    uncovered_result = run(
        TOOL,
        uncovered,
        "--output",
        temporary / "uncovered-output",
        expected=3,
    )
    assert "unmaterialized bytes" in uncovered_result.stderr

    anonymous = temporary / "anonymous.s"
    anonymous.write_text(
        ".text\n"
        ".fn anonymous_entry, global\n"
        "/* 80008000 00000000  4E 80 00 20 */ blr\n"
        ".endfn anonymous_entry\n"
        "# 0x80014000..0x80014008 | size: 0x8\n"
        ".data\n"
        "# .data:0x0 | 0x80014000 | size: 0x4\n"
        ".obj explicit_object, global\n"
        "  .4byte 0x12345678\n"
        ".endobj explicit_object\n"
        "  .skip 4\n",
        encoding="utf-8",
    )
    anonymous_output = temporary / "anonymous-output"
    run(TOOL, anonymous, "--output", anonymous_output)
    anonymous_report = json.loads(
        (anonymous_output / "porpoise-report.json").read_text(encoding="utf-8")
    )
    assert anonymous_report["summary"]["anonymous_explicit_bytes"] == 4
    assert [item["kind"] for item in anonymous_report["data_spans"]] == [
        "initialized",
        "zero_fill",
    ]

    orphaned_range = temporary / "orphaned-range.s"
    orphaned_range.write_text(
        "# 0x80015000..0x80015004 | size: 0x4\n",
        encoding="utf-8",
    )
    orphaned_result = run(
        TOOL,
        orphaned_range,
        "--output",
        temporary / "orphaned-range-output",
        expected=3,
    )
    assert "not followed by a section selector" in orphaned_result.stderr
