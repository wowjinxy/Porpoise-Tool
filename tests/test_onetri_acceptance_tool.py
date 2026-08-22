#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = ROOT / "tools" / "run_onetri_acceptance.py"
SPEC = importlib.util.spec_from_file_location("run_onetri_acceptance", TOOL_PATH)
assert SPEC is not None and SPEC.loader is not None
acceptance = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = acceptance
SPEC.loader.exec_module(acceptance)


def function(
    name: str,
    address: int,
    category: str,
    confidence: str,
    action: str,
    *,
    binding: object | None = None,
    signature: str | None = None,
) -> dict[str, object]:
    return {
        "source_name": name,
        "canonical_name": f"sdk.a/object.c/{name}",
        "translation_unit": "asm/object.s",
        "section": ".text",
        "address": address,
        "size": 32,
        "signature": signature or f"{address:064x}",
        "category": category,
        "confidence": confidence,
        "resolved_action": action,
        "binding": binding,
        "conflict": False,
    }


def reviewed_project(root: Path) -> tuple[Path, dict[str, object]]:
    document: dict[str, object] = {
        "schema_version": 2,
        "sdk_catalogs": ["evidence/sdk-catalog.json"],
        "abi_contracts": ["evidence/abi.json"],
        "targets": [
            {
                "id": "onetri",
                "enabled": True,
                "source_kind": "managed_elf",
                "input": "inputs/title.elf",
                "output": "generated/title",
                "entry": None,
                "strict": False,
                "sdk_policy": "imported",
                "symbol_sources": [
                    {
                        "kind": "codewarrior_map",
                        "path": "evidence/title.map",
                        "auxiliary_path": None,
                        "module": "",
                        "permissive": False,
                    }
                ],
                "skip_list": None,
                "overrides": [],
                "annotations": [],
                "title_host": {
                    "entry_address": 0x800055E0,
                    "gpr": [0] * 32,
                    "startup_functions": [],
                    "initial_words": [],
                    "initialize_dvd": True,
                    "input_sha256": "0" * 64,
                },
                "cache": None,
            }
        ],
    }
    path = root / "reviewed.porpoise.json"
    path.write_text(json.dumps(document), encoding="utf-8")
    return path, document


