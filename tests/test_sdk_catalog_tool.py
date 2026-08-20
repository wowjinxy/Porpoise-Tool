#!/usr/bin/env python3
from __future__ import annotations

import base64
from contextlib import redirect_stderr, redirect_stdout
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = ROOT / "tools" / "porpoise_sdk_catalog.py"
SPEC = importlib.util.spec_from_file_location("porpoise_sdk_catalog", TOOL_PATH)
assert SPEC is not None and SPEC.loader is not None
catalog_tool = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = catalog_tool
SPEC.loader.exec_module(catalog_tool)


BASE_WORDS = (
    (0x7C0802A6, 0xFFFFFFFF),
    (0x9421FFF0, 0xFFFFFFFF),
    (0x38601234, 0xFFFFFFFF),
    (0x48001235, 0xFC000003),
    (0x3C80ABCD, 0xFFFF0000),
    (0x3884EF01, 0xFFFF0000),
    (0x48000004, 0xFFFFFFFF),
    (0x4E800020, 0xFFFFFFFF),
)


def dtk_yaml(symbol: str, *, variant: int = 0, crlf: bool = False) -> str:
    words = list(BASE_WORDS)
    if variant:
        words[2] = (0x38601234 + variant, 0xFFFFFFFF)
    blob = b"".join(struct.pack(">II", word, mask) for word, mask in words)
    encoded = base64.b64encode(blob).decode("ascii")
    dtk_hash = hashlib.sha1(blob).hexdigest()
    text = f"""# DTK signature fixture
- symbol: 0
  hash: \"{dtk_hash}\"
  signature: '{encoded}'
  symbols:
  - kind: Function
    name: {json.dumps(symbol)}
    size: 32
    flags: 1
    section: .text
  - kind: Function
    name: ExternalCall
    size: 0
    flags: 1
    section: .text
  - kind: Object
    name: ExternalObject
    size: 4
    flags: 1
    section: .data
  relocations:
  - offset: 12
    kind: rel24
    symbol: 1
    addend: 0
  - offset: 16
    kind: ha
    symbol: 2
    addend: 4
  - offset: 20
    kind: l
    symbol: 2
    addend: 4
"""
    return text.replace("\n", "\r\n") if crlf else text


class FakeRunner:
    def __init__(
        self,
        *,
        version: str = "dtk.exe 1.8.3 e4219e7644fb7b96d920d5bc3d1d950f5569dcaf",
        variants: dict[str, int] | None = None,
        failure_symbol: str | None = None,
    ):
        self.version = version
        self.variants = variants or {}
        self.failure_symbol = failure_symbol
        self.commands: list[tuple[str, ...]] = []

    def __call__(self, command):
        command = tuple(str(part) for part in command)
        self.commands.append(command)
        if command[-1] == "--version":
            return subprocess.CompletedProcess(command, 0, self.version + "\n", "")
        symbol = command[command.index("-s") + 1]
        if symbol == self.failure_symbol:
            return subprocess.CompletedProcess(command, 7, "", "bad ELF\n")
        output_path = Path(command[command.index("-o") + 1])
        output_path.write_text(
            dtk_yaml(symbol, variant=self.variants.get(symbol, 0), crlf=True),
            encoding="utf-8",
        )
        return subprocess.CompletedProcess(command, 0, "", "fixture warning\n")


def candidate(
    symbol: str = "Example",
    identity: str = "gx.a/example.c/Example",
    category: str = "nintendo_dolphin",
    contract: str | None = "ExampleContract",
):
    return catalog_tool.Candidate(
        symbol=symbol,
        canonical_identity=identity,
        category=category,
        contract=contract,
    )


