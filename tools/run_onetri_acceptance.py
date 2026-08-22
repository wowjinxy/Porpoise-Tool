#!/usr/bin/env python3
"""Run the external-project OneTri local acceptance gate.

The ELF, map, generated assembly, catalog, and outputs always remain outside
the repository. Analysis variants are derived from the reviewed schema-v2
project, and boot uses Porpoise's shared project Build/Run core.
"""

from __future__ import annotations

import argparse
from collections import Counter
import copy
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
from typing import Any


AUTOMATIC_CATEGORIES = {"nintendo_dolphin", "demo"}
REPORT_ONLY_CATEGORIES = {"crt_msl", "runtime", "metrotrk", "debugger", "stub"}
POLICIES = ("keep", "imported", "omit")
ACTION_NAMES = ("lift", "import", "omit", "data")
REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[1]
EXPECTED_MAP_BACKED = (6, 521, 196)
EXPECTED_POLICY_ACTIONS = {
    "keep": {"lift": 723, "import": 0, "omit": 0, "data": 0},
    "imported": {"lift": 671, "import": 52, "omit": 0, "data": 0},
    "omit": {"lift": 335, "import": 52, "omit": 336, "data": 0},
}
EXPECTED_OMIT_REMOVED_BODIES = 388
EXPECTED_MAPLESS = (78, 480, 165, 533)
BUILTIN_EXACT_CONTRACT_IDENTITIES = {
    "demo.a/DEMOPad.c/DEMOPadInit",
    "dvd.a/dvd.c/DVDInit",
    "gx.a/GXAttr.c/GXClearVtxDesc",
    "gx.a/GXAttr.c/GXInvalidateVtxCache",
    "gx.a/GXAttr.c/GXSetNumTexGens",
    "gx.a/GXAttr.c/GXSetVtxAttrFmt",
    "gx.a/GXAttr.c/GXSetVtxDesc",
    "gx.a/GXFrameBuf.c/GXGetYScaleFactor",
    "gx.a/GXFrameBuf.c/GXSetDispCopyGamma",
    "gx.a/GXFrameBuf.c/GXSetDispCopySrc",
    "gx.a/GXFrameBuf.c/GXSetDispCopyYScale",
    "gx.a/GXGeometry.c/GXBegin",
    "gx.a/GXLight.c/GXSetNumChans",
    "gx.a/GXMisc.c/GXDrawDone",
    "gx.a/GXPixel.c/GXSetColorUpdate",
    "gx.a/GXPixel.c/GXSetPixelFmt",
    "gx.a/GXPixel.c/GXSetZMode",
    "gx.a/GXTev.c/GXSetNumTevStages",
    "gx.a/GXTev.c/GXSetTevOp",
    "gx.a/GXTev.c/GXSetTevOrder",
    "gx.a/GXTexture.c/GXInvalidateTexAll",
    "gx.a/GXTransform.c/GXSetScissor",
    "gx.a/GXTransform.c/GXSetViewport",
    "os.a/OS.c/OSInit",
    "pad.a/Pad.c/PADRead",
    "vi.a/vi.c/VIFlush",
    "vi.a/vi.c/VIInit",
    "vi.a/vi.c/VISetBlack",
    "vi.a/vi.c/VIWaitForRetrace",
}
EXPECTED_BOOT_MILESTONES = (
    "main",
    "DEMOInit",
    "CameraInit",
    "DrawInit",
    "PrintIntro",
)
EXPECTED_LOOP_MILESTONE = "PADRead"


def path_contains(parent: pathlib.Path, child: pathlib.Path) -> bool:
    """Return whether the resolved child is parent or is beneath parent."""
    resolved_parent = parent.resolve()
    resolved_child = child.resolve()
    try:
        resolved_child.relative_to(resolved_parent)
        return True
    except ValueError:
        return False


def require_disjoint_paths(
    left: pathlib.Path,
    left_label: str,
    right: pathlib.Path,
    right_label: str,
) -> None:
    if path_contains(left, right) or path_contains(right, left):
        raise ValueError(
            f"{left_label} '{left.resolve()}' and {right_label} "
            f"'{right.resolve()}' must not contain one another"
        )


def validate_output_roots(
    work_root: pathlib.Path,
    libporpoise: pathlib.Path,
) -> None:
    require_disjoint_paths(
        REPOSITORY_ROOT, "Porpoise repository", work_root, "acceptance work root"
    )
    require_disjoint_paths(
        REPOSITORY_ROOT, "Porpoise repository", libporpoise, "libPorpoise root"
    )
    require_disjoint_paths(
        libporpoise, "libPorpoise root", work_root, "acceptance work root"
    )