class PathSafetyTests(unittest.TestCase):
    def test_legacy_semantic_path_cannot_override_project(self):
        with tempfile.TemporaryDirectory(prefix="porpoise-onetri-test-") as temporary:
            root = Path(temporary)
            authoritative = root / "authoritative.elf"
            different = root / "different.elf"
            with self.assertRaisesRegex(ValueError, "authoritative path"):
                acceptance.assert_legacy_path_matches(
                    different, authoritative, "--elf"
                )
            acceptance.assert_legacy_path_matches(
                authoritative, authoritative, "--elf"
            )
            acceptance.assert_legacy_path_matches(None, authoritative, "--elf")

    def test_disjoint_validation_rejects_nested_work_root(self):
        with tempfile.TemporaryDirectory(prefix="porpoise-onetri-test-") as temporary:
            root = Path(temporary)
            libporpoise = root / "libPorpoise"
            work = libporpoise / "acceptance"
            libporpoise.mkdir()
            with self.assertRaisesRegex(ValueError, "must not contain"):
                acceptance.validate_output_roots(work, libporpoise)

    def test_external_materials_must_be_disjoint_from_work_and_project_roots(self):
        with tempfile.TemporaryDirectory(prefix="porpoise-onetri-test-") as temporary:
            root = Path(temporary)
            work = root / "work"
            libporpoise = root / "libPorpoise"
            project_root = root / "project"
            work.mkdir()
            libporpoise.mkdir()
            project_root.mkdir()
            safe_materials = (
                (project_root / "recovery.porpoise.json", "reviewed project"),
                (project_root / "input.elf", "ELF"),
            )
            acceptance.validate_external_material_paths(
                work, libporpoise, project_root, safe_materials
            )

            nested_materials = safe_materials + (
                (work / "catalog.json", "catalog"),
            )
            with self.assertRaisesRegex(
                ValueError, "acceptance work root.*OneTri catalog.*must not contain"
            ):
                acceptance.validate_external_material_paths(
                    work, libporpoise, project_root, nested_materials
                )

            nested_project_root = work / "reviewed-project"
            with self.assertRaisesRegex(
                ValueError, "acceptance work root.*reviewed project root"
            ):
                acceptance.validate_external_material_paths(
                    work, libporpoise, nested_project_root, ()
                )

    def test_trace_must_be_disjoint_from_every_material_and_root(self):
        with tempfile.TemporaryDirectory(prefix="porpoise-onetri-test-") as temporary:
            root = Path(temporary)
            occupied = (
                (root / "project/recovery.porpoise.json", "OneTri reviewed project"),
                (root / "inputs/title.elf", "OneTri ELF"),
                (root / "inputs/title.map", "OneTri map"),
                (root / "evidence/catalog.json", "OneTri catalog"),
                (root / "generated", "OneTri generated output"),
                (root / "dvd", "OneTri DVD root"),
                (root / "work", "acceptance work root"),
                (root / "project", "reviewed project root"),
            )
            for path, label in occupied:
                with self.subTest(label=label):
                    with self.assertRaisesRegex(
                        ValueError, f"{label}.*trace output.*must not contain"
                    ):
                        acceptance.validate_trace_destination(
                            path, ((path, label),)
                        )

            generated = root / "generated"
            with self.assertRaisesRegex(ValueError, "generated output.*trace output"):
                acceptance.validate_trace_destination(
                    generated / "traces/boot.jsonl",
                    ((generated, "OneTri generated output"),),
                )
            with self.assertRaisesRegex(ValueError, "generated output.*trace output"):
                acceptance.validate_trace_destination(
                    root,
                    ((generated, "OneTri generated output"),),
                )

            acceptance.validate_trace_destination(
                root / "separate-traces/boot.jsonl", occupied
            )

    def test_each_invocation_gets_a_unique_owned_child(self):
        with tempfile.TemporaryDirectory(prefix="porpoise-onetri-test-") as temporary:
            root = Path(temporary) / "acceptance-root"
            first = acceptance.create_run_directory(root)
            second = acceptance.create_run_directory(root)
            self.assertNotEqual(first, second)
            self.assertTrue(
                (first / ".porpoise-onetri-acceptance-run-v1").is_file()
            )
            self.assertTrue(
                (second / ".porpoise-onetri-acceptance-run-v1").is_file()
            )


