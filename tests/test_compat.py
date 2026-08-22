#!/usr/bin/env python3
"""Self-tests for the opt-in libPorpoise compatibility checker."""

from __future__ import annotations

import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


if len(sys.argv) < 3 or sys.argv[2] != "--cc":
    raise SystemExit("usage: test_compat.py SOURCE_ROOT --cc COMPILER [ARG ...]")

root = Path(sys.argv[1]).resolve()
compiler = sys.argv[3:]
if not compiler:
    raise SystemExit("test_compat.py: missing compiler command")

checker = root / "tools" / "check_libporpoise_compat.py"
fixture = root / "tests" / "fixtures" / "libporpoise_stub"
target = "win64" if sys.platform == "win32" else "linux"
compile_scope = (
    "scope: compile interface only; declarations, version gates, enum "
    "values, and consumer source compatibility"
)
semantic_scope = (
    "semantic conformance: NOT TESTED (no linking or execution; full-span "
    "validation, canonical XFB materialization, VI latch/presentation, "
    "and exactly-once clear require libPorpoise runtime/integration tests)"
)


def fixture_digest() -> str:
    digest = hashlib.sha256()
    for path in sorted(path for path in fixture.rglob("*") if path.is_file()):
        digest.update(path.relative_to(fixture).as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def run_checker(
    *extra: str,
    checkout: Path = fixture,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(checker),
            str(checkout),
            "--source-root",
            str(root),
            "--target",
            target,
            *extra,
            "--cc",
            *compiler,
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def assert_compile_only_scope(result: subprocess.CompletedProcess[str]) -> None:
    for expected in (compile_scope, semantic_scope):
        if expected not in result.stdout:
            raise AssertionError(
                f"compatibility checker omitted {expected!r}:\n{result.stdout}"
            )


runtime_guest_os = root / "runtime" / "src" / "porpoise_libporpoise_guest_os.c"
strict_autodetect_args = [
    "-std=c99",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-Werror",
    "-DPORPOISE_AUTODETECT_LIBPORPOISE_HOST_THREAD_CARRIER_V1=1",
    f"-I{root / 'runtime' / 'include'}",
    f"-I{root / 'runtime' / 'src'}",
]
carrier_feature_define = (
    "#define PORPOISE_HAVE_LIBPORPOISE_HOST_THREAD_CARRIER_V1 1"
)


def run_carrier_compile(
    include_directories: list[Path],
    *extra: str,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            *compiler,
            *strict_autodetect_args,
            *(f"-I{path}" for path in include_directories),
            *extra,
            "-fsyntax-only",
            str(runtime_guest_os),
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def carrier_feature_is_enabled(include_directories: list[Path]) -> bool:
    result = subprocess.run(
        [
            *compiler,
            *strict_autodetect_args,
            *(f"-I{path}" for path in include_directories),
            "-dM",
            "-E",
            str(runtime_guest_os),
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if result.returncode != 0:
        raise AssertionError(
            "carrier feature macro probe failed:\n" + result.stdout
        )
    return carrier_feature_define in result.stdout.splitlines()


def check_carrier_autodetection() -> None:
    absent = run_carrier_compile([])
    if absent.returncode != 0 or carrier_feature_is_enabled([]):
        raise AssertionError(
            "absent carrier header did not select the strict fallback:\n"
            + absent.stdout
        )

    fixture_includes = [fixture / "include", fixture / "private"]
    present_v1 = run_carrier_compile(fixture_includes)
    if present_v1.returncode != 0 or not carrier_feature_is_enabled(
        fixture_includes
    ):
        raise AssertionError(
            "exact carrier v1 header did not enable the strict carrier path:\n"
            + present_v1.stdout
        )

    with tempfile.TemporaryDirectory(
        prefix="porpoise-carrier-autodetect-"
    ) as temporary:
        temporary_root = Path(temporary)
        wrong_root = temporary_root / "wrong"
        unversioned_root = temporary_root / "unversioned"
        variants = (
            (
                wrong_root,
                "#define LIBPORPOISE_HOST_THREAD_CARRIER_API_VERSION 2U\n",
            ),
            (unversioned_root, ""),
        )
        for include_root, version_line in variants:
            header = include_root / "porpoise" / "host_thread_carrier.h"
            header.parent.mkdir(parents=True)
            header.write_text(
                "#ifndef TEST_HOST_THREAD_CARRIER_H\n"
                "#define TEST_HOST_THREAD_CARRIER_H\n"
                + version_line
                + "#endif\n",
                encoding="utf-8",
            )
            result = run_carrier_compile([include_root])
            if result.returncode != 0 or carrier_feature_is_enabled(
                [include_root]
            ):
                raise AssertionError(
                    "non-v1 carrier header did not select the strict fallback:\n"
                    + result.stdout
                )

        forced_wrong = run_carrier_compile(
            [wrong_root, fixture / "private"],
            "-DPORPOISE_HAVE_LIBPORPOISE_HOST_THREAD_CARRIER_V1=1",
        )
        if (
            forced_wrong.returncode == 0
            or "requires API version 1" not in forced_wrong.stdout
        ):
            raise AssertionError(
                "explicitly forced carrier contract accepted a wrong version:\n"
                + forced_wrong.stdout
            )


before = fixture_digest()

check_carrier_autodetection()

good = run_checker()
if good.returncode != 0:
    raise AssertionError(f"valid fixture failed:\n{good.stdout}")
assert_compile_only_scope(good)
required_passes = (
    "PASS generated-runtime-header-consumer:",
    "PASS host-thread-carrier-v1:",
    "PASS consumer-headers:",
    "PASS gx-copy-enum-values:",
    "PASS gx-copy-yuva8-a8-distinct:",
    "PASS gx-canonical-fifo-bytes-v1:",
    "PASS gx-host-array-contract:",
    "PASS gx-copy-disp-destination-v1:",
    "PASS gx-copy-tex-destination-v1:",
    "PASS vi-next-framebuffer-guest-address-v1:",
    "result: COMPILE-COMPATIBLE (10 of 10 gates passed)",
)
for expected in required_passes:
    if expected not in good.stdout:
        raise AssertionError(f"missing {expected!r} in checker output:\n{good.stdout}")

with tempfile.TemporaryDirectory(
    prefix="porpoise-no-carrier-checkout-"
) as temporary:
    no_carrier_fixture = Path(temporary) / "libporpoise"
    shutil.copytree(fixture, no_carrier_fixture)
    (
        no_carrier_fixture
        / "include"
        / "porpoise"
        / "host_thread_carrier.h"
    ).unlink()
    no_carrier = run_checker(checkout=no_carrier_fixture)
if no_carrier.returncode != 0:
    raise AssertionError(
        "checkout without the optional carrier failed compatibility:\n"
        + no_carrier.stdout
    )
assert_compile_only_scope(no_carrier)
for expected in (
    "LIMITED host-thread-carrier-v1: header absent; "
    "single-thread compatibility only",
    "PASS generated-runtime-header-consumer:",
    "result: COMPILE-COMPATIBLE "
    "(9 of 9 gates passed; single-thread compatibility only)",
):
    if expected not in no_carrier.stdout:
        raise AssertionError(
            f"missing {expected!r} in no-carrier output:\n"
            + no_carrier.stdout
        )

with tempfile.TemporaryDirectory(
    prefix="porpoise-wrong-carrier-checkout-"
) as temporary:
    wrong_carrier_fixture = Path(temporary) / "libporpoise"
    shutil.copytree(fixture, wrong_carrier_fixture)
    wrong_carrier_header = (
        wrong_carrier_fixture
        / "include"
        / "porpoise"
        / "host_thread_carrier.h"
    )
    wrong_carrier_header.write_text(
        wrong_carrier_header.read_text(encoding="utf-8").replace(
            "LIBPORPOISE_HOST_THREAD_CARRIER_API_VERSION 1U",
            "LIBPORPOISE_HOST_THREAD_CARRIER_API_VERSION 2U",
            1,
        ),
        encoding="utf-8",
    )
    forced_wrong_carrier = run_checker(
        "--c-arg=-DPORPOISE_HAVE_LIBPORPOISE_HOST_THREAD_CARRIER_V1=1",
        checkout=wrong_carrier_fixture,
    )
if forced_wrong_carrier.returncode != 1:
    raise AssertionError(
        "explicitly forced wrong carrier API was accepted:\n"
        + forced_wrong_carrier.stdout
    )
for expected in (
    "FAIL generated-runtime-header-consumer:",
    "requires API version 1",
    "FAIL host-thread-carrier-v1:",
    "result: COMPILE-INCOMPATIBLE",
):
    if expected not in forced_wrong_carrier.stdout:
        raise AssertionError(
            f"missing {expected!r} in forced-wrong-carrier output:\n"
            + forced_wrong_carrier.stdout
        )

missing_copy_contract = run_checker(
    "--c-arg=-DPORPOISE_STUB_DISABLE_GX_COPY_DISP_GUEST_ADDRESS_CONTRACT",
    "--c-arg=-DPORPOISE_STUB_DISABLE_GX_COPY_TEX_GUEST_ADDRESS_CONTRACT",
)
if missing_copy_contract.returncode != 1:
    raise AssertionError(
        "missing copy-destination contracts did not report incompatibility:\n"
        + missing_copy_contract.stdout
    )
assert_compile_only_scope(missing_copy_contract)
for expected in (
    "FAIL gx-copy-disp-destination-v1:",
    "FAIL gx-copy-tex-destination-v1:",
    "PASS gx-canonical-fifo-bytes-v1:",
    "result: COMPILE-INCOMPATIBLE (2 of 10 gates failed)",
):
    if expected not in missing_copy_contract.stdout:
        raise AssertionError(
            f"missing {expected!r} in checker output:\n"
            + missing_copy_contract.stdout
        )

missing_disp_contract = run_checker(
    "--c-arg=-DPORPOISE_STUB_DISABLE_GX_COPY_DISP_GUEST_ADDRESS_CONTRACT"
)
if missing_disp_contract.returncode != 1:
    raise AssertionError(
        "missing display-copy contract did not report incompatibility:\n"
        + missing_disp_contract.stdout
    )
assert_compile_only_scope(missing_disp_contract)
for expected in (
    "FAIL gx-copy-disp-destination-v1:",
    "PASS gx-copy-tex-destination-v1:",
    "result: COMPILE-INCOMPATIBLE (1 of 10 gates failed)",
):
    if expected not in missing_disp_contract.stdout:
        raise AssertionError(
            f"missing {expected!r} in checker output:\n"
            + missing_disp_contract.stdout
        )

missing_tex_contract = run_checker(
    "--c-arg=-DPORPOISE_STUB_DISABLE_GX_COPY_TEX_GUEST_ADDRESS_CONTRACT"
)
if missing_tex_contract.returncode != 1:
    raise AssertionError(
        "missing texture-copy contract did not report incompatibility:\n"
        + missing_tex_contract.stdout
    )
assert_compile_only_scope(missing_tex_contract)
for expected in (
    "PASS gx-copy-disp-destination-v1:",
    "FAIL gx-copy-tex-destination-v1:",
    "result: COMPILE-INCOMPATIBLE (1 of 10 gates failed)",
):
    if expected not in missing_tex_contract.stdout:
        raise AssertionError(
            f"missing {expected!r} in checker output:\n"
            + missing_tex_contract.stdout
        )

shifted_formats = run_checker(
    "--c-arg=-DPORPOISE_STUB_SHIFTED_GX_COPY_FORMATS"
)
if shifted_formats.returncode != 1:
    raise AssertionError(
        "shifted/aliased GX formats did not report incompatibility:\n"
        + shifted_formats.stdout
    )
assert_compile_only_scope(shifted_formats)
for expected in (
    "FAIL gx-copy-enum-values:",
    "FAIL gx-copy-yuva8-a8-distinct:",
    "PASS gx-copy-disp-destination-v1:",
    "result: COMPILE-INCOMPATIBLE (2 of 10 gates failed)",
):
    if expected not in shifted_formats.stdout:
        raise AssertionError(
            f"missing {expected!r} in checker output:\n" + shifted_formats.stdout
        )

missing_vi_contract = run_checker(
    "--c-arg=-DPORPOISE_STUB_DISABLE_VI_NEXT_FRAMEBUFFER_GUEST_ADDRESS_CONTRACT"
)
if missing_vi_contract.returncode != 1:
    raise AssertionError(
        "missing VI next-framebuffer contract did not report incompatibility:\n"
        + missing_vi_contract.stdout
    )
assert_compile_only_scope(missing_vi_contract)
for expected in (
    "PASS gx-copy-disp-destination-v1:",
    "PASS gx-copy-tex-destination-v1:",
    "FAIL vi-next-framebuffer-guest-address-v1:",
    "result: COMPILE-INCOMPATIBLE (1 of 10 gates failed)",
):
    if expected not in missing_vi_contract.stdout:
        raise AssertionError(
            f"missing {expected!r} in checker output:\n"
            + missing_vi_contract.stdout
        )

with tempfile.TemporaryDirectory(prefix="porpoise-broken-cc-") as temporary:
    fake_compiler = Path(temporary) / "fake_compiler.py"
    fake_compiler.write_text(
        "import sys\n"
        "if '--version' in sys.argv:\n"
        "    print('fake gcc 1.0')\n"
        "    raise SystemExit(0)\n"
        "raise SystemExit(1)\n",
        encoding="utf-8",
    )
    broken_compiler = subprocess.run(
        [
            sys.executable,
            str(checker),
            str(fixture),
            "--source-root",
            str(root),
            "--target",
            target,
            "--cc",
            sys.executable,
            str(fake_compiler),
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
if broken_compiler.returncode != 2:
    raise AssertionError(
        "unusable compiler was not classified as an environment error:\n"
        + broken_compiler.stdout
    )
if "compiler environment cannot compile a trivial strict C99" not in broken_compiler.stdout:
    raise AssertionError(
        "unusable compiler did not report its health-probe failure:\n"
        + broken_compiler.stdout
    )
if (
    "FAIL " in broken_compiler.stdout
    or "result: COMPILE-INCOMPATIBLE" in broken_compiler.stdout
):
    raise AssertionError(
        "unusable compiler was misleadingly reported as a contract failure:\n"
        + broken_compiler.stdout
    )

after = fixture_digest()
if after != before:
    raise AssertionError("compatibility checker mutated its input checkout")

print("libPorpoise compatibility checker self-test passed")