def validate_external_material_paths(
    work_root: pathlib.Path,
    libporpoise: pathlib.Path,
    project_root: pathlib.Path,
    materials: tuple[tuple[pathlib.Path, str], ...],
) -> None:
    """Keep the owned work tree disjoint from every external input/output."""
    validate_output_roots(work_root, libporpoise)
    require_disjoint_paths(
        work_root,
        "acceptance work root",
        project_root,
        "reviewed project root",
    )
    require_disjoint_paths(
        REPOSITORY_ROOT,
        "Porpoise repository",
        project_root,
        "reviewed project root",
    )
    require_disjoint_paths(
        libporpoise,
        "libPorpoise root",
        project_root,
        "reviewed project root",
    )
    for path, label in materials:
        require_disjoint_paths(
            work_root,
            "acceptance work root",
            path,
            f"OneTri {label}",
        )
        require_disjoint_paths(
            REPOSITORY_ROOT,
            "Porpoise repository",
            path,
            f"OneTri {label}",
        )
        require_disjoint_paths(
            libporpoise,
            "libPorpoise root",
            path,
            f"OneTri {label}",
        )


def validate_trace_destination(
    trace_path: pathlib.Path,
    occupied: tuple[tuple[pathlib.Path, str], ...],
) -> None:
    """Reject trace files that can overwrite or contain an owned path."""
    for path, label in occupied:
        require_disjoint_paths(
            path,
            label,
            trace_path,
            "trace output",
        )


def create_run_directory(work_root: pathlib.Path) -> pathlib.Path:
    work_root.mkdir(parents=True, exist_ok=True)
    if not work_root.is_dir():
        raise ValueError(f"acceptance work root '{work_root}' is not a directory")
    work = pathlib.Path(
        tempfile.mkdtemp(prefix="onetri-run-", dir=work_root)
    ).resolve()
    (work / ".porpoise-onetri-acceptance-run-v1").write_text(
        "schema_version=1\n", encoding="utf-8", newline="\n"
    )
    return work


def invoke(
    arguments: list[str],
    description: str,
    *,
    environment: dict[str, str] | None = None,
) -> None:
    process = subprocess.run(
        arguments,
        check=False,
        text=True,
        env=environment,
    )
    if process.returncode != 0:
        raise RuntimeError(f"{description} failed with exit code {process.returncode}")


def read_runtime_trace(path: pathlib.Path) -> list[dict[str, Any]]:
    """Read a complete JSONL trace and reject truncated or non-object events."""
    events: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as trace:
        for line_number, line in enumerate(trace, 1):
            if not line.strip():
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError as error:
                raise AssertionError(
                    f"runtime trace line {line_number} is invalid JSON: {error.msg}"
                ) from error
            if not isinstance(event, dict):
                raise AssertionError(
                    f"runtime trace line {line_number} is not an object"
                )
            events.append(event)
    if not events:
        raise AssertionError("runtime trace is empty")
    return events


def verify_boot_trace(
    path: pathlib.Path,
    *,
    minimum_frames: int,
    reject_approximations: bool = True,
) -> dict[str, Any]:
    """Verify the deterministic first-boot evidence emitted by the runtime."""
    if minimum_frames < 1:
        raise ValueError("minimum_frames must be positive")
    events = read_runtime_trace(path)
    faults = [event for event in events if event.get("event") == "fault"]
    if faults:
        first = faults[0]
        raise AssertionError(
            "runtime trace contains a guest fault at "
            f"{first.get('pc', 'unknown PC')}: "
            f"{first.get('message', first.get('fault', 'unknown fault'))}"
        )

    approximations = [
        event for event in events if event.get("event") == "approximate"
    ]
    if reject_approximations and approximations:
        first = approximations[0]
        raise AssertionError(
            "runtime trace reached an unreviewed approximation at "
            f"{first.get('address', first.get('pc', 'unknown PC'))} "
            f"({first.get('mnemonic', 'unknown instruction')})"
        )

    milestone_index = 0
    milestone_event_index = -1
    for event_index, event in enumerate(events):
        if (
            event.get("event") == "call"
            and event.get("phase") == "enter"
            and event.get("function") == EXPECTED_BOOT_MILESTONES[milestone_index]
        ):
            milestone_index += 1
            milestone_event_index = event_index
            if milestone_index == len(EXPECTED_BOOT_MILESTONES):
                break
    if milestone_index != len(EXPECTED_BOOT_MILESTONES):
        raise AssertionError(
            "runtime trace did not reach the ordered boot milestone "
            f"'{EXPECTED_BOOT_MILESTONES[milestone_index]}'"
        )
    reached_loop = any(
        event.get("event") == "call"
        and event.get("phase") == "enter"
        and event.get("function") == EXPECTED_LOOP_MILESTONE
        for event in events[milestone_event_index + 1 :]
    )
    if not reached_loop:
        raise AssertionError(
            "runtime trace did not reach loop milestone "
            f"'{EXPECTED_LOOP_MILESTONE}' after "
            f"'{EXPECTED_BOOT_MILESTONES[-1]}'"
        )

    frames = [event for event in events if event.get("event") == "frame"]
    if len(frames) < minimum_frames:
        raise AssertionError(
            f"runtime trace presented {len(frames)} frames; "
            f"expected at least {minimum_frames}"
        )
    for index, frame in enumerate(frames, 1):
        frame_number = frame.get("frame")
        frame_buffer = frame.get("frame_buffer")
        try:
            parsed_buffer = (
                int(frame_buffer, 0) if isinstance(frame_buffer, str)
                else int(frame_buffer)
            )
        except (TypeError, ValueError) as error:
            raise AssertionError(
                f"runtime frame {index} has an invalid framebuffer"
            ) from error
        if parsed_buffer == 0:
            raise AssertionError(
                f"runtime frame {index} has a zero guest framebuffer"
            )
        if frame_number != index:
            raise AssertionError(
                "runtime frame sequence is not contiguous: "
                f"got {frame_number!r}, expected {index}"
            )

    observed_frames = [
        frame
        for frame in frames
        if isinstance(frame.get("content_hash"), str)
        and frame.get("content_varied") is not None
    ]
    if len(observed_frames) != len(frames):
        raise AssertionError(
            "runtime trace did not observe the complete content of every "
            "presented framebuffer"
        )
    varied_frames = [
        frame for frame in observed_frames if frame.get("content_varied") is True
    ]
    if not varied_frames:
        raise AssertionError(
            "runtime trace presented only uniform framebuffer content; "
            "expected at least one nonempty rendered frame"
        )

    return {
        "events": len(events),
        "frames": len(frames),
        "varied_frames": len(varied_frames),
        "milestones": list(EXPECTED_BOOT_MILESTONES),
        "loop_milestone": EXPECTED_LOOP_MILESTONE,
        "approximations": len(approximations),
    }