class ReviewedProjectTests(unittest.TestCase):
    def test_project_is_schema_v2_imported_and_has_reviewed_host(self):
        with tempfile.TemporaryDirectory(prefix="porpoise-onetri-test-") as temporary:
            root = Path(temporary)
            project_path, _ = reviewed_project(root)
            document, target = acceptance.load_reviewed_project(
                project_path, "onetri"
            )
            self.assertEqual(document["schema_version"], 2)
            self.assertEqual(target["sdk_policy"], "imported")
            self.assertIsInstance(target["title_host"], dict)

            target["sdk_policy"] = "keep"
            project_path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(AssertionError, "explicitly use.*imported"):
                acceptance.load_reviewed_project(project_path, "onetri")

    def test_selected_inputs_resolve_relative_to_reviewed_project(self):
        with tempfile.TemporaryDirectory(prefix="porpoise-onetri-test-") as temporary:
            root = Path(temporary)
            project_path, _ = reviewed_project(root)
            document, target = acceptance.load_reviewed_project(
                project_path, "onetri"
            )
            elf, map_path, catalog, output = acceptance.selected_project_inputs(
                project_path, document, target
            )
            self.assertEqual(elf, (root / "inputs/title.elf").resolve())
            self.assertEqual(map_path, (root / "evidence/title.map").resolve())
            self.assertEqual(catalog, (root / "evidence/sdk-catalog.json").resolve())
            self.assertEqual(output, (root / "generated/title").resolve())

    def test_analysis_variants_rebase_paths_without_changing_source_project(self):
        with tempfile.TemporaryDirectory(prefix="porpoise-onetri-test-") as temporary:
            root = Path(temporary)
            project_path, original = reviewed_project(root)
            document, target = acceptance.load_reviewed_project(
                project_path, "onetri"
            )
            derived = acceptance.absolute_analysis_project(
                project_path,
                document,
                target,
                root / "analysis-output",
                "omit",
                include_map=False,
                include_catalog=True,
            )
            derived_target = derived["targets"][0]
            self.assertEqual(derived_target["sdk_policy"], "omit")
            self.assertEqual(derived_target["symbol_sources"], [])
            self.assertIsNone(derived_target["cache"])
            self.assertTrue(Path(derived_target["input"]).is_absolute())
            self.assertTrue(Path(derived["sdk_catalogs"][0]).is_absolute())
            self.assertEqual(document, original)

    def test_shared_run_uses_porpoise_build_core_and_operational_overrides(self):
        root = Path("C:/external")
        command = acceptance.shared_run_command(
            root / "porpoise.exe",
            root / "dtk.exe",
            root / "recovery.porpoise.json",
            "onetri",
            root / "libPorpoise",
            str(root / "meson.exe"),
            root / "dvd",
            root / "boot.jsonl",
            root / "boot-report.json",
            300,
            "debugoptimized",
            str(root / "gcc.exe"),
            str(root / "g++.exe"),
        )
        self.assertEqual(command[0], str(root / "porpoise.exe"))
        self.assertIn("--run", command)
        self.assertIn("--libporpoise", command)
        self.assertIn("--dvd-root", command)
        self.assertIn("--trace", command)
        self.assertIn("--frame-limit", command)
        self.assertIn("300", command)
        self.assertIn("--force", command)
        self.assertNotIn("setup", command)
        self.assertNotIn("compile", command)

        sustained = acceptance.shared_run_command(
            root / "porpoise.exe",
            root / "dtk.exe",
            root / "recovery.porpoise.json",
            "onetri",
            root / "libPorpoise",
            str(root / "meson.exe"),
            root / "dvd",
            None,
            root / "sustained-report.json",
            300,
            "debugoptimized",
            None,
            None,
        )
        self.assertNotIn("--trace", sustained)
        self.assertIn("--frame-limit", sustained)

        environment = acceptance.acceptance_run_environment()
        self.assertEqual(environment["PORPOISE_REJECT_APPROXIMATIONS"], "1")


