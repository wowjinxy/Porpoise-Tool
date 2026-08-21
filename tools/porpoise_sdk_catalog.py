#!/usr/bin/env python3
"""Build strict Porpoise SDK signature catalogs with devkitPro dtk.

The helper deliberately treats classification as trusted input.  CodeWarrior
maps are classified only by exact archive ownership, while mapless builds use
an explicit JSON allowlist.  Function names are never used as category
prefixes.  DTK's base64 signature is consumed in memory to produce Porpoise's
PPSIG-v1 identity and is not written to the resulting catalog.
"""

from __future__ import annotations

import argparse
import base64
import binascii
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from typing import Callable, Iterable, Mapping, Sequence


CATALOG_SCHEMA_VERSION = 1
SIGNATURE_ALGORITHM_VERSION = 1
MINIMUM_DTK_VERSION = (1, 8, 0)
UINT32_MAX = 0xFFFFFFFF

CATEGORIES = frozenset(
    {
        "nintendo_dolphin",
        "demo",
        "crt_msl",
        "runtime",
        "metrotrk",
        "debugger",
        "stub",
    }
)

# These are exact archive basenames, compared case-insensitively.  Extending
# this table must be an explicit ownership decision; symbol spellings are not
# consulted anywhere in classification.
_BUILTIN_ARCHIVE_CATEGORIES = {
    "ai.a": "nintendo_dolphin",
    "ar.a": "nintendo_dolphin",
    "base.a": "nintendo_dolphin",
    "card.a": "nintendo_dolphin",
    "dsp.a": "nintendo_dolphin",
    "dvd.a": "nintendo_dolphin",
    "exi.a": "nintendo_dolphin",
    "gx.a": "nintendo_dolphin",
    "mtx.a": "nintendo_dolphin",
    "os.a": "nintendo_dolphin",
    "pad.a": "nintendo_dolphin",
    "si.a": "nintendo_dolphin",
    "vi.a": "nintendo_dolphin",
    "demo.a": "demo",
    "msl_c.ppceabi.bare.h.a": "crt_msl",
    "msl_c.ppceabi.h.a": "crt_msl",
    "msl_c.ppceabi.bare.sz.h.a": "crt_msl",
    "runtime.ppceabi.h.a": "runtime",
    "runtime.ppceabi.sz.h.a": "runtime",
    "trk_minnow_dolphin.a": "metrotrk",
    "metrotrk.a": "metrotrk",
    # Dolphin's DB library is part of the Nintendo SDK policy set.  The
    # separate debugger category is available to explicit/local catalogs.
    "db.a": "nintendo_dolphin",
    "amcstubs.a": "stub",
    "odemustubs.a": "stub",
}

# Audited direct-call adapters implemented by src/sdk_contract.c.  Map import
# may attach these names only after exact archive ownership has classified the
# function as Nintendo/Dolphin SDK code.  Keep this list synchronized with the
# C registry; tests compare the two sources exactly.
_BUILTIN_SDK_CONTRACTS = frozenset(
    {
        "AIInit",
        "ARAlloc",
        "ARFree",
        "ARGetSize",
        "ARInit",
        "ARQPostRequest",
        "ARReset",
        "CARDProbeEx",
        "DSPAddTask",
        "DVDCancel",
        "DVDClose",
        "DVDConvertPathToEntrynum",
        "DVDFastOpen",
        "DVDGetCommandBlockStatus",
        "DVDOpen",
        "DVDReadPrio",
        "GXCallDisplayList",
        "GXCopyDisp",
        "GXCopyTex",
        "GXGetProjectionv",
        "GXGetViewportv",
        "GXInit",
        "GXLoadLightObjImm",
        "GXLoadNrmMtxImm",
        "GXLoadPosMtxImm",
        "GXLoadTexMtxImm",
        "GXLoadTexObj",
        "GXLoadTlut",
        "GXSetArray",
        "GXSetChanAmbColor",
        "GXSetChanMatColor",
        "GXSetCopyClear",
        "GXSetCopyFilter",
        "GXSetDispCopyDst",
        "GXSetDrawDoneCallback",
        "GXSetFog",
        "GXSetFogRangeAdj",
        "GXSetIndTexMtx",
        "GXSetProjection",
        "GXSetTevColor",
        "GXSetTevColorS10",
        "GXSetTevIndirect",
        "GXSetTevKColor",
        "GXSetTexCopyDst",
        "OSAllocFromArenaHi",
        "OSAllocFromArenaLo",
        "OSExitThread",
        "OSGetArenaHi",
        "OSGetArenaLo",
        "OSGetCurrentThread",
        "OSInitMessageQueue",
        "OSReceiveMessage",
        "OSReport",
        "OSResumeThread",
        "OSSendMessage",
        "OSSetArenaHi",
        "OSSetArenaLo",
        "OSSleepThread",
        "OSSuspendThread",
        "OSWakeupThread",
        "VIConfigure",
        "VISetNextFrameBuffer",
    }
)

_DTK_VERSION_PATTERN = re.compile(
    r"\bdtk(?:\.exe)?\s+(\d+)\.(\d+)\.(\d+)(?:\s+[0-9a-fA-F]+)?\b",
    re.IGNORECASE,
)
_CW_LAYOUT_LINE = re.compile(
    r"^\s*([0-9a-fA-F]{8})\s+([0-9a-fA-F]{6,8})\s+"
    r"([0-9a-fA-F]{8})\s+(\d+)\s+(\S+)\s+(\S+)\s+(\S+)\s*$"
)
_CW_CALL_TREE_LINE = re.compile(
    r"^\s*\d+\]\s+(\S+)\s+"
    r"\((func|object|notype|section),(local|global|weak)\)\s+"
    r"found in\s+(\S+)(?:\s+(\S+))?\s*$"
)


