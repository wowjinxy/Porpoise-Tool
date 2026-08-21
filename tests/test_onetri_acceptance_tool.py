#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
from pathlib import Path
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


class PathSafetyTests(unittest.TestCase):
    def test_copy_rejects_destination_within_source(self):
        with tempfile.TemporaryDirectory(prefix="porpoise-onetri-test-") as temporary:
            source = Path(temporary) / "source"
            source.mkdir()
            destination = source / "recursive-copy"
            with self.assertRaisesRegex(ValueError, "must not contain"):
                acceptance.link_or_copy_directory(source, destination)
            self.assertFalse(destination.exists())

    def test_disjoint_validation_rejects_nested_work_root(self):
        with tempfile.TemporaryDirectory(prefix="porpoise-onetri-test-") as temporary:
            root = Path(temporary)
            libporpoise = root / "libPorpoise"
            work = libporpoise / "acceptance"
            libporpoise.mkdir()
            with self.assertRaisesRegex(ValueError, "must not contain"):
                acceptance.validate_output_roots(work, libporpoise)

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


class NativeFileTests(unittest.TestCase):
    def test_compiler_paths_with_spaces_use_meson_arrays(self):
        with tempfile.TemporaryDirectory(prefix="porpoise-onetri-test-") as temporary:
            root = Path(temporary)
            compiler_dir = root / "Compiler Tools"
            compiler_dir.mkdir()
            cc = compiler_dir / "clang test.exe"
            cxx = compiler_dir / "clang++ test.exe"
            cc.touch()
            cxx.touch()
            native = acceptance.write_meson_native_file(
                root, str(cc), str(cxx)
            )
            self.assertIsNotNone(native)
            text = native.read_text(encoding="utf-8")
            self.assertIn("[binaries]", text)
            self.assertIn(f"c = ['{cc.resolve().as_posix()}']", text)
            self.assertIn(f"cpp = ['{cxx.resolve().as_posix()}']", text)


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