class BootTraceTests(unittest.TestCase):
    def write_trace(self, root: Path, events: list[dict[str, object]]) -> Path:
        trace = root / "boot.jsonl"
        trace.write_text(
            "".join(json.dumps(event) + "\n" for event in events),
            encoding="utf-8",
        )
        return trace

    def valid_events(self) -> list[dict[str, object]]:
        events: list[dict[str, object]] = []
        for sequence, name in enumerate(acceptance.EXPECTED_BOOT_MILESTONES, 1):
            events.append(
                {
                    "sequence": sequence,
                    "event": "call",
                    "phase": "enter",
                    "function": name,
                    "pc": "0x80000000",
                }
            )
        events.extend(
            [
                {
                    "sequence": 6,
                    "event": "call",
                    "phase": "enter",
                    "function": acceptance.EXPECTED_LOOP_MILESTONE,
                    "pc": "0x80000000",
                },
                {
                    "sequence": 7,
                    "event": "frame",
                    "frame": 1,
                    "frame_buffer": "0x81234000",
                    "content_hash": "0x1111111111111111",
                    "content_varied": False,
                },
                {
                    "sequence": 8,
                    "event": "frame",
                    "frame": 2,
                    "frame_buffer": "0x81234000",
                    "content_hash": "0x2222222222222222",
                    "content_varied": True,
                },
            ]
        )
        return events

    def test_valid_trace_reaches_ordered_milestones_and_nonempty_frames(self):
        with tempfile.TemporaryDirectory(prefix="porpoise-trace-test-") as temporary:
            summary = acceptance.verify_boot_trace(
                self.write_trace(Path(temporary), self.valid_events()),
                minimum_frames=2,
            )
            self.assertEqual(summary["frames"], 2)
            self.assertEqual(summary["varied_frames"], 1)
            self.assertEqual(
                summary["milestones"],
                list(acceptance.EXPECTED_BOOT_MILESTONES),
            )
            self.assertEqual(
                summary["loop_milestone"], acceptance.EXPECTED_LOOP_MILESTONE
            )

    def test_fault_approximation_and_zero_frame_are_rejected(self):
        with tempfile.TemporaryDirectory(prefix="porpoise-trace-test-") as temporary:
            root = Path(temporary)
            faulty = self.valid_events()
            faulty.append(
                {
                    "event": "fault",
                    "pc": "0x80001234",
                    "message": "boom",
                }
            )
            with self.assertRaisesRegex(AssertionError, "guest fault"):
                acceptance.verify_boot_trace(
                    self.write_trace(root, faulty), minimum_frames=2
                )

            approximate = self.valid_events()
            approximate.insert(
                5,
                {
                    "event": "approximate",
                    "pc": "0x80001234",
                    "address": "0x80001234",
                    "mnemonic": "psq_l",
                },
            )
            with self.assertRaisesRegex(AssertionError, "unreviewed approximation"):
                acceptance.verify_boot_trace(
                    self.write_trace(root, approximate), minimum_frames=2
                )

            zero = self.valid_events()
            zero[-1]["frame_buffer"] = "0x00000000"
            with self.assertRaisesRegex(AssertionError, "zero guest framebuffer"):
                acceptance.verify_boot_trace(
                    self.write_trace(root, zero), minimum_frames=2
                )

            uniform = self.valid_events()
            uniform[-1]["content_varied"] = False
            with self.assertRaisesRegex(AssertionError, "only uniform"):
                acceptance.verify_boot_trace(
                    self.write_trace(root, uniform), minimum_frames=2
                )

            unobserved = self.valid_events()
            del unobserved[-1]["content_hash"]
            with self.assertRaisesRegex(AssertionError, "complete content"):
                acceptance.verify_boot_trace(
                    self.write_trace(root, unobserved), minimum_frames=2
                )

    def test_missing_or_out_of_order_milestone_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="porpoise-trace-test-") as temporary:
            events = self.valid_events()
            events[2]["function"], events[3]["function"] = (
                events[3]["function"],
                events[2]["function"],
            )
            with self.assertRaisesRegex(AssertionError, "boot milestone"):
                acceptance.verify_boot_trace(
                    self.write_trace(Path(temporary), events),
                    minimum_frames=2,
                )

    def test_pad_loop_must_be_reached_after_print_intro(self):
        with tempfile.TemporaryDirectory(prefix="porpoise-trace-test-") as temporary:
            events = self.valid_events()
            events = [
                event
                for event in events
                if event.get("function") != acceptance.EXPECTED_LOOP_MILESTONE
            ]
            with self.assertRaisesRegex(AssertionError, "PADRead.*after.*PrintIntro"):
                acceptance.verify_boot_trace(
                    self.write_trace(Path(temporary), events),
                    minimum_frames=2,
                )

            early_pad = {
                "sequence": 0,
                "event": "call",
                "phase": "enter",
                "function": acceptance.EXPECTED_LOOP_MILESTONE,
                "pc": "0x80000000",
            }
            events.insert(0, early_pad)
            with self.assertRaisesRegex(AssertionError, "PADRead.*after.*PrintIntro"):
                acceptance.verify_boot_trace(
                    self.write_trace(Path(temporary), events),
                    minimum_frames=2,
                )