class CatalogError(Exception):
    """An expected, user-actionable catalog construction failure."""


@dataclass(frozen=True)
class Candidate:
    symbol: str
    canonical_identity: str
    category: str
    contract: str | None = None
    expected_size: int | None = None
    address: int | None = None
    source: str | None = None
    line: int = 0


@dataclass(frozen=True)
class SignatureResult:
    sha256: str
    function_size: int
    instruction_count: int
    fixed_instruction_count: int
    meaningful_fixed_words: int
    relocation_count: int
    internal_branch_count: int
    external_branch_count: int
    external_target_count: int
    issue_flags: int = 0

    def to_json(self) -> dict[str, object]:
        return {
            "sha256": self.sha256,
            "function_size": self.function_size,
            "instruction_count": self.instruction_count,
            "fixed_instruction_count": self.fixed_instruction_count,
            "meaningful_fixed_words": self.meaningful_fixed_words,
            "relocation_count": self.relocation_count,
            "internal_branch_count": self.internal_branch_count,
            "external_branch_count": self.external_branch_count,
            "external_target_count": self.external_target_count,
            "issue_flags": self.issue_flags,
        }


@dataclass(frozen=True)
class _DtkRecord:
    primary_symbol: int
    dtk_hash: str
    encoded_signature: str
    symbols: tuple[Mapping[str, str], ...]
    relocations: tuple[Mapping[str, str], ...]


Runner = Callable[[Sequence[str]], subprocess.CompletedProcess[str]]