class SignatureConversionTests(unittest.TestCase):
    def test_converts_dtk_signature_to_exact_ppsig_v1_metadata(self):
        record = catalog_tool._parse_dtk_yaml(dtk_yaml("Example", crlf=True), "Example")
        result = catalog_tool.convert_dtk_signature(record, "Example")
        self.assertEqual(
            result.sha256,
            "5dd59c019d265775c6edd1d30f9ce4dd1730da508ab824ca0b739d926cb50620",
        )
        self.assertEqual(result.function_size, 32)
        self.assertEqual(result.instruction_count, 8)
        self.assertEqual(result.fixed_instruction_count, 5)
        self.assertEqual(result.meaningful_fixed_words, 2)
        self.assertEqual(result.relocation_count, 2)
        self.assertEqual(result.internal_branch_count, 1)
        self.assertEqual(result.external_branch_count, 1)
        self.assertEqual(result.external_target_count, 2)
        self.assertEqual(result.issue_flags, 0)

    def test_rejects_corrupt_embedded_hash(self):
        text = dtk_yaml("Example").replace("hash: \"", "hash: \"0", 1)
        record = catalog_tool._parse_dtk_yaml(text, "Example")
        with self.assertRaisesRegex(catalog_tool.CatalogError, "invalid SHA-1"):
            catalog_tool.convert_dtk_signature(record, "Example")

    def test_rejects_unvalidated_mask(self):
        text = dtk_yaml("Example")
        record = catalog_tool._parse_dtk_yaml(text, "Example")
        blob = bytearray(base64.b64decode(record.encoded_signature))
        struct.pack_into(">I", blob, 3 * 8 + 4, 0xFFFFFFFF)
        encoded = base64.b64encode(blob).decode("ascii")
        changed = catalog_tool._DtkRecord(
            primary_symbol=record.primary_symbol,
            dtk_hash=hashlib.sha1(blob).hexdigest(),
            encoded_signature=encoded,
            symbols=record.symbols,
            relocations=record.relocations,
        )
        with self.assertRaisesRegex(catalog_tool.CatalogError, "validated mask"):
            catalog_tool.convert_dtk_signature(changed, "Example")

    def test_rejects_unknown_yaml_root_fields(self):
        text = dtk_yaml("Example").replace("  symbols:\n", "  surprise: 1\n  symbols:\n")
        with self.assertRaisesRegex(catalog_tool.CatalogError, "unknown fields"):
            catalog_tool._parse_dtk_yaml(text, "Example")


