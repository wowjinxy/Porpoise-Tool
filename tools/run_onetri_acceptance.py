#!/usr/bin/env python3
"""Run the proprietary-file-free OneTri local acceptance gate.

The ELF, map, generated assembly, catalog, and outputs always remain outside
the repository.  This script only describes the checks and consumes paths
supplied by the local developer.
"""

from __future__ import annotations

import argparse
from collections import Counter
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
    "imported": {"lift": 698, "import": 25, "omit": 0, "data": 0},
    "omit": {"lift": 328, "import": 25, "omit": 370, "data": 0},
}
EXPECTED_OMIT_REMOVED_BODIES = 395
EXPECTED_MAPLESS = (78, 480, 165, 533)


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


def make_project(
    elf: pathlib.Path,
    map_path: pathlib.Path | None,
    catalog: pathlib.Path | None,
    output: pathlib.Path,
    policy: str,
) -> dict[str, Any]:
    symbols: list[dict[str, Any]] = []
    if map_path is not None:
        symbols.append(
            {
                "kind": "codewarrior_map",
                "path": str(map_path),
                "auxiliary_path": None,
                "module": "",
                "permissive": False,
            }
        )
    return {
        "schema_version": 1,
        "sdk_catalogs": [] if catalog is None else [str(catalog)],
        "abi_contracts": [],
        "targets": [
            {
                "id": "onetri",
                "enabled": True,
                "source_kind": "managed_elf",
                "input": str(elf),
                "output": str(output),
                "entry": None,
                "strict": False,
                "sdk_policy": policy,
                "symbol_sources": symbols,
                "skip_list": None,
                "overrides": [],
                "annotations": [],
                "cache": None,
            }
        ],
    }