class PolicyTests(unittest.TestCase):
    def policy_items(self) -> dict[str, list[dict[str, object]]]:
        keep = [
            function("WithContract", 0x1000, "nintendo_dolphin", "exact", "lift"),
            function("WithoutContract", 0x1020, "demo", "exact", "lift"),
            function("ReportOnly", 0x1040, "runtime", "exact", "lift"),
        ]
        imported = [dict(item) for item in keep]
        imported[0]["resolved_action"] = "import"
        imported[0]["binding"] = "WithContract"
        omitted = [dict(item) for item in keep]
        omitted[0]["resolved_action"] = "import"
        omitted[0]["binding"] = "WithContract"
        omitted[1]["resolved_action"] = "omit"
        return {"keep": keep, "imported": imported, "omit": omitted}

    def test_policy_counts_are_derived_from_contract_dispositions(self):
        expected = acceptance.verify_policy_matrix(
            self.policy_items(), {"sdk.a/object.c/WithContract"}
        )
        self.assertEqual(
            expected,
            {
                "keep": {"lift": 3, "import": 0, "omit": 0, "data": 0},
                "imported": {"lift": 2, "import": 1, "omit": 0, "data": 0},
                "omit": {"lift": 1, "import": 1, "omit": 1, "data": 0},
            },
        )

    def test_omit_keeps_structurally_nonreplaceable_sdk_body_lifted(self):
        items = self.policy_items()
        retained = items["omit"][1]
        retained["resolved_action"] = "lift"
        retained["requested_action"] = "omit"
        retained["origin"] = "sdk-policy"
        expected = acceptance.verify_policy_matrix(
            items, {"sdk.a/object.c/WithContract"}
        )
        self.assertEqual(
            expected["omit"],
            {"lift": 2, "import": 1, "omit": 0, "data": 0},
        )

    def test_omit_rejects_unexplained_exact_sdk_lift(self):
        items = self.policy_items()
        items["omit"][1]["resolved_action"] = "lift"
        with self.assertRaisesRegex(AssertionError, "invalid dispositions"):
            acceptance.verify_policy_matrix(
                items, {"sdk.a/object.c/WithContract"}
            )

    def test_imported_and_omit_must_use_the_same_contract_set(self):
        items = self.policy_items()
        items["omit"][0]["resolved_action"] = "omit"
        items["omit"][0]["binding"] = None
        with self.assertRaisesRegex(AssertionError, "different valid host contracts"):
            acceptance.verify_policy_matrix(
                items, {"sdk.a/object.c/WithContract"}
            )

    def test_catalog_contracts_are_independent_expected_evidence(self):
        items = self.policy_items()
        items["imported"][0]["resolved_action"] = "lift"
        items["imported"][0]["binding"] = None
        items["omit"][0]["resolved_action"] = "omit"
        items["omit"][0]["binding"] = None
        with self.assertRaisesRegex(AssertionError, "disagrees with catalog"):
            acceptance.verify_policy_matrix(
                items, {"sdk.a/object.c/WithContract"}
            )

    def test_exact_builtin_contracts_extend_catalog_evidence(self):
        with tempfile.TemporaryDirectory(
            prefix="porpoise-onetri-catalog-test-"
        ) as temporary:
            catalog = Path(temporary) / "catalog.json"
            catalog.write_text(
                '{"entries":[{"canonical_identity":'
                '"sdk.a/object.c/WithContract","contract":"WithContract"}]}',
                encoding="utf-8",
            )
            identities = acceptance.catalog_contract_identities(catalog)
            self.assertIn("sdk.a/object.c/WithContract", identities)
            self.assertEqual(
                acceptance.BUILTIN_EXACT_CONTRACT_IDENTITIES,
                identities - {"sdk.a/object.c/WithContract"},
            )
            source = (ROOT / "src" / "sdk_contract.c").read_text(
                encoding="utf-8"
            )
            registry_identities = set(
                re.findall(
                    r'SDK_EXACT_CONTRACT\(\s*"[^"]+"\s*,\s*"[^"]+"'
                    r'\s*,\s*"([^"]+)"',
                    source,
                )
            )
            self.assertEqual(
                acceptance.BUILTIN_EXACT_CONTRACT_IDENTITIES,
                registry_identities,
            )

    def test_mapless_exact_parity_rejects_signature_swaps(self):
        mapped = [
            function("Exact", 0x1000, "nintendo_dolphin", "exact", "lift")
        ]
        mapless = [dict(mapped[0])]
        acceptance.verify_mapless_exact_parity(mapped, mapless)
        mapless[0]["signature"] = "f" * 64
        with self.assertRaisesRegex(AssertionError, "exact identity set differs"):
            acceptance.verify_mapless_exact_parity(mapped, mapless)


if __name__ == "__main__":
    unittest.main()
