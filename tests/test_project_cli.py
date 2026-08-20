#!/usr/bin/env python3
"""End-to-end contract checks for project-mode CLI execution."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile


def run(tool: pathlib.Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(tool), *arguments],
        check=False,
        encoding="utf-8",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def target(identifier: str, enabled: bool, source: pathlib.Path,
           output: pathlib.Path, skip: pathlib.Path) -> dict[str, object]:
    return {
        "id": identifier,
        "enabled": enabled,
        "source_kind": "assembly",
        "input": str(source),
        "output": str(output),
        "entry": "lift_me",
        "strict": False,
        "sdk_policy": "keep",
        "symbol_sources": [],
        "skip_list": str(skip),
        "overrides": [],
        "annotations": [],
        "cache": None,
    }


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: test_project_cli.py PORPOISE SOURCE_ROOT", file=sys.stderr)
        return 2
    tool = pathlib.Path(sys.argv[1]).resolve()
    source_root = pathlib.Path(sys.argv[2]).resolve()
    fixture = source_root / "tests" / "fixtures" / "session_plan"

    with tempfile.TemporaryDirectory(prefix="porpoise-project-cli-") as raw:
        temporary = pathlib.Path(raw)
        project_path = temporary / "recovery.porpoise.json"
        report_path = temporary / "analysis-report.json"
        project = {
            "schema_version": 1,
            "sdk_catalogs": [],
            "abi_contracts": [str(fixture / "abi.json")],
            "targets": [
                target("main", True, fixture / "input",
                       temporary / "out-main", fixture / "skip.txt"),
                target("overlay", False, fixture / "input",
                       temporary / "out-overlay", fixture / "skip.txt"),
            ],
        }
        project_path.write_text(json.dumps(project, indent=2), encoding="utf-8")

        analyzed = run(
            tool, "--project", str(project_path), "--analyze-only",
            "--report", str(report_path),
        )
        assert analyzed.returncode == 0, analyzed.stderr
        assert "Analyzed 1 project target(s)" in analyzed.stdout
        assert not (temporary / "out-main").exists()
        report = json.loads(report_path.read_text(encoding="utf-8"))
        assert report["schema_version"] == 3
        assert [item["id"] for item in report["targets"]] == ["main"]

        selected = run(
            tool, "--project", str(project_path), "--target", "overlay",
            "--analyze-only", "--quiet",
        )
        assert selected.returncode == 0, selected.stderr
        assert selected.stdout == ""

        generated = run(tool, "--project", str(project_path), "--force")
        assert generated.returncode == 0, generated.stderr
        assert "Generated 1 project target(s) transactionally" in generated.stdout
        assert (temporary / "out-main" / "meson.build").is_file()
        assert (temporary / "out-main" / "porpoise-report.json").is_file()
        assert not (temporary / "out-overlay").exists()

        conflict = run(
            tool, "--project", str(project_path),
            str(fixture / "input"), "--output", str(temporary / "bad"),
        )
        assert conflict.returncode == 2
        assert "mutually exclusive" in conflict.stderr

        missing = run(
            tool, "--project", str(project_path),
            "--target", "does-not-exist", "--analyze-only",
        )
        assert missing.returncode == 2
        assert "no target named" in missing.stderr

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
