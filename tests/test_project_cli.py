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


def output_files(output: pathlib.Path) -> list[pathlib.Path]:
    return sorted(
        path.relative_to(output)
        for path in output.rglob("*")
        if path.is_file()
    )


def snapshot_tree(output: pathlib.Path) -> dict[str, bytes]:
    return {
        path.relative_to(output).as_posix(): path.read_bytes()
        for path in output.rglob("*")
        if path.is_file()
    }


def normalized_report(output: pathlib.Path) -> dict[str, object]:
    report = json.loads(
        (output / "porpoise-report.json").read_text(encoding="utf-8")
    )
    target_identity = report["target"]
    assert isinstance(target_identity, dict)
    # Project mode records its requested target and bound plan digest. Classic
    # mode and the direct API intentionally have no project target context.
    target_identity.pop("id")
    target_identity.pop("plan_digest")
    return report


def assert_equivalent_output(
    reference: pathlib.Path,
    candidate: pathlib.Path,
) -> None:
    reference_files = output_files(reference)
    candidate_files = output_files(candidate)
    assert candidate_files == reference_files
    for relative in reference_files:
        if relative.as_posix() == "porpoise-report.json":
            continue
        assert (candidate / relative).read_bytes() == (
            reference / relative
        ).read_bytes(), relative.as_posix()
    assert normalized_report(candidate) == normalized_report(reference)