def resolve_project_path(
    project_path: pathlib.Path,
    value: object,
    label: str,
) -> pathlib.Path:
    if not isinstance(value, str) or not value:
        raise AssertionError(f"reviewed project has an invalid {label} path")
    path = pathlib.Path(value)
    if not path.is_absolute():
        path = project_path.parent / path
    return path.resolve()


def load_reviewed_project(
    project_path: pathlib.Path,
    target_id: str,
) -> tuple[dict[str, Any], dict[str, Any]]:
    """Load the external schema-v2 project used by the shared Build/Run core."""
    try:
        document = json.loads(project_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise AssertionError(
            f"reviewed project is invalid JSON: {error.msg}"
        ) from error
    if not isinstance(document, dict) or document.get("schema_version") != 2:
        raise AssertionError("OneTri acceptance requires a schema-version-2 project")
    targets = document.get("targets")
    if not isinstance(targets, list):
        raise AssertionError("reviewed project has no targets array")
    matches = [
        target
        for target in targets
        if isinstance(target, dict) and target.get("id") == target_id
    ]
    if len(matches) != 1:
        raise AssertionError(
            f"reviewed project must contain exactly one target named '{target_id}'"
        )
    target = matches[0]
    if target.get("source_kind") != "managed_elf":
        raise AssertionError("OneTri target must use managed_elf source input")
    if target.get("sdk_policy") != "imported":
        raise AssertionError(
            "reviewed OneTri target must explicitly use SDK policy 'imported'"
        )
    if not isinstance(target.get("title_host"), dict):
        raise AssertionError(
            "reviewed OneTri target has no title-host profile; review it in the GUI"
        )
    return document, target


def selected_project_inputs(
    project_path: pathlib.Path,
    document: dict[str, Any],
    target: dict[str, Any],
) -> tuple[pathlib.Path, pathlib.Path, pathlib.Path, pathlib.Path]:
    """Resolve the authoritative ELF, map, catalog, and output from the project."""
    elf = resolve_project_path(project_path, target.get("input"), "target input")
    output = resolve_project_path(project_path, target.get("output"), "target output")
    symbols = target.get("symbol_sources")
    maps = (
        [
            symbol
            for symbol in symbols
            if isinstance(symbol, dict)
            and symbol.get("kind") == "codewarrior_map"
        ]
        if isinstance(symbols, list)
        else []
    )
    if len(maps) != 1:
        raise AssertionError(
            "reviewed OneTri target must contain exactly one CodeWarrior map"
        )
    map_path = resolve_project_path(
        project_path, maps[0].get("path"), "CodeWarrior map"
    )
    catalogs = document.get("sdk_catalogs")
    if not isinstance(catalogs, list) or len(catalogs) != 1:
        raise AssertionError(
            "reviewed OneTri project must contain exactly one exact SDK catalog"
        )
    catalog = resolve_project_path(project_path, catalogs[0], "SDK catalog")
    return elf, map_path, catalog, output


def assert_legacy_path_matches(
    supplied: pathlib.Path | None,
    authoritative: pathlib.Path,
    option: str,
) -> None:
    """Treat legacy semantic path options as assertions, never overrides."""
    if supplied is None:
        return
    if supplied.resolve() != authoritative.resolve():
        raise ValueError(
            f"{option} does not match the authoritative path in --project: "
            f"{supplied.resolve()} != {authoritative.resolve()}"
        )


def absolute_analysis_project(
    project_path: pathlib.Path,
    document: dict[str, Any],
    target: dict[str, Any],
    output: pathlib.Path,
    policy: str,
    *,
    include_map: bool,
    include_catalog: bool,
) -> dict[str, Any]:
    """Clone one target for analysis while preserving reviewed project evidence."""
    if policy not in POLICIES:
        raise ValueError(f"unknown SDK policy '{policy}'")
    derived = copy.deepcopy(document)
    derived_target = copy.deepcopy(target)
    derived["targets"] = [derived_target]
    derived["sdk_catalogs"] = (
        [
            str(resolve_project_path(project_path, value, "SDK catalog"))
            for value in document.get("sdk_catalogs", [])
        ]
        if include_catalog
        else []
    )
    derived["abi_contracts"] = [
        str(resolve_project_path(project_path, value, "ABI contract"))
        for value in document.get("abi_contracts", [])
    ]
    derived_target["enabled"] = True
    derived_target["input"] = str(
        resolve_project_path(project_path, target.get("input"), "target input")
    )
    derived_target["output"] = str(output.resolve())
    derived_target["sdk_policy"] = policy
    skip_list = target.get("skip_list")
    derived_target["skip_list"] = None if skip_list is None else str(
        resolve_project_path(project_path, skip_list, "skip list")
    )
    symbols: list[dict[str, Any]] = []
    if include_map:
        source_symbols = target.get("symbol_sources")
        if not isinstance(source_symbols, list):
            raise AssertionError("reviewed project has an invalid symbol_sources array")
        for source_symbol in source_symbols:
            symbol = copy.deepcopy(source_symbol)
            if not isinstance(symbol, dict):
                raise AssertionError("reviewed project has an invalid symbol source")
            symbol["path"] = str(
                resolve_project_path(project_path, symbol.get("path"), "symbol source")
            )
            auxiliary = symbol.get("auxiliary_path")
            if auxiliary is not None:
                symbol["auxiliary_path"] = str(
                    resolve_project_path(
                        project_path, auxiliary, "auxiliary symbol source"
                    )
                )
            symbols.append(symbol)
    derived_target["symbol_sources"] = symbols
    derived_target["cache"] = None
    return derived


def analyze(
    porpoise: pathlib.Path,
    dtk: pathlib.Path,
    work: pathlib.Path,
    reviewed_project_path: pathlib.Path,
    reviewed_document: dict[str, Any],
    reviewed_target: dict[str, Any],
    policy: str,
    *,
    include_map: bool,
    include_catalog: bool,
) -> dict[str, Any]:
    evidence = "map" if include_map else "mapless"
    evidence += "-catalog" if include_catalog else "-no-catalog"
    project_path = work / f"onetri-{policy}-{evidence}.porpoise.json"
    report_path = project_path.with_suffix(".report.json")
    output = work / f"output-{policy}-{evidence}"
    project_path.write_text(
        json.dumps(
            absolute_analysis_project(
                reviewed_project_path,
                reviewed_document,
                reviewed_target,
                output,
                policy,
                include_map=include_map,
                include_catalog=include_catalog,
            ),
            indent=2,
        ) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    invoke(
        [
            str(porpoise),
            "--project",
            str(project_path),
            "--dtk",
            str(dtk),
            "--analyze-only",
            "--report",
            str(report_path),
        ],
        f"OneTri {policy} analysis",
    )
    return json.loads(report_path.read_text(encoding="utf-8"))


def functions(report: dict[str, Any]) -> list[dict[str, Any]]:
    targets = report.get("targets")
    if not isinstance(targets, list) or len(targets) != 1:
        raise AssertionError("acceptance report must contain exactly one target")
    items = targets[0].get("functions")
    if not isinstance(items, list):
        raise AssertionError("acceptance report has no function list")
    return items


def category_counts(items: list[dict[str, Any]]) -> tuple[int, int, int]:
    automatic = sum(item.get("category") in AUTOMATIC_CATEGORIES for item in items)
    report_only = sum(item.get("category") in REPORT_ONLY_CATEGORIES for item in items)
    title = len(items) - automatic - report_only
    return title, automatic, report_only


def action_counts(items: list[dict[str, Any]]) -> dict[str, int]:
    return {
        action: sum(item.get("resolved_action") == action for item in items)
        for action in ACTION_NAMES
    }


def structural_policy_fallback(item: dict[str, Any], policy: str) -> bool:
    if policy not in {"imported", "omit"}:
        return False
    requested = item.get("requested_action")
    return (
        item.get("resolved_action") == "lift"
        and item.get("origin") == "sdk-policy"
        and requested in ({"import"} if policy == "imported" else {"import", "omit"})
    )


def verify_policy(items: list[dict[str, Any]], policy: str) -> None:
    if policy not in POLICIES:
        raise ValueError(f"unknown SDK policy '{policy}'")
    exact_automatic = []
    invalid: list[dict[str, Any]] = []
    missing_bindings: list[dict[str, Any]] = []
    for item in items:
        eligible = (
            item.get("category") in AUTOMATIC_CATEGORIES
            and item.get("confidence") == "exact"
        )
        action = item.get("resolved_action")
        if eligible:
            exact_automatic.append(item)
        if policy == "keep":
            allowed = {"lift"}
        elif policy == "imported" and eligible:
            allowed = {"lift", "import"}
        elif policy == "omit" and eligible:
            allowed = {"import", "omit"}
            if structural_policy_fallback(item, policy):
                allowed.add("lift")
        else:
            allowed = {"lift"}
        if action not in allowed:
            invalid.append(item)
        if action == "import" and item.get("binding") is None:
            missing_bindings.append(item)
    if not exact_automatic:
        raise AssertionError("local catalog yielded no exact automatic SDK matches")
    if invalid:
        raise AssertionError(
            f"{policy} policy produced {len(invalid)} invalid dispositions"
        )
    if missing_bindings:
        raise AssertionError(
            f"{policy} policy produced {len(missing_bindings)} imports without bindings"
        )


def function_locator(item: dict[str, Any]) -> tuple[object, ...]:
    return (
        item.get("translation_unit"),
        item.get("section"),
        item.get("address"),
        item.get("size"),
        item.get("source_name"),
    )


def indexed_functions(
    items: list[dict[str, Any]],
    description: str,
) -> dict[tuple[object, ...], dict[str, Any]]:
    result: dict[tuple[object, ...], dict[str, Any]] = {}
    for item in items:
        locator = function_locator(item)
        if locator in result:
            raise AssertionError(f"{description} contains duplicate function locator {locator!r}")
        result[locator] = item
    return result


def policy_evidence(item: dict[str, Any]) -> tuple[object, ...]:
    return (
        item.get("canonical_name"),
        item.get("category"),
        item.get("confidence"),
        item.get("signature"),
        item.get("conflict"),
    )


def function_canonical_identity(item: dict[str, Any]) -> object:
    identity = item.get("canonical_name")
    return identity if identity is not None else item.get("canonical_sdk_identity")


def catalog_contract_identities(path: pathlib.Path) -> set[str]:
    document = json.loads(path.read_text(encoding="utf-8"))
    entries = document.get("entries") if isinstance(document, dict) else None
    if not isinstance(entries, list):
        raise AssertionError("SDK catalog has no entries array")
    identities: set[str] = set()
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict):
            raise AssertionError(f"SDK catalog entry {index} is not an object")
        contract = entry.get("contract")
        if contract is None:
            continue
        identity = entry.get("canonical_identity")
        if not isinstance(contract, str) or not contract or not isinstance(identity, str):
            raise AssertionError(
                f"SDK catalog entry {index} has an invalid contract identity"
            )
        identities.add(identity)
    return identities | BUILTIN_EXACT_CONTRACT_IDENTITIES


def verify_policy_matrix(
    policy_items: dict[str, list[dict[str, Any]]],
    contracted_identities: set[str],
) -> dict[str, dict[str, int]]:
    if set(policy_items) != set(POLICIES):
        raise AssertionError("policy matrix must contain keep, imported, and omit")
    indexed = {
        policy: indexed_functions(policy_items[policy], f"{policy} report")
        for policy in POLICIES
    }
    baseline = indexed["keep"]
    for policy in POLICIES:
        verify_policy(policy_items[policy], policy)
        if set(indexed[policy]) != set(baseline):
            raise AssertionError(f"{policy} report changed the function inventory")
        changed_evidence = [
            locator
            for locator, item in indexed[policy].items()
            if policy_evidence(item) != policy_evidence(baseline[locator])
        ]
        if changed_evidence:
            raise AssertionError(
                f"{policy} report changed evidence for {len(changed_evidence)} functions"
            )

    eligible = {
        locator
        for locator, item in baseline.items()
        if item.get("category") in AUTOMATIC_CATEGORIES
        and item.get("confidence") == "exact"
    }
    structurally_retained = {
        locator
        for locator in eligible
        if structural_policy_fallback(indexed["omit"][locator], "omit")
    }
    replaceable = eligible - structurally_retained
    imported_contracts = {
        locator
        for locator, item in indexed["imported"].items()
        if item.get("resolved_action") == "import"
    }
    omit_contracts = {
        locator
        for locator, item in indexed["omit"].items()
        if item.get("resolved_action") == "import"
    }
    expected_contracts = {
        locator
        for locator in replaceable
        if function_canonical_identity(baseline[locator]) in contracted_identities
    }
    if imported_contracts != expected_contracts:
        missing = expected_contracts - imported_contracts
        extra = imported_contracts - expected_contracts
        raise AssertionError(
            "imported policy disagrees with catalog host contracts: "
            f"missing={len(missing)}, extra={len(extra)}"
        )
    if imported_contracts != omit_contracts:
        raise AssertionError(
            "imported and omit policies selected different valid host contracts"
        )
    changed_bindings = [
        locator
        for locator in imported_contracts
        if indexed["imported"][locator].get("binding")
        != indexed["omit"][locator].get("binding")
    ]
    if changed_bindings:
        raise AssertionError(
            "imported and omit policies selected different bindings for "
            f"{len(changed_bindings)} functions"
        )
    total = len(baseline)
    imported_count = len(imported_contracts)
    replaceable_count = len(replaceable)
    expected = {
        "keep": {"lift": total, "import": 0, "omit": 0, "data": 0},
        "imported": {
            "lift": total - imported_count,
            "import": imported_count,
            "omit": 0,
            "data": 0,
        },
        "omit": {
            "lift": total - replaceable_count,
            "import": imported_count,
            "omit": replaceable_count - imported_count,
            "data": 0,
        },
    }
    for policy in POLICIES:
        actual = action_counts(policy_items[policy])
        if actual != expected[policy]:
            raise AssertionError(
                f"{policy} action counts changed: got {actual}, expected {expected[policy]}"
            )
    return expected


def exact_function_multiset(
    items: list[dict[str, Any]],
) -> Counter[tuple[object, ...]]:
    exact: Counter[tuple[object, ...]] = Counter()
    for item in items:
        if item.get("confidence") != "exact":
            continue
        canonical_identity = function_canonical_identity(item)
        exact[
            (
                item.get("address"),
                item.get("size"),
                canonical_identity,
                item.get("category"),
                item.get("signature"),
            )
        ] += 1
    return exact


def verify_mapless_exact_parity(
    map_items: list[dict[str, Any]],
    mapless_items: list[dict[str, Any]],
) -> None:
    map_exact = exact_function_multiset(map_items)
    mapless_exact = exact_function_multiset(mapless_items)
    if map_exact == mapless_exact:
        return
    missing = list((map_exact - mapless_exact).elements())
    extra = list((mapless_exact - map_exact).elements())
    raise AssertionError(
        "mapless exact identity set differs from map-backed analysis: "
        f"missing={len(missing)}, extra={len(extra)}; "
        f"first_missing={missing[:1]!r}, first_extra={extra[:1]!r}"
    )


def resolve_command(value: str, label: str) -> str:
    path = pathlib.Path(value)
    if path.is_file():
        return str(path.resolve())
    discovered = shutil.which(value)
    if discovered is None:
        raise FileNotFoundError(f"{label} executable '{value}'")
    return discovered


def shared_run_command(
    porpoise: pathlib.Path,
    dtk: pathlib.Path,
    project: pathlib.Path,
    target: str,
    libporpoise: pathlib.Path,
    meson: str,
    dvd_root: pathlib.Path,
    trace: pathlib.Path | None,
    report: pathlib.Path,
    frame_limit: int,
    build_type: str,
    cc: str | None,
    cxx: str | None,
) -> list[str]:
    """Construct the one authoritative shared project Build/Run invocation."""
    if frame_limit < 1:
        raise ValueError("frame limit must be positive")
    command = [
        str(porpoise),
        "--project",
        str(project),
        "--target",
        target,
        "--dtk",
        str(dtk),
        "--run",
        "--libporpoise",
        str(libporpoise),
        "--meson",
        meson,
        "--dvd-root",
        str(dvd_root),
        "--build-type",
        build_type,
        "--frame-limit",
        str(frame_limit),
        "--report",
        str(report),
        "--force",
    ]
    if trace is not None:
        command.extend(("--trace", str(trace)))
    if cc is not None:
        command.extend(("--cc", cc))
    if cxx is not None:
        command.extend(("--cxx", cxx))
    return command


def positive_integer(value: str) -> int:
    try:
        parsed = int(value, 10)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be a positive integer") from error
    if parsed < 1:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return parsed


def acceptance_run_environment() -> dict[str, str]:
    """Enable the test-only dynamic approximation rejection gate."""
    environment = dict(os.environ)
    environment["PORPOISE_REJECT_APPROXIMATIONS"] = "1"
    return environment


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--porpoise", type=pathlib.Path, required=True)
    parser.add_argument("--dtk", type=pathlib.Path, required=True)
    parser.add_argument("--project", type=pathlib.Path, required=True)
    parser.add_argument("--target", default="onetri")
    parser.add_argument(
        "--elf",
        type=pathlib.Path,
        help="legacy assertion; must match the selected project's input",
    )
    parser.add_argument(
        "--map",
        dest="map_path",
        type=pathlib.Path,
        help="legacy assertion; must match the selected project's map",
    )
    parser.add_argument("--work", type=pathlib.Path, required=True)
    parser.add_argument(
        "--catalog",
        type=pathlib.Path,
        help="legacy assertion; must match the selected project's catalog",
    )
    parser.add_argument("--catalog-tool", type=pathlib.Path)
    parser.add_argument("--build-catalog", action="store_true")
    parser.add_argument("--libporpoise", type=pathlib.Path, required=True)
    parser.add_argument("--meson", default="meson")
    parser.add_argument("--cc")
    parser.add_argument("--cxx")
    parser.add_argument("--dvd-root", type=pathlib.Path, required=True)
    parser.add_argument(
        "--build-type",
        choices=("debugoptimized", "debug", "release"),
        default="debugoptimized",
    )
    parser.add_argument(
        "--trace",
        type=pathlib.Path,
        help="JSONL evidence path (defaults inside the unique work directory)",
    )
    parser.add_argument(
        "--trace-frame-limit",
        type=positive_integer,
        default=3,
        help="frames in the strict traced boot (default: 3)",
    )
    parser.add_argument(
        "--frame-limit",
        type=positive_integer,
        default=300,
        help="frames in the sustained untraced run (default: 300)",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    porpoise = arguments.porpoise.resolve()
    dtk = arguments.dtk.resolve()
    project_path = arguments.project.resolve()
    work_root = arguments.work.resolve()
    libporpoise = arguments.libporpoise.resolve()
    dvd_root = arguments.dvd_root.resolve()
    catalog_tool: pathlib.Path | None = None
    meson = resolve_command(arguments.meson, "Meson")
    cc = None if arguments.cc is None else resolve_command(
        arguments.cc, "C compiler"
    )
    cxx = None if arguments.cxx is None else resolve_command(
        arguments.cxx, "C++ compiler"
    )
    if (cc is None) != (cxx is None):
        raise ValueError("--cc and --cxx must be supplied together")
    if arguments.trace_frame_limit > arguments.frame_limit:
        raise ValueError("--trace-frame-limit cannot exceed --frame-limit")
    for path in (porpoise, dtk, project_path):
        if not path.is_file():
            raise FileNotFoundError(path)
    if not (libporpoise / "meson.build").is_file():
        raise FileNotFoundError(libporpoise / "meson.build")
    if not dvd_root.is_dir():
        raise FileNotFoundError(dvd_root)

    reviewed_document, reviewed_target = load_reviewed_project(
        project_path, arguments.target
    )
    elf, map_path, catalog, generated_output = selected_project_inputs(
        project_path, reviewed_document, reviewed_target
    )
    assert_legacy_path_matches(arguments.elf, elf, "--elf")
    assert_legacy_path_matches(arguments.map_path, map_path, "--map")
    assert_legacy_path_matches(arguments.catalog, catalog, "--catalog")
    for path in (elf, map_path):
        if not path.is_file():
            raise FileNotFoundError(path)

    if arguments.build_catalog:
        if arguments.catalog_tool is None:
            raise ValueError("--build-catalog requires --catalog-tool")
        catalog_tool = arguments.catalog_tool.resolve()
        if not catalog_tool.is_file():
            raise FileNotFoundError(catalog_tool)
    elif not catalog.is_file():
        raise FileNotFoundError(catalog)
    external_material = (
        (project_path, "reviewed project"),
        (elf, "ELF"),
        (map_path, "map"),
        (catalog, "catalog"),
        (generated_output, "generated output"),
        (dvd_root, "DVD root"),
    )
    project_root = project_path.parent.resolve()
    validate_external_material_paths(
        work_root,
        libporpoise,
        project_root,
        external_material,
    )
    explicit_trace_path = (
        None if arguments.trace is None else arguments.trace.resolve()
    )
    if explicit_trace_path is not None:
        validate_trace_destination(
            explicit_trace_path,
            external_material + (
                (REPOSITORY_ROOT, "Porpoise repository"),
                (libporpoise, "libPorpoise root"),
                (work_root, "acceptance work root"),
                (project_root, "reviewed project root"),
            ),
        )
    work = create_run_directory(work_root)
    print(f"OneTri acceptance run directory: {work}", file=sys.stderr)
    trace_path = (
        (work / "onetri-first-boot.jsonl")
        if explicit_trace_path is None else explicit_trace_path
    )
    if explicit_trace_path is None:
        validate_trace_destination(
            trace_path,
            external_material + (
                (REPOSITORY_ROOT, "Porpoise repository"),
                (libporpoise, "libPorpoise root"),
                (project_root, "reviewed project root"),
            ),
        )

    map_report = analyze(
        porpoise,
        dtk,
        work,
        project_path,
        reviewed_document,
        reviewed_target,
        "keep",
        include_map=True,
        include_catalog=False,
    )
    title, automatic, report_only = category_counts(functions(map_report))
    expected = EXPECTED_MAP_BACKED
    if (title, automatic, report_only) != expected:
        raise AssertionError(
            "map-backed classification mismatch: "
            f"got title={title}, sdk={automatic}, report_only={report_only}; "
            f"expected {expected}"
        )

    if arguments.build_catalog:
        assert catalog_tool is not None
        invoke(
            [
                sys.executable,
                str(catalog_tool),
                "--elf",
                str(elf),
                "--map",
                str(map_path),
                "--dtk",
                str(dtk),
                "--output",
                str(catalog),
            ],
            "OneTri exact catalog generation",
        )

    if not catalog.is_file():
        raise FileNotFoundError(catalog)
    contracted_identities = catalog_contract_identities(catalog)
    policy_items: dict[str, list[dict[str, Any]]] = {}
    policy_summary: dict[str, dict[str, int]] = {}
    for policy in POLICIES:
        report = analyze(
            porpoise,
            dtk,
            work,
            project_path,
            reviewed_document,
            reviewed_target,
            policy,
            include_map=True,
            include_catalog=True,
        )
        items = functions(report)
        policy_items[policy] = items
        policy_summary[policy] = action_counts(items)
    expected_actions = verify_policy_matrix(policy_items, contracted_identities)
    if expected_actions != EXPECTED_POLICY_ACTIONS:
        raise AssertionError(
            "catalog-derived policy expectations changed: "
            f"got {expected_actions}, expected {EXPECTED_POLICY_ACTIONS}"
        )
    if policy_summary != EXPECTED_POLICY_ACTIONS:
        raise AssertionError(
            f"policy action counts changed: got {policy_summary}, "
            f"expected {EXPECTED_POLICY_ACTIONS}"
        )
    omit_removed = (
        policy_summary["omit"]["import"] + policy_summary["omit"]["omit"]
    )
    if omit_removed != EXPECTED_OMIT_REMOVED_BODIES:
        raise AssertionError(
            f"omit policy removed {omit_removed} bodies; "
            f"expected {EXPECTED_OMIT_REMOVED_BODIES}"
        )

    mapless = analyze(
        porpoise,
        dtk,
        work,
        project_path,
        reviewed_document,
        reviewed_target,
        "keep",
        include_map=False,
        include_catalog=True,
    )
    mapless_items = functions(mapless)
    verify_policy(mapless_items, "keep")
    if action_counts(mapless_items) != {
        "lift": len(mapless_items),
        "import": 0,
        "omit": 0,
        "data": 0,
    }:
        raise AssertionError("mapless keep policy did not lift every function")
    if len(mapless_items) != len(policy_items["keep"]):
        raise AssertionError("mapless analysis changed the function inventory")
    verify_mapless_exact_parity(policy_items["keep"], mapless_items)
    mapless_categories = category_counts(mapless_items)
    mapless_exact = sum(item.get("confidence") == "exact" for item in mapless_items)
    if (*mapless_categories, mapless_exact) != EXPECTED_MAPLESS:
        raise AssertionError(
            "mapless catalog mismatch: got "
            f"title={mapless_categories[0]}, sdk={mapless_categories[1]}, "
            f"report_only={mapless_categories[2]}, exact={mapless_exact}; "
            f"expected {EXPECTED_MAPLESS}"
        )

    traced_report = work / "onetri-imported-traced-boot.report.json"
    traced_run_command = shared_run_command(
        porpoise,
        dtk,
        project_path,
        arguments.target,
        libporpoise,
        meson,
        dvd_root,
        trace_path,
        traced_report,
        arguments.trace_frame_limit,
        arguments.build_type,
        cc,
        cxx,
    )
    acceptance_environment = acceptance_run_environment()
    invoke(
        traced_run_command,
        "shared OneTri imported traced Build/Run",
        environment=acceptance_environment,
    )
    if not (generated_output / "meson.build").is_file():
        raise AssertionError(
            "shared OneTri Build/Run did not publish the generated Meson project"
        )
    if not traced_report.is_file():
        raise AssertionError("shared traced Build/Run did not publish its report")
    if not trace_path.is_file():
        raise AssertionError("shared OneTri Run did not publish its JSONL trace")
    traced_summary = verify_boot_trace(
        trace_path,
        minimum_frames=arguments.trace_frame_limit,
    )

    sustained_report = work / "onetri-imported-sustained.report.json"
    sustained_reused_traced_run = (
        arguments.frame_limit == arguments.trace_frame_limit
    )
    if sustained_reused_traced_run:
        sustained_report = traced_report
    else:
        sustained_run_command = shared_run_command(
            porpoise,
            dtk,
            project_path,
            arguments.target,
            libporpoise,
            meson,
            dvd_root,
            None,
            sustained_report,
            arguments.frame_limit,
            arguments.build_type,
            cc,
            cxx,
        )
        invoke(
            sustained_run_command,
            "shared OneTri imported sustained Build/Run",
            environment=acceptance_environment,
        )
        if not sustained_report.is_file():
            raise AssertionError(
                "shared sustained Build/Run did not publish its report"
            )
    generated_build = project_path.parent / ".porpoise-build" / arguments.target
    generated = {
        "imported": {
            "output": str(generated_output),
            "build_root": str(generated_build),
        }
    }

    print(
        json.dumps(
            {
                "map_backed": {
                    "title": title,
                    "nintendo_demo": automatic,
                    "crt_debug_stub": report_only,
                },
                "policies": policy_summary,
                "expected_policies": expected_actions,
                "mapless": {
                    "title": mapless_categories[0],
                    "nintendo_demo": mapless_categories[1],
                    "crt_debug_stub": mapless_categories[2],
                    "exact": mapless_exact,
                },
                "generated": generated,
                "generated_output": str(generated_output),
                "generated_build": str(generated_build),
                "boot": {
                    "project": str(project_path),
                    "target": arguments.target,
                    "build_type": arguments.build_type,
                    "traced": {
                        "trace": str(trace_path),
                        "report": str(traced_report),
                        "frames_requested": arguments.trace_frame_limit,
                        "reject_approximations": True,
                        **traced_summary,
                    },
                    "sustained": {
                        "report": str(sustained_report),
                        "frames_requested": arguments.frame_limit,
                        "trace_enabled": sustained_reused_traced_run,
                        "reused_traced_run": sustained_reused_traced_run,
                        "reject_approximations": True,
                        "status": "passed",
                    },
                },
                "work_root": str(work_root),
                "work": str(work),
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, RuntimeError, ValueError) as error:
        print(f"OneTri acceptance failed: {error}", file=sys.stderr)
        raise SystemExit(1)