def _default_runner(command: Sequence[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        list(command),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=120,
    )


def _completed_output(completed: subprocess.CompletedProcess[str]) -> str:
    return "\n".join(
        part.strip()
        for part in (completed.stdout or "", completed.stderr or "")
        if part and part.strip()
    )


class DtkClient:
    """Small injectable DTK process boundary used by the CLI and tests."""

    def __init__(self, command: Sequence[str], runner: Runner | None = None):
        if not command or any(not str(part) for part in command):
            raise CatalogError("the DTK command is empty")
        self.command = tuple(str(part) for part in command)
        self.runner = runner or _default_runner

    def _run(self, arguments: Sequence[str], operation: str) -> subprocess.CompletedProcess[str]:
        command = [*self.command, *arguments]
        try:
            completed = self.runner(command)
        except (OSError, subprocess.SubprocessError) as error:
            raise CatalogError(f"failed to {operation}: {error}") from error
        if completed.returncode != 0:
            details = _completed_output(completed)
            suffix = f": {details}" if details else ""
            raise CatalogError(
                f"DTK failed to {operation} (exit {completed.returncode}){suffix}"
            )
        return completed

    def validate_version(self) -> tuple[int, int, int]:
        completed = self._run(("--version",), "query its version")
        output = _completed_output(completed)
        match = _DTK_VERSION_PATTERN.search(output)
        if match is None:
            raise CatalogError(
                "DTK returned an unrecognized version string; expected "
                "'dtk MAJOR.MINOR.PATCH'"
            )
        version = tuple(int(match.group(index)) for index in range(1, 4))
        if version < MINIMUM_DTK_VERSION:
            required = ".".join(str(part) for part in MINIMUM_DTK_VERSION)
            actual = ".".join(str(part) for part in version)
            raise CatalogError(f"DTK {actual} is too old; {required} or newer is required")
        return version  # type: ignore[return-value]

    def read_signature(
        self,
        elf_path: Path,
        symbol: str,
        output_path: Path,
    ) -> _DtkRecord:
        completed = self._run(
            (
                "--no-color",
                "elf",
                "sigs",
                "-s",
                symbol,
                "-o",
                str(output_path),
                str(elf_path),
            ),
            f"build a signature for '{symbol}'",
        )
        del completed
        if not output_path.is_file():
            raise CatalogError(
                f"DTK reported success for '{symbol}' but did not create its signature file"
            )
        try:
            text = output_path.read_text(encoding="utf-8-sig")
        except (OSError, UnicodeError) as error:
            raise CatalogError(f"cannot read DTK signature for '{symbol}': {error}") from error
        finally:
            try:
                output_path.unlink()
            except OSError:
                pass
        return _parse_dtk_yaml(text, symbol)


def _json_without_duplicate_keys(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise CatalogError(f"duplicate JSON key '{key}'")
        result[key] = value
    return result


def _read_json(path: Path) -> object:
    try:
        text = path.read_text(encoding="utf-8-sig")
    except (OSError, UnicodeError) as error:
        raise CatalogError(f"cannot read '{path}': {error}") from error
    try:
        return json.loads(text, object_pairs_hook=_json_without_duplicate_keys)
    except CatalogError:
        raise
    except json.JSONDecodeError as error:
        raise CatalogError(
            f"invalid JSON in '{path}' at line {error.lineno}, column {error.colno}: "
            f"{error.msg}"
        ) from error


def _require_exact_keys(
    value: Mapping[str, object],
    required: set[str],
    optional: set[str],
    context: str,
) -> None:
    keys = set(value)
    missing = sorted(required - keys)
    unknown = sorted(keys - required - optional)
    if missing:
        raise CatalogError(f"{context} is missing key(s): {', '.join(missing)}")
    if unknown:
        raise CatalogError(f"{context} has unknown key(s): {', '.join(unknown)}")


def _nonempty_string(value: object, context: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise CatalogError(f"{context} must be a nonempty string")
    if "\x00" in value:
        raise CatalogError(f"{context} must not contain a NUL byte")
    return value


def load_allowlist(path: Path) -> list[Candidate]:
    document = _read_json(path)
    if not isinstance(document, dict):
        raise CatalogError(f"allowlist '{path}' must contain a JSON object")
    _require_exact_keys(document, {"schema_version", "entries"}, set(), "allowlist root")
    if type(document["schema_version"]) is not int or document["schema_version"] != 1:
        raise CatalogError("allowlist schema_version must be the integer 1")
    raw_entries = document["entries"]
    if not isinstance(raw_entries, list):
        raise CatalogError("allowlist entries must be an array")

    candidates: list[Candidate] = []
    for index, raw_entry in enumerate(raw_entries):
        context = f"allowlist entry {index}"
        if not isinstance(raw_entry, dict):
            raise CatalogError(f"{context} must be an object")
        _require_exact_keys(
            raw_entry,
            {"symbol", "canonical_identity", "category"},
            {"contract", "address"},
            context,
        )
        symbol = _nonempty_string(raw_entry["symbol"], f"{context}.symbol")
        identity = _nonempty_string(
            raw_entry["canonical_identity"], f"{context}.canonical_identity"
        )
        category = _nonempty_string(raw_entry["category"], f"{context}.category")
        if category not in CATEGORIES:
            raise CatalogError(
                f"{context}.category must be one of: {', '.join(sorted(CATEGORIES))}"
            )
        contract: str | None = None
        if "contract" in raw_entry:
            contract = _nonempty_string(raw_entry["contract"], f"{context}.contract")
        address: int | None = None
        if "address" in raw_entry:
            raw_address = raw_entry["address"]
            if (
                type(raw_address) is not int
                or raw_address < 0
                or raw_address > UINT32_MAX
                or raw_address % 4 != 0
            ):
                raise CatalogError(
                    f"{context}.address must be an aligned uint32 integer"
                )
            address = raw_address
        candidates.append(
            Candidate(
                symbol=symbol,
                canonical_identity=identity,
                category=category,
                contract=contract,
                address=address,
                source=str(path),
                line=index + 1,
            )
        )
    selectors: dict[str, Candidate] = {}
    for entry in candidates:
        previous = selectors.get(entry.symbol)
        if previous is not None and (
            previous.canonical_identity != entry.canonical_identity
            or previous.category != entry.category
            or previous.contract != entry.contract
            or previous.expected_size != entry.expected_size
            or previous.address != entry.address
        ):
            raise CatalogError(
                f"allowlist symbol selector '{entry.symbol}' names both "
                f"'{previous.canonical_identity}' and '{entry.canonical_identity}'; "
                "DTK cannot disambiguate them"
            )
        selectors[entry.symbol] = entry
    return candidates


def _archive_basename(value: str) -> str:
    return value.replace("\\", "/").rsplit("/", 1)[-1]


def _builtin_contract_for_identity(
    canonical_identity: str,
    category: str,
) -> str | None:
    if category != "nintendo_dolphin":
        return None
    leaf = canonical_identity.rsplit("/", 1)[-1]
    return leaf if leaf in _BUILTIN_SDK_CONTRACTS else None


def parse_library_overrides(values: Iterable[str]) -> dict[str, str]:
    result: dict[str, str] = {}
    for value in values:
        if "=" not in value:
            raise CatalogError(
                f"invalid --library '{value}'; expected CATEGORY=ARCHIVE"
            )
        category, archive = value.split("=", 1)
        category = category.strip()
        archive = _archive_basename(archive.strip())
        if category not in CATEGORIES:
            raise CatalogError(
                f"invalid --library category '{category}'; expected one of: "
                f"{', '.join(sorted(CATEGORIES))}"
            )
        if not archive:
            raise CatalogError("--library archive basename must not be empty")
        key = archive.casefold()
        previous = result.get(key)
        if previous is not None and previous != category:
            raise CatalogError(
                f"archive '{archive}' was assigned both '{previous}' and '{category}'"
            )
        result[key] = category
    return result


def load_codewarrior_map(
    path: Path,
    library_overrides: Mapping[str, str] | None = None,
    skipped_ambiguous: list[dict[str, object]] | None = None,
) -> list[Candidate]:
    try:
        lines = path.read_text(encoding="utf-8-sig", errors="strict").splitlines()
    except (OSError, UnicodeError) as error:
        raise CatalogError(f"cannot read map '{path}': {error}") from error

    categories = dict(_BUILTIN_ARCHIVE_CATEGORIES)
    for archive, category in (library_overrides or {}).items():
        existing = categories.get(archive.casefold())
        if existing is not None and existing != category:
            raise CatalogError(
                f"archive '{archive}' is built in as '{existing}' and cannot also be "
                f"classified as '{category}'"
            )
        categories[archive.casefold()] = category

    known_nonfunctions: set[tuple[str, str, str]] = set()
    for line in lines:
        match = _CW_CALL_TREE_LINE.match(line)
        if match is None or match.group(2) == "func":
            continue
        if match.group(5) is None:
            library = ""
            object_name = match.group(4)
        else:
            library = match.group(4)
            object_name = match.group(5)
        known_nonfunctions.add((match.group(1), library, object_name))

    in_code_layout = False
    candidates: list[Candidate] = []
    for line_number, line in enumerate(lines, 1):
        stripped = line.strip()
        section_match = re.fullmatch(r"(\S+) section layout", stripped)
        if section_match is not None:
            in_code_layout = section_match.group(1) in {".init", ".text", ".fini"}
            continue
        if not in_code_layout:
            continue
        match = _CW_LAYOUT_LINE.match(line)
        if match is None:
            continue
        size = int(match.group(2), 16)
        address = int(match.group(3), 16)
        symbol = match.group(5)
        library = match.group(6)
        object_name = match.group(7)
        archive = _archive_basename(library)
        category = categories.get(archive.casefold())
        if category is None or symbol.startswith("."):
            continue
        if (symbol, library, object_name) in known_nonfunctions:
            continue
        identity = "/".join(
            (
                archive,
                object_name.replace("\\", "/"),
                symbol,
            )
        )
        candidates.append(
            Candidate(
                symbol=symbol,
                canonical_identity=identity,
                category=category,
                contract=_builtin_contract_for_identity(identity, category),
                expected_size=size,
                address=address,
                source=str(path),
                line=line_number,
            )
        )

    # DTK's `-s` selector cannot distinguish same-named symbols.  Selecting an
    # arbitrary first owner is unsafe even when all candidates have the same
    # size, so exclude every member of every duplicated map-symbol group.
    by_symbol: dict[str, list[Candidate]] = {}
    for candidate in candidates:
        by_symbol.setdefault(candidate.symbol, []).append(candidate)

    unique: list[Candidate] = []
    for symbol_candidates in by_symbol.values():
        if len(symbol_candidates) == 1:
            unique.append(symbol_candidates[0])
            continue
        for candidate in symbol_candidates:
            if skipped_ambiguous is not None:
                skipped_ambiguous.append(
                    {
                        "symbol": candidate.symbol,
                        "excluded_identity": candidate.canonical_identity,
                        "excluded_size": candidate.expected_size,
                        "excluded_address": candidate.address,
                        "group_size": len(symbol_candidates),
                        "source": candidate.source,
                        "line": candidate.line,
                        "reason": "dtk_name_selector_is_ambiguous",
                    }
                )
    return unique


def _strip_yaml_comment(value: str) -> str:
    quote: str | None = None
    escaped = False
    for index, character in enumerate(value):
        if escaped:
            escaped = False
            continue
        if quote == '"' and character == "\\":
            escaped = True
            continue
        if quote is not None:
            if character == quote:
                quote = None
            continue
        if character in ("'", '"'):
            quote = character
        elif character == "#" and (index == 0 or value[index - 1].isspace()):
            return value[:index].rstrip()
    return value.rstrip()


def _yaml_scalar(value: str, context: str) -> str:
    value = _strip_yaml_comment(value.strip())
    if not value:
        raise CatalogError(f"DTK YAML {context} has an empty scalar")
    if value.startswith('"'):
        try:
            decoded = json.loads(value)
        except json.JSONDecodeError as error:
            raise CatalogError(f"DTK YAML {context} has an invalid quoted scalar") from error
        if not isinstance(decoded, str):
            raise CatalogError(f"DTK YAML {context} must be a scalar string")
        return decoded
    if value.startswith("'"):
        if len(value) < 2 or not value.endswith("'"):
            raise CatalogError(f"DTK YAML {context} has an invalid quoted scalar")
        return value[1:-1].replace("''", "'")
    return value


def _yaml_key_value(text: str, context: str) -> tuple[str, str]:
    if ":" not in text:
        raise CatalogError(f"DTK YAML {context} is not a key/value pair")
    key, value = text.split(":", 1)
    key = key.strip()
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", key):
        raise CatalogError(f"DTK YAML {context} has invalid key '{key}'")
    return key, value.strip()


def _parse_dtk_yaml(text: str, requested_symbol: str) -> _DtkRecord:
    records: list[dict[str, object]] = []
    current: dict[str, object] | None = None
    mode: str | None = None
    nested_item: dict[str, str] | None = None
    nested_indent: int | None = None

    for line_number, raw_line in enumerate(text.splitlines(), 1):
        if not raw_line.strip() or raw_line.lstrip().startswith("#"):
            continue
        if "\t" in raw_line[: len(raw_line) - len(raw_line.lstrip())]:
            raise CatalogError(f"DTK YAML line {line_number} uses tab indentation")
        indent = len(raw_line) - len(raw_line.lstrip(" "))
        content = raw_line[indent:]
        if indent == 0 and content in ("---", "..."):
            continue
        if indent == 0 and content.startswith("- "):
            key, raw_value = _yaml_key_value(content[2:], f"line {line_number}")
            if key != "symbol":
                raise CatalogError(
                    f"DTK YAML line {line_number} must begin a record with 'symbol'"
                )
            current = {key: _yaml_scalar(raw_value, f"line {line_number}")}
            records.append(current)
            mode = None
            nested_item = None
            nested_indent = None
            continue
        if current is None:
            raise CatalogError(f"DTK YAML line {line_number} appears before a record")
        if indent in (2, 4) and content.startswith("- "):
            if mode not in ("symbols", "relocations"):
                raise CatalogError(f"DTK YAML line {line_number} has an unexpected list item")
            key, raw_value = _yaml_key_value(content[2:], f"line {line_number}")
            nested_item = {key: _yaml_scalar(raw_value, f"line {line_number}")}
            values = current[mode]
            assert isinstance(values, list)
            values.append(nested_item)
            nested_indent = indent
            continue
        if indent == 2:
            key, raw_value = _yaml_key_value(content, f"line {line_number}")
            if key in current:
                raise CatalogError(f"DTK YAML line {line_number} repeats key '{key}'")
            if key in ("symbols", "relocations"):
                if raw_value and _strip_yaml_comment(raw_value) != "[]":
                    raise CatalogError(
                        f"DTK YAML line {line_number} has an unsupported inline '{key}' value"
                    )
                current[key] = []
                mode = key if not raw_value else None
                nested_item = None
                nested_indent = None
            else:
                current[key] = _yaml_scalar(raw_value, f"line {line_number}")
                mode = None
                nested_item = None
                nested_indent = None
            continue
        if (
            nested_indent is not None
            and indent == nested_indent + 2
            and nested_item is not None
            and mode is not None
        ):
            key, raw_value = _yaml_key_value(content, f"line {line_number}")
            if key in nested_item:
                raise CatalogError(f"DTK YAML line {line_number} repeats key '{key}'")
            nested_item[key] = _yaml_scalar(raw_value, f"line {line_number}")
            continue
        raise CatalogError(f"DTK YAML line {line_number} has unsupported indentation")

    if len(records) != 1:
        raise CatalogError(
            f"DTK returned {len(records)} signature records for '{requested_symbol}'; expected one"
        )
    record = records[0]
    allowed = {"symbol", "hash", "signature", "symbols", "relocations"}
    missing = sorted(allowed - set(record))
    unknown = sorted(set(record) - allowed)
    if missing:
        raise CatalogError(f"DTK signature for '{requested_symbol}' is missing: {', '.join(missing)}")
    if unknown:
        raise CatalogError(
            f"DTK signature for '{requested_symbol}' has unknown fields: {', '.join(unknown)}"
        )
    try:
        primary = _parse_yaml_integer(str(record["symbol"]), "symbol")
    except CatalogError as error:
        raise CatalogError(f"invalid DTK signature for '{requested_symbol}': {error}") from error
    symbols = record["symbols"]
    relocations = record["relocations"]
    assert isinstance(symbols, list)
    assert isinstance(relocations, list)
    return _DtkRecord(
        primary_symbol=primary,
        dtk_hash=str(record["hash"]),
        encoded_signature=str(record["signature"]),
        symbols=tuple(symbols),
        relocations=tuple(relocations),
    )


def _parse_yaml_integer(value: str, context: str, *, signed: bool = False) -> int:
    value = value.strip()
    pattern = r"-?(?:0[xX][0-9a-fA-F]+|[0-9]+)" if signed else r"(?:0[xX][0-9a-fA-F]+|[0-9]+)"
    if re.fullmatch(pattern, value) is None:
        raise CatalogError(f"{context} must be an integer")
    negative = value.startswith("-")
    magnitude_text = value[1:] if negative else value
    base = 16 if magnitude_text.lower().startswith("0x") else 10
    magnitude = int(magnitude_text, base)
    return -magnitude if negative else magnitude


def _required_mapping_string(
    value: Mapping[str, str], key: str, context: str
) -> str:
    if key not in value or not value[key]:
        raise CatalogError(f"DTK {context} is missing '{key}'")
    return value[key]


def _decode_dtk_signature(record: _DtkRecord, requested_symbol: str) -> bytes:
    if re.fullmatch(r"[0-9a-fA-F]{40}", record.dtk_hash) is None:
        raise CatalogError(f"DTK signature for '{requested_symbol}' has an invalid SHA-1 hash")
    try:
        blob = base64.b64decode(record.encoded_signature, validate=True)
    except (ValueError, binascii.Error) as error:
        raise CatalogError(
            f"DTK signature for '{requested_symbol}' has invalid base64 data"
        ) from error
    actual_hash = hashlib.sha1(blob).hexdigest()
    if actual_hash != record.dtk_hash.lower():
        raise CatalogError(
            f"DTK signature for '{requested_symbol}' failed its embedded hash check"
        )
    if not blob or len(blob) % 8 != 0:
        raise CatalogError(
            f"DTK signature for '{requested_symbol}' has an invalid word/mask layout"
        )
    return blob


def _normalize_relocation_kind(kind: str) -> str:
    aliases = {
        "addr16_lo": "l",
        "addr16_hi": "h",
        "addr16_ha": "ha",
        # DTK serializes PpcAddr16Hi as "hi" (while Porpoise's assembly
        # suffix and canonical signature vocabulary use "h").
        "hi": "h",
        "emb_sda21": "sda21",
        "rel14_brta": "rel14",
        "rel14_brntaken": "rel14",
    }
    normalized = kind.strip().casefold()
    return aliases.get(normalized, normalized)


def _branch_form(word: int) -> tuple[str, int, int, bool] | None:
    opcode = word >> 26
    if opcode == 18:
        field = word & 0x03FFFFFC
        displacement = field - (1 << 26) if field & (1 << 25) else field
        return "rel24", 0x03FFFFFC, displacement, bool(word & 2)
    if opcode == 16:
        field = word & 0x0000FFFC
        displacement = field - (1 << 16) if field & (1 << 15) else field
        return "rel14", 0x0000FFFC, displacement, bool(word & 2)
    return None


def _boilerplate_word(word: int) -> bool:
    if word in {
        0x60000000,
        0x4E800020,
        0x4E800420,
        0x4E800421,
        0x7C0802A6,
        0x7C0803A6,
    }:
        return True
    opcode = word >> 26
    rt = (word >> 21) & 31
    ra = (word >> 16) & 31
    return (opcode, rt, ra) in {
        (37, 1, 1),
        (14, 1, 1),
        (36, 0, 1),
        (32, 0, 1),
    }


def _u32(value: int) -> bytes:
    return struct.pack(">I", value & UINT32_MAX)


def _u64(value: int) -> bytes:
    return struct.pack(">Q", value & 0xFFFFFFFFFFFFFFFF)


def convert_dtk_signature(
    record: _DtkRecord,
    requested_symbol: str,
    expected_size: int | None = None,
    function_address: int | None = None,
) -> SignatureResult:
    blob = _decode_dtk_signature(record, requested_symbol)
    symbols = record.symbols
    if record.primary_symbol < 0 or record.primary_symbol >= len(symbols):
        raise CatalogError(f"DTK signature for '{requested_symbol}' has an invalid primary symbol")
    primary = symbols[record.primary_symbol]
    if _required_mapping_string(primary, "kind", "primary symbol").casefold() != "function":
        raise CatalogError(f"DTK symbol '{requested_symbol}' is not a function")
    primary_name = _required_mapping_string(primary, "name", "primary symbol")
    if primary_name != requested_symbol:
        raise CatalogError(
            f"DTK selected '{primary_name}' while '{requested_symbol}' was requested"
        )
    function_size = _parse_yaml_integer(
        _required_mapping_string(primary, "size", "primary symbol"),
        "primary symbol size",
    )
    instruction_count = len(blob) // 8
    if function_size == 0 or function_size != instruction_count * 4:
        raise CatalogError(
            f"DTK function '{requested_symbol}' size {function_size} does not match "
            f"its {instruction_count} signature instructions"
        )
    if function_size > UINT32_MAX:
        raise CatalogError(f"DTK function '{requested_symbol}' is larger than uint32")
    if expected_size is not None and function_size != expected_size:
        raise CatalogError(
            f"DTK function '{requested_symbol}' has size {function_size}, but its map "
            f"record has size {expected_size}"
        )
    if function_address is not None and (
        function_address < 0
        or function_address > UINT32_MAX
        or function_address % 4 != 0
        or function_size - 1 > UINT32_MAX - function_address
    ):
        raise CatalogError(
            f"function '{requested_symbol}' has an invalid uint32 address range"
        )

    relocations: dict[int, tuple[str, int, int]] = {}
    for index, relocation in enumerate(record.relocations):
        context = f"relocation {index} for '{requested_symbol}'"
        offset = _parse_yaml_integer(
            _required_mapping_string(relocation, "offset", context), f"{context} offset"
        )
        kind = _normalize_relocation_kind(
            _required_mapping_string(relocation, "kind", context)
        )
        symbol_index = _parse_yaml_integer(
            _required_mapping_string(relocation, "symbol", context), f"{context} symbol"
        )
        addend = _parse_yaml_integer(
            _required_mapping_string(relocation, "addend", context),
            f"{context} addend",
            signed=True,
        )
        if offset % 4 != 0 or offset < 0 or offset >= function_size:
            raise CatalogError(f"DTK {context} has an out-of-range or unaligned offset")
        if offset in relocations:
            raise CatalogError(f"DTK function '{requested_symbol}' has multiple relocations at {offset}")
        if symbol_index < 0 or symbol_index >= len(symbols):
            raise CatalogError(f"DTK {context} has an invalid symbol index")
        if kind not in {"l", "h", "ha", "sda21", "rel24", "rel14"}:
            raise CatalogError(f"DTK {context} uses unsupported kind '{kind}'")
        relocations[offset] = (kind, symbol_index, addend)

    fixed_count = 0
    meaningful_fixed_count = 0
    relocation_count = 0
    internal_branch_count = 0
    external_branch_count = 0
    target_ordinals: dict[tuple[object, ...], int] = {}
    instruction_records: list[tuple[int, int, int, int, int, int, int, int]] = []

    def target_ordinal(key: tuple[object, ...]) -> int:
        ordinal = target_ordinals.get(key)
        if ordinal is None:
            ordinal = len(target_ordinals) + 1
            target_ordinals[key] = ordinal
        return ordinal

    relocation_masks = {
        "l": (1, 0x0000FFFF),
        "h": (2, 0x0000FFFF),
        "ha": (3, 0x0000FFFF),
        "sda21": (4, 0x001FFFFF),
    }
    for instruction_index in range(instruction_count):
        offset = instruction_index * 4
        word, dtk_mask = struct.unpack_from(">II", blob, instruction_index * 8)
        significant_mask = UINT32_MAX
        relocation_kind = 0
        target_kind = 0
        ordinal = 0
        target_offset = UINT32_MAX
        addend = 0
        relocation = relocations.get(offset)
        branch = _branch_form(word)

        if relocation is not None:
            kind, symbol_index, addend = relocation
            if kind in ("rel24", "rel14"):
                if branch is None or branch[0] != kind:
                    raise CatalogError(
                        f"DTK branch relocation at {offset} in '{requested_symbol}' "
                        "does not match its instruction form"
                    )
                variable_mask = branch[1]
                significant_mask &= ~variable_mask & UINT32_MAX
                if symbol_index == record.primary_symbol and 0 <= addend < function_size:
                    target_kind = 1  # internal branch
                    target_offset = addend
                    internal_branch_count += 1
                else:
                    target_kind = 2  # external branch
                    ordinal = target_ordinal((target_kind, symbol_index))
                    external_branch_count += 1
            else:
                if branch is not None:
                    raise CatalogError(
                        f"DTK data relocation at {offset} in '{requested_symbol}' "
                        "is attached to a branch"
                    )
                relocation_kind, variable_mask = relocation_masks[kind]
                significant_mask &= ~variable_mask & UINT32_MAX
                relocation_count += 1
                if symbol_index == record.primary_symbol and 0 <= addend < function_size:
                    target_kind = 3  # internal reference
                    target_offset = addend
                else:
                    target_kind = 4  # external reference
                    ordinal = target_ordinal((target_kind, symbol_index))
        elif branch is not None:
            _, variable_mask, displacement, absolute = branch
            if absolute:
                # DTK can omit relocation records for absolute executable
                # branches. The DTK record does not carry the function's load
                # address, so map/allowlist layout evidence is required to
                # distinguish internal control flow from an external vector.
                if function_address is None:
                    raise CatalogError(
                        f"DTK function '{requested_symbol}' contains an absolute "
                        "branch but has no function layout address"
                    )
                decoded_target = displacement & UINT32_MAX
            else:
                decoded_target = offset + displacement
            if absolute:
                assert function_address is not None
                internal_offset = decoded_target - function_address
                internal = (
                    decoded_target >= function_address
                    and 0 <= internal_offset < function_size
                    and internal_offset % 4 == 0
                )
            else:
                internal_offset = decoded_target
                internal = (
                    0 <= internal_offset < function_size
                    and internal_offset % 4 == 0
                )
            if internal:
                target_kind = 1
                target_offset = internal_offset
                if absolute:
                    significant_mask &= ~variable_mask & UINT32_MAX
                internal_branch_count += 1
            else:
                target_kind = 2
                significant_mask &= ~variable_mask & UINT32_MAX
                ordinal = target_ordinal(
                    (
                        target_kind,
                        "absolute" if absolute else "relative",
                        decoded_target,
                    )
                )
                external_branch_count += 1

        inferred_linker_branch_mask = (
            relocation is None
            and branch is not None
            and dtk_mask == UINT32_MAX
            and significant_mask != UINT32_MAX
        )
        if dtk_mask != significant_mask and not inferred_linker_branch_mask:
            raise CatalogError(
                f"DTK mask 0x{dtk_mask:08x} at {offset} in '{requested_symbol}' "
                f"does not match validated mask 0x{significant_mask:08x}"
            )
        normalized_word = word & significant_mask
        if significant_mask == UINT32_MAX:
            fixed_count += 1
            if not _boilerplate_word(normalized_word):
                meaningful_fixed_count += 1
        instruction_records.append(
            (
                offset,
                significant_mask,
                normalized_word,
                relocation_kind,
                target_kind,
                ordinal,
                target_offset,
                addend,
            )
        )

    digest = hashlib.sha256()
    digest.update(b"PPSIG\x00\x00\x01")
    digest.update(_u32(SIGNATURE_ALGORITHM_VERSION))
    digest.update(_u32(function_size))
    digest.update(_u32(instruction_count))
    digest.update(_u32(0))  # issue_flags; malformed DTK input is rejected above.
    for (
        offset,
        significant_mask,
        normalized_word,
        relocation_kind,
        target_kind,
        ordinal,
        target_offset,
        addend,
    ) in instruction_records:
        digest.update(_u32(offset))
        digest.update(_u32(significant_mask))
        digest.update(_u32(normalized_word))
        digest.update(bytes((relocation_kind, target_kind, 0, 0)))
        digest.update(_u32(ordinal))
        digest.update(_u32(target_offset))
        digest.update(_u64(addend))

    return SignatureResult(
        sha256=digest.hexdigest(),
        function_size=function_size,
        instruction_count=instruction_count,
        fixed_instruction_count=fixed_count,
        meaningful_fixed_words=meaningful_fixed_count,
        relocation_count=relocation_count,
        internal_branch_count=internal_branch_count,
        external_branch_count=external_branch_count,
        external_target_count=len(target_ordinals),
    )


def _candidate_context(candidate: Candidate) -> str:
    if candidate.source is None:
        return candidate.canonical_identity
    if candidate.line:
        return f"{candidate.source}:{candidate.line}"
    return candidate.source


def build_catalog(
    elf_path: Path,
    candidates: Sequence[Candidate],
    dtk: DtkClient,
) -> dict[str, object]:
    dtk.validate_version()
    entries_by_identity: dict[str, dict[str, object]] = {}
    identity_origins: dict[str, str] = {}

    with tempfile.TemporaryDirectory(prefix="porpoise-sdk-catalog-") as temporary:
        temporary_path = Path(temporary)
        for index, candidate in enumerate(candidates):
            signature_path = temporary_path / f"signature-{index:08d}.yml"
            record = dtk.read_signature(elf_path, candidate.symbol, signature_path)
            signature = convert_dtk_signature(
                record,
                candidate.symbol,
                expected_size=candidate.expected_size,
                function_address=candidate.address,
            )
            entry: dict[str, object] = {
                "canonical_identity": candidate.canonical_identity,
                "category": candidate.category,
                "signature": signature.to_json(),
            }
            if candidate.contract is not None:
                entry["contract"] = candidate.contract

            existing = entries_by_identity.get(candidate.canonical_identity)
            if existing is None:
                entries_by_identity[candidate.canonical_identity] = entry
                identity_origins[candidate.canonical_identity] = _candidate_context(candidate)
            elif existing != entry:
                raise CatalogError(
                    f"canonical identity '{candidate.canonical_identity}' conflicts between "
                    f"{identity_origins[candidate.canonical_identity]} and "
                    f"{_candidate_context(candidate)}"
                )

    entries = sorted(
        entries_by_identity.values(),
        key=lambda entry: (
            str(entry["canonical_identity"]),
            str(entry["category"]),
            str(entry.get("contract", "")),
            str(entry["signature"]),
        ),
    )
    return {
        "schema_version": CATALOG_SCHEMA_VERSION,
        "signature_algorithm_version": SIGNATURE_ALGORITHM_VERSION,
        "entries": entries,
    }


def write_catalog_atomic(path: Path, catalog: Mapping[str, object]) -> None:
    parent = path.parent
    try:
        parent.mkdir(parents=True, exist_ok=True)
    except OSError as error:
        raise CatalogError(f"cannot create output directory '{parent}': {error}") from error
    payload = json.dumps(catalog, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            newline="\n",
            prefix=f".{path.name}.",
            suffix=".tmp",
            dir=parent,
            delete=False,
        ) as output:
            temporary_path = Path(output.name)
            output.write(payload)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary_path, path)
        temporary_path = None
    except OSError as error:
        raise CatalogError(f"cannot publish catalog '{path}': {error}") from error
    finally:
        if temporary_path is not None:
            try:
                temporary_path.unlink()
            except OSError:
                pass


def _resolve_dtk_command(value: str | None) -> tuple[str, ...]:
    selected = value or os.environ.get("PORPOISE_DTK") or shutil.which("dtk")
    if not selected:
        raise CatalogError("DTK was not found; use --dtk or set PORPOISE_DTK")
    path = Path(selected)
    if path.suffix.casefold() == ".py":
        return (sys.executable, str(path))
    return (selected,)


def _validate_input_file(path: Path, label: str) -> Path:
    if not path.is_file():
        raise CatalogError(f"{label} '{path}' is not a file")
    return path.resolve()


def _dry_run_document(
    dtk_command: Sequence[str],
    elf_path: Path,
    candidates: Sequence[Candidate],
    diagnostics: Mapping[str, object],
) -> dict[str, object]:
    commands = [
        [
            *dtk_command,
            "--no-color",
            "elf",
            "sigs",
            "-s",
            candidate.symbol,
            "-o",
            "<signature-output.yml>",
            str(elf_path),
        ]
        for candidate in candidates
    ]
    return {
        "entry_count": len(candidates),
        "commands": commands,
        "diagnostics": diagnostics,
    }


def _tool_diagnostics(skipped_ambiguous: Sequence[Mapping[str, object]]) -> dict[str, object]:
    return {
        "schema_version": 1,
        "skipped_ambiguous_count": len(skipped_ambiguous),
        "skipped_ambiguous": list(skipped_ambiguous),
    }


def make_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="porpoise-sdk-catalog",
        description=(
            "Build a strict Porpoise SDK catalog from an ELF and either a "
            "CodeWarrior map or an explicit JSON allowlist."
        ),
        epilog=(
            "Allowlist v1: {\"schema_version\":1,\"entries\":[{\"symbol\":"
            "\"OSReport\",\"canonical_identity\":\"sdk:OSReport\",\"category\":"
            "\"nintendo_dolphin\",\"contract\":\"OSReport\",\"address\":"
            "2147487744}]}\nThe contract and aligned uint32 address keys are "
            "optional. An address is required if the selected function contains "
            "an absolute branch. Categories are explicit; symbol-name prefixes "
            "are never used."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--elf", required=True, type=Path, help="input ELF file")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument(
        "--map",
        dest="map_path",
        type=Path,
        help="CodeWarrior linker map; exact archive ownership selects entries",
    )
    source.add_argument(
        "--allowlist",
        type=Path,
        help="strict version-1 JSON list of explicitly classified symbols",
    )
    parser.add_argument("--output", type=Path, help="catalog JSON to publish")
    parser.add_argument(
        "--dtk",
        help="DTK executable (or Python script for an injected test driver)",
    )
    parser.add_argument(
        "--library",
        action="append",
        default=[],
        metavar="CATEGORY=ARCHIVE",
        help="classify one additional exact map archive basename; repeatable",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="validate inputs and print planned DTK commands without running them",
    )
    parser.add_argument(
        "--diagnostics",
        type=Path,
        help="optional machine-readable JSON describing skipped ambiguous map symbols",
    )
    return parser


def main(argv: Sequence[str] | None = None, runner: Runner | None = None) -> int:
    parser = make_argument_parser()
    arguments = parser.parse_args(argv)
    try:
        elf_path = _validate_input_file(arguments.elf, "ELF")
        overrides = parse_library_overrides(arguments.library)
        skipped_ambiguous: list[dict[str, object]] = []
        if arguments.map_path is not None:
            map_path = _validate_input_file(arguments.map_path, "map")
            candidates = load_codewarrior_map(
                map_path, overrides, skipped_ambiguous=skipped_ambiguous
            )
        else:
            if overrides:
                raise CatalogError("--library is only valid with --map")
            allowlist_path = _validate_input_file(arguments.allowlist, "allowlist")
            candidates = load_allowlist(allowlist_path)
        diagnostics = _tool_diagnostics(skipped_ambiguous)
        for skipped in skipped_ambiguous:
            print(
                f"porpoise-sdk-catalog: warning: map symbol '{skipped['symbol']}' "
                f"is ambiguous across {skipped['group_size']} function records; "
                f"excluding '{skipped['excluded_identity']}' at "
                f"0x{skipped['excluded_address']:08x}",
                file=sys.stderr,
            )
        try:
            dtk_command = _resolve_dtk_command(arguments.dtk)
        except CatalogError:
            if not arguments.dry_run:
                raise
            dtk_command = ("dtk",)
        if arguments.dry_run:
            print(
                json.dumps(
                    _dry_run_document(dtk_command, elf_path, candidates, diagnostics),
                    indent=2,
                    ensure_ascii=False,
                )
            )
            if arguments.diagnostics is not None:
                write_catalog_atomic(arguments.diagnostics, diagnostics)
            return 0
        if arguments.output is None:
            raise CatalogError("--output is required unless --dry-run is used")
        if (
            arguments.diagnostics is not None
            and arguments.output.resolve() == arguments.diagnostics.resolve()
        ):
            raise CatalogError("--output and --diagnostics must name different files")
        dtk = DtkClient(dtk_command, runner=runner)
        catalog = build_catalog(elf_path, candidates, dtk)
        write_catalog_atomic(arguments.output, catalog)
        if arguments.diagnostics is not None:
            write_catalog_atomic(arguments.diagnostics, diagnostics)
        print(
            f"wrote {len(catalog['entries'])} catalog entries to "
            f"{arguments.output}"
        )
        return 0
    except CatalogError as error:
        print(f"porpoise-sdk-catalog: error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