class InputTests(unittest.TestCase):
    def test_allowlist_is_strict_and_never_infers_categories(self):
        with tempfile.TemporaryDirectory(prefix="porpoise-catalog-test-") as temporary:
            path = Path(temporary) / "allowlist.json"
            path.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "entries": [
                            {
                                "symbol": "LooksLikeGXButIsExplicitlyRuntime",
                                "canonical_identity": "runtime/example",
                                "category": "runtime",
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            loaded = catalog_tool.load_allowlist(path)
            self.assertEqual(len(loaded), 1)
            self.assertEqual(loaded[0].category, "runtime")

            path.write_text(
                '{"schema_version":1,"entries":[],"unknown":true}',
                encoding="utf-8",
            )
            with self.assertRaisesRegex(catalog_tool.CatalogError, "unknown key"):
                catalog_tool.load_allowlist(path)

    def test_allowlist_rejects_one_dtk_selector_for_distinct_identities(self):
        with tempfile.TemporaryDirectory(prefix="porpoise-catalog-test-") as temporary:
            path = Path(temporary) / "allowlist.json"
            path.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "entries": [
                            {
                                "symbol": "LocalName",
                                "canonical_identity": "one.o/LocalName",
                                "category": "runtime",
                            },
                            {
                                "symbol": "LocalName",
                                "canonical_identity": "two.o/LocalName",
                                "category": "runtime",
                            },
                        ],
                    }
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(catalog_tool.CatalogError, "cannot disambiguate"):
                catalog_tool.load_allowlist(path)

    def test_map_classification_uses_exact_archive_ownership_only(self):
        map_text = """Link map of main
  1] TotallyOrdinary (func,global) found in gx.a gx.c
  2] DemoRoutine (func,global) found in demo.a demo.c
  3] DebugRoutine (func,global) found in db.a db.c
  4] StubRoutine (func,global) found in amcstubs.a amc.c
  5] NotAFunction (object,global) found in gx.a gx.c

.text section layout
  Starting        Virtual
  address  Size   address
  -----------------------
  00000000 000020 80001000  4 GXPrefixDoesNotClassify title.o
  00000020 000020 80001020  4 TotallyOrdinary gx.a gx.c
  00000040 000020 80001040  4 DemoRoutine demo.a demo.c
  00000060 000020 80001060  4 DebugRoutine db.a db.c
  00000080 000020 80001080  4 StubRoutine amcstubs.a amc.c
  000000a0 000020 800010a0  4 GXUnknownArchive gx_extra.a fake.c
  000000c0 000020 800010c0  4 NotAFunction gx.a gx.c

.ctors section layout
"""
        with tempfile.TemporaryDirectory(prefix="porpoise-catalog-test-") as temporary:
            path = Path(temporary) / "sample map.map"
            path.write_text(map_text, encoding="utf-8")
            loaded = catalog_tool.load_codewarrior_map(path)
        self.assertEqual(
            [(item.symbol, item.category) for item in loaded],
            [
                ("TotallyOrdinary", "nintendo_dolphin"),
                ("DemoRoutine", "demo"),
                ("DebugRoutine", "nintendo_dolphin"),
                ("StubRoutine", "stub"),
            ],
        )

    def test_custom_map_archive_is_an_exact_explicit_assignment(self):
        map_text = """.text section layout
  00000000 000020 80001000  4 Ordinary custom.a ordinary.c
  00000020 000020 80001020  4 OrdinaryPrefix custom-extra.a ordinary.c
.data section layout
"""
        with tempfile.TemporaryDirectory(prefix="porpoise-catalog-test-") as temporary:
            path = Path(temporary) / "sample.map"
            path.write_text(map_text, encoding="utf-8")
            loaded = catalog_tool.load_codewarrior_map(
                path,
                catalog_tool.parse_library_overrides(["demo=custom.a"]),
            )
        self.assertEqual([item.symbol for item in loaded], ["Ordinary"])

    def test_map_name_collision_keeps_only_first_owner_with_diagnostics(self):
        map_text = """.text section layout
  00000000 000020 80001000  4 Local gx.a first.c
  00000020 000040 80001020  4 Local gx.a second.c
.data section layout
"""
        diagnostics = []
        with tempfile.TemporaryDirectory(prefix="porpoise-catalog-test-") as temporary:
            path = Path(temporary) / "sample.map"
            path.write_text(map_text, encoding="utf-8")
            loaded = catalog_tool.load_codewarrior_map(
                path, skipped_ambiguous=diagnostics
            )
        self.assertEqual([item.canonical_identity for item in loaded], ["gx.a/first.c/Local"])
        self.assertEqual(len(diagnostics), 1)
        self.assertEqual(diagnostics[0]["selected_identity"], "gx.a/first.c/Local")
        self.assertEqual(diagnostics[0]["skipped_identity"], "gx.a/second.c/Local")
        self.assertEqual(diagnostics[0]["reason"], "dtk_name_selector_is_ambiguous")


class CatalogBuildTests(unittest.TestCase):
    def test_invokes_required_dtk_command_and_discards_body(self):
        runner = FakeRunner()
        dtk = catalog_tool.DtkClient(("injected-dtk",), runner=runner)
        catalog = catalog_tool.build_catalog(Path("input game.elf"), [candidate()], dtk)
        self.assertEqual(set(catalog), {"schema_version", "signature_algorithm_version", "entries"})
        self.assertEqual(catalog["schema_version"], 1)
        self.assertEqual(catalog["signature_algorithm_version"], 1)
        self.assertEqual(len(catalog["entries"]), 1)
        entry = catalog["entries"][0]
        self.assertEqual(
            set(entry), {"canonical_identity", "category", "contract", "signature"}
        )
        serialized = json.dumps(catalog)
        record = catalog_tool._parse_dtk_yaml(dtk_yaml("Example"), "Example")
        self.assertNotIn(record.encoded_signature, serialized)
        self.assertNotIn(record.dtk_hash, serialized)
        self.assertEqual(runner.commands[0], ("injected-dtk", "--version"))
        command = runner.commands[1]
        self.assertEqual(command[1:4], ("--no-color", "elf", "sigs"))
        self.assertEqual(command[command.index("-s") + 1], "Example")
        self.assertEqual(command[-1], "input game.elf")

    def test_identical_entries_coalesce_and_order_is_deterministic(self):
        runner = FakeRunner()
        catalog = catalog_tool.build_catalog(
            Path("input.elf"),
            [candidate(), candidate(), candidate("Other", "demo.a/d.c/Other", "demo", None)],
            catalog_tool.DtkClient(("fake",), runner=runner),
        )
        self.assertEqual(
            [entry["canonical_identity"] for entry in catalog["entries"]],
            ["demo.a/d.c/Other", "gx.a/example.c/Example"],
        )

    def test_same_identity_with_different_signature_is_rejected(self):
        runner = FakeRunner(variants={"Second": 1})
        with self.assertRaisesRegex(catalog_tool.CatalogError, "canonical identity.*conflicts"):
            catalog_tool.build_catalog(
                Path("input.elf"),
                [candidate("First", "same/identity"), candidate("Second", "same/identity")],
                catalog_tool.DtkClient(("fake",), runner=runner),
            )

    def test_distinct_identities_with_same_signature_remain_ambiguous_entries(self):
        catalog = catalog_tool.build_catalog(
            Path("input.elf"),
            [candidate("First", "first/identity"), candidate("Second", "second/identity")],
            catalog_tool.DtkClient(("fake",), runner=FakeRunner()),
        )
        self.assertEqual(len(catalog["entries"]), 2)
        self.assertEqual(
            catalog["entries"][0]["signature"], catalog["entries"][1]["signature"]
        )

    def test_version_and_command_failures_are_actionable(self):
        old = catalog_tool.DtkClient(("fake",), runner=FakeRunner(version="dtk 1.7.9"))
        with self.assertRaisesRegex(catalog_tool.CatalogError, "too old"):
            old.validate_version()
        bad = catalog_tool.DtkClient(
            ("fake",), runner=FakeRunner(failure_symbol="Example")
        )
        with self.assertRaisesRegex(catalog_tool.CatalogError, "exit 7.*bad ELF"):
            catalog_tool.build_catalog(Path("input.elf"), [candidate()], bad)


class CliTests(unittest.TestCase):
    def test_help_documents_both_input_modes(self):
        completed = subprocess.run(
            [sys.executable, str(TOOL_PATH), "--help"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("--map", completed.stdout)
        self.assertIn("--allowlist", completed.stdout)
        self.assertIn("--dry-run", completed.stdout)

    def test_injected_cli_build_is_atomic_and_deterministic(self):
        with tempfile.TemporaryDirectory(prefix="porpoise-catalog-test-") as temporary:
            root = Path(temporary)
            elf = root / "input game.elf"
            elf.touch()
            allowlist = root / "explicit allowlist.json"
            allowlist.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "entries": [
                            {
                                "symbol": "Example",
                                "canonical_identity": "gx.a/example.c/Example",
                                "category": "nintendo_dolphin",
                                "contract": "ExampleContract",
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            first = root / "one" / "catalog.json"
            second = root / "two" / "catalog.json"
            arguments = [
                "--elf",
                str(elf),
                "--allowlist",
                str(allowlist),
                "--dtk",
                "injected-dtk",
            ]
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                self.assertEqual(
                    catalog_tool.main([*arguments, "--output", str(first)], runner=FakeRunner()),
                    0,
                )
                self.assertEqual(
                    catalog_tool.main([*arguments, "--output", str(second)], runner=FakeRunner()),
                    0,
                )
            self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_dry_run_needs_no_dtk_and_writes_nothing(self):
        with tempfile.TemporaryDirectory(prefix="porpoise-catalog-test-") as temporary:
            root = Path(temporary)
            elf = root / "input.elf"
            elf.touch()
            allowlist = root / "allowlist.json"
            allowlist.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "entries": [
                            {
                                "symbol": "Example",
                                "canonical_identity": "explicit/Example",
                                "category": "runtime",
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            output = root / "must-not-exist.json"
            stdout = io.StringIO()
            with redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                result = catalog_tool.main(
                    [
                        "--elf",
                        str(elf),
                        "--allowlist",
                        str(allowlist),
                        "--output",
                        str(output),
                        "--dry-run",
                    ]
                )
            self.assertEqual(result, 0)
            document = json.loads(stdout.getvalue())
            self.assertEqual(document["entry_count"], 1)
            self.assertEqual(document["commands"][0][1:4], ["--no-color", "elf", "sigs"])
            self.assertEqual(document["diagnostics"]["skipped_ambiguous_count"], 0)
            self.assertFalse(output.exists())

    def test_map_collision_warning_has_machine_readable_diagnostics(self):
        with tempfile.TemporaryDirectory(prefix="porpoise-catalog-test-") as temporary:
            root = Path(temporary)
            elf = root / "input.elf"
            elf.touch()
            map_path = root / "input.map"
            map_path.write_text(
                """.text section layout
  00000000 000020 80001000  4 Example gx.a first.c
  00000020 000040 80001020  4 Example gx.a second.c
.data section layout
""",
                encoding="utf-8",
            )
            output = root / "catalog.json"
            diagnostics = root / "diagnostics.json"
            stderr = io.StringIO()
            with redirect_stdout(io.StringIO()), redirect_stderr(stderr):
                result = catalog_tool.main(
                    [
                        "--elf",
                        str(elf),
                        "--map",
                        str(map_path),
                        "--dtk",
                        "injected-dtk",
                        "--output",
                        str(output),
                        "--diagnostics",
                        str(diagnostics),
                    ],
                    runner=FakeRunner(),
                )
            self.assertEqual(result, 0, stderr.getvalue())
            self.assertIn("skipping unreachable", stderr.getvalue())
            document = json.loads(diagnostics.read_text(encoding="utf-8"))
            self.assertEqual(document["skipped_ambiguous_count"], 1)
            self.assertEqual(document["skipped_ambiguous"][0]["symbol"], "Example")
            self.assertEqual(len(json.loads(output.read_text(encoding="utf-8"))["entries"]), 1)


if __name__ == "__main__":
    unittest.main()