def analyze(
    porpoise: pathlib.Path,
    dtk: pathlib.Path,
    work: pathlib.Path,
    elf: pathlib.Path,
    map_path: pathlib.Path | None,
    catalog: pathlib.Path | None,
    policy: str,
) -> dict[str, Any]:
    evidence = "mapless" if map_path is None else "map"
    evidence += "-catalog" if catalog is not None else "-no-catalog"
    project_path = work / f"onetri-{policy}-{evidence}.porpoise.json"
    report_path = project_path.with_suffix(".report.json")
    output = work / f"output-{policy}-{evidence}"
    project_path.write_text(
        json.dumps(make_project(elf, map_path, catalog, output, policy), indent=2),
        encoding="utf-8",
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
    return identities


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
        for locator in eligible
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
    eligible_count = len(eligible)
    expected = {
        "keep": {"lift": total, "import": 0, "omit": 0, "data": 0},
        "imported": {
            "lift": total - imported_count,
            "import": imported_count,
            "omit": 0,
            "data": 0,
        },
        "omit": {
            "lift": total - eligible_count,
            "import": imported_count,
            "omit": eligible_count - imported_count,
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


def link_or_copy_directory(source: pathlib.Path, destination: pathlib.Path) -> None:
    source = source.resolve()
    destination = destination.resolve()
    if not source.is_dir():
        raise FileNotFoundError(source)
    require_disjoint_paths(source, "copy source", destination, "copy destination")
    try:
        destination.symlink_to(source, target_is_directory=True)
        return
    except OSError:
        pass
    shutil.copytree(
        source,
        destination,
        ignore=shutil.ignore_patterns(
            ".git", "build", "build-*", "*.zip", "benchmarks", "docs", "tests"
        ),
    )


def meson_quote(value: str) -> str:
    normalized = pathlib.Path(value).as_posix()
    return "'" + normalized.replace("\\", "\\\\").replace("'", "\\'") + "'"


def write_meson_native_file(
    work: pathlib.Path,
    cc: str | None,
    cxx: str | None,
) -> pathlib.Path | None:
    binaries: list[tuple[str, str]] = []
    if cc is not None:
        binaries.append(("c", resolve_command(cc, "C compiler")))
    if cxx is not None:
        binaries.append(("cpp", resolve_command(cxx, "C++ compiler")))
    if not binaries:
        return None
    path = work / "acceptance-native.ini"
    lines = ["[binaries]"]
    lines.extend(
        f"{language} = [{meson_quote(command)}]"
        for language, command in binaries
    )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    return path


def write_acceptance_host(output: pathlib.Path) -> None:
    host = output / "subprojects" / "porpoise-title-host"
    host.mkdir(parents=True)
    (host / "meson.build").write_text(
        """project('porpoise-title-host-acceptance', 'c', default_options: ['c_std=c99', 'werror=true'])

title_contract_dep = dependency('porpoise-title-contract')
host_library = static_library(
  'porpoise_title_host_acceptance',
  'host.c',
  dependencies: title_contract_dep,
)
porpoise_title_host_dep = declare_dependency(
  link_with: host_library,
  dependencies: title_contract_dep,
)
""",
        encoding="utf-8",
        newline="\n",
    )
    (host / "host.c").write_text(
        """#include \"porpoise_title_host.h\"

int PorpoiseHostPrepareRuntimeV1(
    uint32_t entry_address,
    PorpoiseTitleRuntimeConfigV1 *config_out) {
    (void)entry_address;
    (void)config_out;
    return PORPOISE_TITLE_HOST_UNAVAILABLE;
}

int PorpoiseHostPrepareTitleEntryV3(
    uint32_t entry_address,
    PorpoiseTitleEntryStateV3 *state_out) {
    (void)entry_address;
    (void)state_out;
    return PORPOISE_TITLE_HOST_UNAVAILABLE;
}
""",
        encoding="utf-8",
        newline="\n",
    )


def append_flag(environment: dict[str, str], name: str, value: str) -> None:
    current = environment.get(name, "").strip()
    environment[name] = f"{current} {value}".strip()


def prepare_windows_sdl(
    libporpoise: pathlib.Path,
    work: pathlib.Path,
    environment: dict[str, str],
) -> None:
    sdk = libporpoise / "msys2" / "ucrt64"
    headers = sdk / "include" / "SDL2"
    import_library = sdk / "lib" / "libSDL2.dll.a"
    if not headers.is_dir() or not import_library.is_file():
        raise FileNotFoundError(
            "Windows libPorpoise acceptance requires its pinned "
            "msys2/ucrt64 SDL2 headers and import library"
        )
    include_root = work / "acceptance-sdl-include"
    link_root = work / "acceptance-sdl-lib"
    include_root.mkdir()
    link_root.mkdir()
    link_or_copy_directory(headers, include_root / "SDL2")
    shutil.copy2(import_library, link_root / "libSDL2.a")
    include_flag = f'-I"{include_root.as_posix()}"'
    warning_flags = "-Wno-gnu-pointer-arith -Wno-macro-redefined"
    append_flag(environment, "CFLAGS", f"{include_flag} {warning_flags}")
    append_flag(
        environment,
        "CXXFLAGS",
        f"{include_flag} {warning_flags} -Wno-register",
    )
    append_flag(environment, "LDFLAGS", f'-L"{link_root.as_posix()}"')


def generate_and_build(
    porpoise: pathlib.Path,
    dtk: pathlib.Path,
    elf: pathlib.Path,
    map_path: pathlib.Path,
    catalog: pathlib.Path,
    libporpoise: pathlib.Path,
    work: pathlib.Path,
    meson: str,
    policy: str,
    environment: dict[str, str],
    native_file: pathlib.Path | None,
) -> tuple[pathlib.Path, pathlib.Path]:
    if policy not in POLICIES:
        raise ValueError(f"unknown SDK policy '{policy}'")
    output = work / f"generated-{policy}-map"
    build = work / f"generated-{policy}-map-build"
    if output.exists() or build.exists():
        raise ValueError(
            f"generated OneTri {policy} acceptance paths unexpectedly already exist"
        )
    project = work / f"onetri-generated-{policy}-map.porpoise.json"
    project.write_text(
        json.dumps(make_project(elf, map_path, catalog, output, policy), indent=2),
        encoding="utf-8",
    )
    invoke(
        [
            str(porpoise),
            "--project",
            str(project),
            "--dtk",
            str(dtk),
            "--quiet",
        ],
        f"OneTri {policy} generated-target publication",
    )
    if not (output / "meson.build").is_file():
        raise AssertionError(
            f"OneTri {policy} generation did not publish a Meson project"
        )

    subprojects = output / "subprojects"
    subprojects.mkdir()
    link_or_copy_directory(libporpoise, subprojects / "libPorpoise")
    write_acceptance_host(output)

    host_target = "win64" if sys.platform == "win32" else "linux"
    setup_arguments = [
        meson,
        "setup",
        str(build),
        str(output),
        "--buildtype=release",
        f"-DlibPorpoise:build_target={host_target}",
        "-DlibPorpoise:tests=disabled",
    ]
    if native_file is not None:
        setup_arguments.extend(("--native-file", str(native_file)))
    invoke(
        setup_arguments,
        f"configure generated OneTri {policy} target",
        environment=environment,
    )
    invoke(
        [meson, "compile", "-C", str(build)],
        f"build generated OneTri {policy} target",
        environment=environment,
    )
    return output, build


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--porpoise", type=pathlib.Path, required=True)
    parser.add_argument("--dtk", type=pathlib.Path, required=True)
    parser.add_argument("--elf", type=pathlib.Path, required=True)
    parser.add_argument("--map", dest="map_path", type=pathlib.Path, required=True)
    parser.add_argument("--work", type=pathlib.Path, required=True)
    parser.add_argument("--catalog", type=pathlib.Path, required=True)
    parser.add_argument("--catalog-tool", type=pathlib.Path)
    parser.add_argument("--build-catalog", action="store_true")
    parser.add_argument("--libporpoise", type=pathlib.Path, required=True)
    parser.add_argument("--meson", default="meson")
    parser.add_argument("--cc")
    parser.add_argument("--cxx")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    porpoise = arguments.porpoise.resolve()
    dtk = arguments.dtk.resolve()
    elf = arguments.elf.resolve()
    map_path = arguments.map_path.resolve()
    work_root = arguments.work.resolve()
    catalog = arguments.catalog.resolve()
    libporpoise = arguments.libporpoise.resolve()
    catalog_tool: pathlib.Path | None = None
    meson = resolve_command(arguments.meson, "Meson")
    for path in (porpoise, dtk, elf, map_path):
        if not path.is_file():
            raise FileNotFoundError(path)
    if not (libporpoise / "meson.build").is_file():
        raise FileNotFoundError(libporpoise / "meson.build")
    if arguments.build_catalog:
        if arguments.catalog_tool is None:
            raise ValueError("--build-catalog requires --catalog-tool")
        catalog_tool = arguments.catalog_tool.resolve()
        if not catalog_tool.is_file():
            raise FileNotFoundError(catalog_tool)
    elif not catalog.is_file():
        raise FileNotFoundError(catalog)
    validate_output_roots(work_root, libporpoise)
    if path_contains(REPOSITORY_ROOT, catalog):
        raise ValueError("the OneTri catalog must remain outside the repository")
    if path_contains(libporpoise, catalog):
        raise ValueError("the OneTri catalog must remain outside libPorpoise")
    work = create_run_directory(work_root)
    print(f"OneTri acceptance run directory: {work}", file=sys.stderr)

    native_file = write_meson_native_file(work, arguments.cc, arguments.cxx)
    build_environment = dict(os.environ)
    if sys.platform == "win32":
        prepare_windows_sdl(libporpoise, work, build_environment)

    map_report = analyze(porpoise, dtk, work, elf, map_path, None, "keep")
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
        report = analyze(porpoise, dtk, work, elf, map_path, catalog, policy)
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

    mapless = analyze(porpoise, dtk, work, elf, None, catalog, "keep")
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

    generated: dict[str, dict[str, str]] = {}
    for policy in POLICIES:
        generated_output, generated_build = generate_and_build(
            porpoise,
            dtk,
            elf,
            map_path,
            catalog,
            libporpoise,
            work,
            meson,
            policy,
            build_environment,
            native_file,
        )
        generated[policy] = {
            "output": str(generated_output),
            "build": str(generated_build),
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
                "generated_output": generated["keep"]["output"],
                "generated_build": generated["keep"]["build"],
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
