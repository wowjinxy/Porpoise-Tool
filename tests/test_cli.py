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
    assert not any(no_entry.glob("subprojects/*.wrap"))
    run("meson", "setup", "build", "--wrap-mode=forcefallback", *CHILD_MESON_ARGS, cwd=no_entry)
    run("meson", "compile", "-C", "build", cwd=no_entry)

    opcodes = temporary / "opcodes"
    run(TOOL, FIXTURES / "inputs" / "opcodes", "--output", opcodes)
    harness = opcodes / "tests" / "semantic_harness.c"
    harness.parent.mkdir(parents=True)
    harness.write_text(
        "#include <assert.h>\n"
        "#include \"porpoise_generated.h\"\n"
        "#include \"porpoise_libporpoise_adapter.h\"\n"
        "#include <porpoise/stub.h>\n"
        "int main(void) {\n"
        "  PorpoiseHostAdapter host; PorpoisePpcState state;\n"
        "  assert(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);\n"
        "  porpoise_state_init(&state, &host);\n"
        "  porpoise_lifted_integer_semantics(&state);\n"
        "  assert(!porpoise_state_has_fault(&state));\n"
        "  assert(state.gpr[3] == 7U && state.gpr[5] == 3U);\n"
        "  assert(state.gpr[6] == 4U && state.gpr[8] == 2U && state.gpr[9] == 8U);\n"
        "  state.fpr[1].f64 = 1.25; state.fpr[2].f64 = 2.5;\n"
        "  porpoise_lifted_scalar_float_semantics(&state);\n"
        "  assert(state.fpr[3].f64 == 3.75);\n"
        "  state.fpr[1].ps[0] = 1.0F; state.fpr[1].ps[1] = 2.0F;\n"
        "  state.fpr[2].ps[0] = 3.0F; state.fpr[2].ps[1] = 4.0F;\n"
        "  porpoise_lifted_paired_float_semantics(&state);\n"
        "  assert(state.fpr[3].ps[0] == 4.0F && state.fpr[3].ps[1] == 6.0F);\n"
        "  porpoise_lifted_direct_branch_semantics(&state);\n"
        "  assert(state.gpr[3] == 5U);\n"
        "  porpoise_lifted_indirect_branch_semantics(&state);\n"
        "  assert(state.gpr[3] == 11U);\n"
        "  porpoise_lifted_fault_propagation(&state);\n"
        "  assert(porpoise_state_has_fault(&state));\n"
        "  assert(state.gpr[5] == 1U);\n"
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
    assert "DolphinMain" in (with_entry / "src" / "porpoise_entry.c").read_text(encoding="utf-8")
    assert "__start" not in (with_entry / "src" / "porpoise_entry.c").read_text()
    add_stub(with_entry)
    run("meson", "setup", "build", "--wrap-mode=forcefallback", *CHILD_MESON_ARGS, cwd=with_entry)
    run("meson", "compile", "-C", "build", cwd=with_entry)
    executable = with_entry / "build" / ("porpoise_title.exe" if os.name == "nt" else "porpoise_title")
    run(executable, cwd=with_entry)

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
        "#include <assert.h>\n"
        "#include \"porpoise_generated.h\"\n"
        "#include \"porpoise_libporpoise_adapter.h\"\n"
        "#include <porpoise/stub.h>\n"
        "int main(void) {\n"
        "  PorpoiseHostAdapter host; PorpoisePpcState state;\n"
        "  assert(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);\n"
        "  porpoise_state_init(&state, &host);\n"
        "  state.gpr[3] = 1U; state.gpr[4] = 2U;\n"
        "  state.fpr[1].f64 = 1.5; state.fpr[2].f64 = 2.25;\n"
        "  porpoise_lifted_call_imports(&state);\n"
        "  assert(!porpoise_state_has_fault(&state));\n"
        "  assert(state.gpr[3] == 0x80000003U);\n"
        "  assert(state.fpr[1].f64 == 3.75);\n"
        "  assert(PorpoiseStubReportCount() == 1U);\n"
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
        "#include <assert.h>\n"
        "#include \"porpoise_exports.h\"\n"
        "#include \"porpoise_libporpoise_adapter.h\"\n"
        "int main(void) {\n"
        "  PorpoiseHostAdapter host; PorpoisePpcState state;\n"
        "  assert(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);\n"
        "  porpoise_state_init(&state, &host); porpoise_bind_export_state(&state);\n"
        "  assert(PorpoiseAddOne(41U) == 42U);\n"
        "  assert(PorpoiseAddFloat(1.25F, 2.5F) == 3.75F);\n"
        "  assert(PorpoiseAddDouble(1.25, 2.5) == 3.75);\n"
        "  porpoise_bind_export_state(0);\n"
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