def main() -> int:
    if len(sys.argv) != 4:
        print(
            "usage: test_project_cli.py PORPOISE API_HELPER SOURCE_ROOT",
            file=sys.stderr,
        )
        return 2
    tool = pathlib.Path(sys.argv[1]).resolve()
    api_helper = pathlib.Path(sys.argv[2]).resolve()
    source_root = pathlib.Path(sys.argv[3]).resolve()
    fixture = source_root / "tests" / "fixtures" / "session_plan"

    with tempfile.TemporaryDirectory(prefix="porpoise-project-cli-") as raw:
        temporary = pathlib.Path(raw)
        project_path = temporary / "recovery.porpoise.json"
        report_path = temporary / "analysis-report.json"
        classic_output = temporary / "classic" / "generated"
        api_output = temporary / "api" / "generated"
        project_output = temporary / "project" / "generated"
        project = {
            "schema_version": 1,
            "sdk_catalogs": [],
            "abi_contracts": [str(fixture / "abi.json")],
            "targets": [
                target("main", True, fixture / "input",
                       project_output, fixture / "skip.txt"),
                target("overlay", False, fixture / "input",
                       temporary / "out-overlay", fixture / "skip.txt"),
            ],
        }
        project_path.write_text(json.dumps(project, indent=2), encoding="utf-8")

        report_directory = temporary / "existing-report-directory"
        report_directory.mkdir()
        (report_directory / "sentinel.bin").write_bytes(
            b"report-directory\x00bytes"
        )
        report_directory_before = snapshot_tree(report_directory)
        rejected_directory = run(
            tool, "--project", str(project_path), "--analyze-only",
            "--report", str(report_directory),
        )
        assert rejected_directory.returncode == 2
        assert "report path names an existing directory" in (
            rejected_directory.stderr
        )
        assert report_directory.is_dir()
        assert snapshot_tree(report_directory) == report_directory_before
        assert not project_output.exists()

        project_output.mkdir(parents=True)
        (project_output / "sentinel.bin").write_bytes(
            b"overlapping-output\x00bytes"
        )
        output_before = snapshot_tree(project_output)
        overlapping_report = project_output / "aggregate-report.json"
        rejected_overlap = run(
            tool, "--project", str(project_path),
            "--report", str(overlapping_report), "--force",
        )
        assert rejected_overlap.returncode == 2
        assert "report path overlaps output" in rejected_overlap.stderr
        assert project_output.is_dir()
        assert snapshot_tree(project_output) == output_before
        assert not overlapping_report.exists()
        (project_output / "sentinel.bin").unlink()
        project_output.rmdir()

        analyzed = run(
            tool, "--project", str(project_path), "--analyze-only",
            "--report", str(report_path),
        )
        assert analyzed.returncode == 0, analyzed.stderr
        assert "Analyzed 1 project target(s)" in analyzed.stdout
        assert not project_output.exists()
        report = json.loads(report_path.read_text(encoding="utf-8"))
        assert report["schema_version"] == 3
        assert [item["id"] for item in report["targets"]] == ["main"]
        assert report["targets"][0]["match_cache_hit"] is False
        assert report["targets"][0]["match_cache_refreshed"] is True
        persisted = json.loads(project_path.read_text(encoding="utf-8"))
        main_target = persisted["targets"][0]
        assert main_target["cache"] is not None
        assert len(main_target["cache"]["input_sha256"]) == 64
        assert len(main_target["cache"]["settings_sha256"]) == 64

        cache_hit_report = temporary / "cache-hit-report.json"
        cache_hit = run(
            tool, "--project", str(project_path), "--analyze-only",
            "--report", str(cache_hit_report),
        )
        assert cache_hit.returncode == 0, cache_hit.stderr
        reloaded_report = json.loads(
            cache_hit_report.read_text(encoding="utf-8")
        )
        assert reloaded_report["targets"][0]["match_cache_hit"] is True
        assert reloaded_report["targets"][0]["match_cache_refreshed"] is False

        persisted = json.loads(project_path.read_text(encoding="utf-8"))
        old_settings_hash = persisted["targets"][0]["cache"][
            "settings_sha256"
        ]
        persisted["targets"][0]["strict"] = True
        project_path.write_text(
            json.dumps(persisted, indent=2), encoding="utf-8"
        )
        invalidated_report = temporary / "cache-invalidated-report.json"
        invalidated = run(
            tool, "--project", str(project_path), "--analyze-only",
            "--report", str(invalidated_report),
        )
        assert invalidated.returncode == 0, invalidated.stderr
        stale_report = json.loads(
            invalidated_report.read_text(encoding="utf-8")
        )
        assert stale_report["targets"][0]["match_cache_hit"] is False
        assert stale_report["targets"][0]["match_cache_refreshed"] is True
        refreshed = json.loads(project_path.read_text(encoding="utf-8"))
        assert refreshed["targets"][0]["strict"] is True
        assert refreshed["targets"][0]["cache"]["settings_sha256"] != (
            old_settings_hash
        )
        # Restore the fixture's generation behavior. This deliberately leaves
        # the refreshed cache stale so the later generation run exercises the
        # same pre-plan invalidation path once more.
        refreshed["targets"][0]["strict"] = False
        project_path.write_text(
            json.dumps(refreshed, indent=2), encoding="utf-8"
        )

        selected = run(
            tool, "--project", str(project_path), "--target", "overlay",
            "--analyze-only", "--quiet",
        )
        assert selected.returncode == 0, selected.stderr
        assert selected.stdout == ""

        classic = run(
            tool,
            str(fixture / "input"),
            "--output", str(classic_output),
            "--abi", str(fixture / "abi.json"),
            "--skip-list", str(fixture / "skip.txt"),
            "--entry", "lift_me",
            "--sdk-policy", "keep",
        )
        assert classic.returncode == 0, classic.stderr

        direct_api = run(api_helper, str(source_root), str(api_output))
        assert direct_api.returncode == 0, direct_api.stderr

        generated = run(tool, "--project", str(project_path), "--force")
        assert generated.returncode == 0, generated.stderr
        assert "Generated 1 project target(s) transactionally" in generated.stdout
        assert (project_output / "meson.build").is_file()
        assert (project_output / "porpoise-report.json").is_file()
        assert not (temporary / "out-overlay").exists()

        classic_target = json.loads(
            (classic_output / "porpoise-report.json").read_text(
                encoding="utf-8"
            )
        )["target"]
        api_target = json.loads(
            (api_output / "porpoise-report.json").read_text(encoding="utf-8")
        )["target"]
        project_target = json.loads(
            (project_output / "porpoise-report.json").read_text(
                encoding="utf-8"
            )
        )["target"]
        assert classic_target["id"] is None
        assert classic_target["plan_digest"] is None
        assert api_target["id"] is None
        assert api_target["plan_digest"] is None
        assert project_target["id"] == "main"
        assert len(project_target["plan_digest"]) == 64
        assert all(
            character in "0123456789abcdef"
            for character in project_target["plan_digest"]
        )

        # This is the same session_plan fixture used by test_session_plan.c for
        # direct-plan versus compatibility-wrapper parity. Here the direct
        # session/plan API, classic CLI, and project CLI must also agree.
        assert_equivalent_output(classic_output, api_output)
        assert_equivalent_output(classic_output, project_output)

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
