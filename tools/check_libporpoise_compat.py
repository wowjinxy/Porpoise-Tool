#!/usr/bin/env python3
"""Read-only compile-time contract probe for a libPorpoise checkout.

The probe never configures or builds libPorpoise.  It compiles temporary C
translation units with the same public headers and consumer defines used by a
generated Porpoise project, then removes the temporary directory.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile


STRICT_C_ARGS = (
    "-std=c99",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-Werror",
    "-fsyntax-only",
)

EXPECTED_VERSION_GATES = {
    "LIBPORPOISE_GX_COPY_DISP_GUEST_ADDRESS_API_VERSION": 1,
    "LIBPORPOISE_GX_COPY_TEX_GUEST_ADDRESS_API_VERSION": 1,
    "LIBPORPOISE_VI_SET_NEXT_FRAME_BUFFER_GUEST_ADDRESS_API_VERSION": 1,
    "SIM_GX_COMMAND_PROCESSOR_CANONICAL_BYTES_API_VERSION": 1,
    "LIBPORPOISE_HOST_THREAD_CARRIER_API_VERSION": 1,
}

COMMON_HEADERS = r"""
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include <dolphin/ar.h>
#include <dolphin/dsp.h>
#include <dolphin/dvd.h>
#include <dolphin/os/OSArena.h>
#include <dolphin/os/OSHostAddress.h>
#include <dolphin/os/OSHostMemory.h>
#include <dolphin/os/OSInterrupt.h>
#include <dolphin/os/OSTime.h>
#include <dolphin/vi.h>
#include <simulator/sim_gx_CommandProcessor.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include "porpoise_libporpoise_gx_headers.h"
"""

PROBES = (
    (
        "host-thread-carrier-v1",
        "host-thread carrier publishes the exact version 1 Tool contract",
        r"""
#include <stdint.h>
#include <porpoise/host_thread_carrier.h>
#if !defined(LIBPORPOISE_HOST_THREAD_CARRIER_API_VERSION)
#error "host-thread carrier API version is absent"
#elif LIBPORPOISE_HOST_THREAD_CARRIER_API_VERSION != 1U
#error "host-thread carrier API version is not exactly 1"
#endif
#define PORPOISE_ASSERT(name, condition) \
    typedef char porpoise_assert_##name[(condition) ? 1 : -1]
PORPOISE_ASSERT(
    carrier_entry_signature,
    __builtin_types_compatible_p(
        LibPorpoiseHostThreadCarrierEntryV1,
        void (*)(void *)));
PORPOISE_ASSERT(
    carrier_create_signature,
    __builtin_types_compatible_p(
        __typeof__(&LibPorpoiseHostThreadCarrierCreatePausedV1),
        LibPorpoiseHostThreadCarrierResultV1 (*)(
            const LibPorpoiseHostThreadCarrierConfigV1 *,
            LibPorpoiseHostThreadCarrier **)));
PORPOISE_ASSERT(
    carrier_resume_signature,
    __builtin_types_compatible_p(
        __typeof__(&LibPorpoiseHostThreadCarrierResumeV1),
        LibPorpoiseHostThreadCarrierResultV1 (*)(
            LibPorpoiseHostThreadCarrier *, int32_t *)));
PORPOISE_ASSERT(
    carrier_suspend_signature,
    __builtin_types_compatible_p(
        __typeof__(&LibPorpoiseHostThreadCarrierSuspendCurrentV1),
        LibPorpoiseHostThreadCarrierResultV1 (*)(
            LibPorpoiseHostThreadCarrier *, int32_t *)));
PORPOISE_ASSERT(
    carrier_stop_signature,
    __builtin_types_compatible_p(
        __typeof__(&LibPorpoiseHostThreadCarrierRequestStopV1),
        LibPorpoiseHostThreadCarrierResultV1 (*)(
            LibPorpoiseHostThreadCarrier *)));
PORPOISE_ASSERT(
    carrier_join_signature,
    __builtin_types_compatible_p(
        __typeof__(&LibPorpoiseHostThreadCarrierJoinV1),
        LibPorpoiseHostThreadCarrierResultV1 (*)(
            LibPorpoiseHostThreadCarrier *, uint32_t)));
PORPOISE_ASSERT(
    carrier_destroy_signature,
    __builtin_types_compatible_p(
        __typeof__(&LibPorpoiseHostThreadCarrierDestroyV1),
        LibPorpoiseHostThreadCarrierResultV1 (*)(
            LibPorpoiseHostThreadCarrier *)));
""",
    ),
    (
        "consumer-headers",
        "generated runtime headers compile as a strict host consumer",
        COMMON_HEADERS
        + r"""
int porpoise_libporpoise_header_probe(void)
{
    return 0;
}
""",
    ),
    (
        "gx-copy-enum-values",
        "GX texture/copy enum values use the canonical GameCube encoding",
        COMMON_HEADERS
        + r"""
#define PORPOISE_ASSERT(name, condition) \
    typedef char porpoise_assert_##name[(condition) ? 1 : -1]
PORPOISE_ASSERT(tf_i4, GX_TF_I4 == 0x00);
PORPOISE_ASSERT(tf_i8, GX_TF_I8 == 0x01);
PORPOISE_ASSERT(tf_ia4, GX_TF_IA4 == 0x02);
PORPOISE_ASSERT(tf_ia8, GX_TF_IA8 == 0x03);
PORPOISE_ASSERT(tf_rgb565, GX_TF_RGB565 == 0x04);
PORPOISE_ASSERT(tf_rgb5a3, GX_TF_RGB5A3 == 0x05);
PORPOISE_ASSERT(tf_rgba8, GX_TF_RGBA8 == 0x06);
PORPOISE_ASSERT(tf_c4, GX_TF_C4 == 0x08);
PORPOISE_ASSERT(tf_c8, GX_TF_C8 == 0x09);
PORPOISE_ASSERT(tf_c14x2, GX_TF_C14X2 == 0x0A);
PORPOISE_ASSERT(tf_cmpr, GX_TF_CMPR == 0x0E);
PORPOISE_ASSERT(tf_z8, GX_TF_Z8 == 0x11);
PORPOISE_ASSERT(tf_z16, GX_TF_Z16 == 0x13);
PORPOISE_ASSERT(tf_z24x8, GX_TF_Z24X8 == 0x16);
PORPOISE_ASSERT(ctf_r4, GX_CTF_R4 == 0x20);
PORPOISE_ASSERT(ctf_ra4, GX_CTF_RA4 == 0x22);
PORPOISE_ASSERT(ctf_ra8, GX_CTF_RA8 == 0x23);
PORPOISE_ASSERT(ctf_yuva8, GX_CTF_YUVA8 == 0x26);
PORPOISE_ASSERT(ctf_a8, GX_CTF_A8 == 0x27);
PORPOISE_ASSERT(ctf_r8, GX_CTF_R8 == 0x28);
PORPOISE_ASSERT(ctf_g8, GX_CTF_G8 == 0x29);
PORPOISE_ASSERT(ctf_b8, GX_CTF_B8 == 0x2A);
PORPOISE_ASSERT(ctf_rg8, GX_CTF_RG8 == 0x2B);
PORPOISE_ASSERT(ctf_gb8, GX_CTF_GB8 == 0x2C);
PORPOISE_ASSERT(ctf_z4, GX_CTF_Z4 == 0x30);
PORPOISE_ASSERT(ctf_z8m, GX_CTF_Z8M == 0x39);
PORPOISE_ASSERT(ctf_z8l, GX_CTF_Z8L == 0x3A);
PORPOISE_ASSERT(ctf_z16l, GX_CTF_Z16L == 0x3C);
""",
    ),
    (
        "gx-copy-yuva8-a8-distinct",
        "GX_CTF_YUVA8 and GX_CTF_A8 are distinct",
        COMMON_HEADERS
        + r"""
#define PORPOISE_ASSERT(name, condition) \
    typedef char porpoise_assert_##name[(condition) ? 1 : -1]
PORPOISE_ASSERT(yuva8_a8_distinct, GX_CTF_YUVA8 != GX_CTF_A8);
""",
    ),
    (
        "gx-canonical-fifo-bytes-v1",
        "canonical GX FIFO byte ingress advertises API version >= 1 and its exact signature",
        COMMON_HEADERS
        + r"""
#if !defined(SIM_GX_COMMAND_PROCESSOR_CANONICAL_BYTES_API_VERSION)
#error "SIM_GX_COMMAND_PROCESSOR_CANONICAL_BYTES_API_VERSION is absent"
#elif SIM_GX_COMMAND_PROCESSOR_CANONICAL_BYTES_API_VERSION < 1
#error "SIM_GX_COMMAND_PROCESSOR_CANONICAL_BYTES_API_VERSION is older than 1"
#endif
#define PORPOISE_ASSERT(name, condition) \
    typedef char porpoise_assert_##name[(condition) ? 1 : -1]
PORPOISE_ASSERT(
    canonical_fifo_signature,
    __builtin_types_compatible_p(
        __typeof__(&SIM_GX_CommandProcessor_SendCanonicalBytes),
        GXBool (*)(const u8 *, u32)));
int porpoise_fifo_signature_probe(void)
{
    GXBool (*function_pointer)(const u8 *, u32) =
        SIM_GX_CommandProcessor_SendCanonicalBytes;
    return function_pointer == 0;
}
""",
    ),
    (
        "gx-host-array-contract",
        "GXHostArray publishes GXSetArrayCanonicalSized with the exact Tool contract",
        COMMON_HEADERS
        + r"""
#if !defined(LIBPORPOISE_DOLPHIN_GX_HOST_ARRAY_H)
#error "dolphin/gx/GXHostArray.h contract is absent"
#endif
#define PORPOISE_ASSERT(name, condition) \
    typedef char porpoise_assert_##name[(condition) ? 1 : -1]
PORPOISE_ASSERT(
    canonical_array_signature,
    __builtin_types_compatible_p(
        __typeof__(&GXSetArrayCanonicalSized),
        void (*)(GXAttr, void *, u32, u8)));
int porpoise_host_array_signature_probe(void)
{
    void (*function_pointer)(GXAttr, void *, u32, u8) =
        GXSetArrayCanonicalSized;
    return function_pointer == 0;
}
""",
    ),
    (
        "gx-copy-disp-destination-v1",
        "GXCopyDisp advertises the exact guest-address destination contract version >= 1",
        COMMON_HEADERS
        + r"""
#define PORPOISE_ASSERT(name, condition) \
    typedef char porpoise_assert_##name[(condition) ? 1 : -1]
#if !defined(LIBPORPOISE_GX_COPY_DISP_GUEST_ADDRESS_API_VERSION)
#error "GXCopyDisp guest-address contract is absent"
#elif LIBPORPOISE_GX_COPY_DISP_GUEST_ADDRESS_API_VERSION < 1
#error "GXCopyDisp guest-address contract is older than 1"
#endif
PORPOISE_ASSERT(
    copy_disp_guest_address_signature,
    __builtin_types_compatible_p(
        __typeof__(&GXHostCopyDispGuestAddress),
        GXBool (*)(u32, GXBool)));
int porpoise_copy_disp_signature_probe(void)
{
    GXBool (*function_pointer)(u32, GXBool) =
        GXHostCopyDispGuestAddress;
    return function_pointer == 0;
}
""",
    ),
    (
        "gx-copy-tex-destination-v1",
        "GXCopyTex advertises the exact guest-address destination contract version >= 1",
        COMMON_HEADERS
        + r"""
#define PORPOISE_ASSERT(name, condition) \
    typedef char porpoise_assert_##name[(condition) ? 1 : -1]
#if !defined(LIBPORPOISE_GX_COPY_TEX_GUEST_ADDRESS_API_VERSION)
#error "GXCopyTex guest-address contract is absent"
#elif LIBPORPOISE_GX_COPY_TEX_GUEST_ADDRESS_API_VERSION < 1
#error "GXCopyTex guest-address contract is older than 1"
#endif
PORPOISE_ASSERT(
    copy_tex_guest_address_signature,
    __builtin_types_compatible_p(
        __typeof__(&GXHostCopyTexGuestAddress),
        GXBool (*)(u32, GXBool)));
int porpoise_copy_tex_signature_probe(void)
{
    GXBool (*function_pointer)(u32, GXBool) =
        GXHostCopyTexGuestAddress;
    return function_pointer == 0;
}
""",
    ),
    (
        "vi-next-framebuffer-guest-address-v1",
        "VI next-framebuffer selection advertises the exact guest-address contract version >= 1",
        COMMON_HEADERS
        + r"""
#define PORPOISE_ASSERT(name, condition) \
    typedef char porpoise_assert_##name[(condition) ? 1 : -1]
#if !defined(LIBPORPOISE_VI_SET_NEXT_FRAME_BUFFER_GUEST_ADDRESS_API_VERSION)
#error "VI next-framebuffer guest-address contract is absent"
#elif LIBPORPOISE_VI_SET_NEXT_FRAME_BUFFER_GUEST_ADDRESS_API_VERSION < 1
#error "VI next-framebuffer guest-address contract is older than 1"
#endif
PORPOISE_ASSERT(
    vi_next_framebuffer_guest_address_signature,
    __builtin_types_compatible_p(
        __typeof__(&VIHostSetNextFrameBufferGuestAddress),
        BOOL (*)(u32)));
int porpoise_vi_next_framebuffer_signature_probe(void)
{
    BOOL (*function_pointer)(u32) =
        VIHostSetNextFrameBufferGuestAddress;
    return function_pointer == 0;
}
""",
    ),
)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "read-only compile-time compatibility check for a user-supplied "
            "libPorpoise checkout"
        )
    )
    parser.add_argument("libporpoise", type=Path, help="libPorpoise checkout")
    parser.add_argument(
        "--source-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="Porpoise-Tool source root (normally inferred)",
    )
    parser.add_argument(
        "--target",
        choices=("auto", "linux", "win64"),
        default="auto",
        help="generated-project host contract to probe (default: current host)",
    )
    parser.add_argument(
        "--c-arg",
        action="append",
        default=[],
        help="extra GCC-compatible argument; repeat for multiple arguments",
    )
    parser.add_argument(
        "--cc",
        nargs="+",
        help="GCC-compatible compiler command; this option must be last",
    )
    return parser.parse_args()


def default_compiler() -> list[str] | None:
    configured = os.environ.get("CC")
    if configured:
        command = shlex.split(configured, posix=os.name != "nt")
        if command:
            return command
    for candidate in ("cc", "gcc", "clang"):
        resolved = shutil.which(candidate)
        if resolved:
            return [resolved]
    return None


def compiler_environment(command: list[str]) -> dict[str, str]:
    environment = os.environ.copy()
    if os.name != "nt":
        return environment

    compiler_directories: list[str] = []
    for argument in command:
        if argument.startswith("-"):
            continue
        candidate = Path(argument)
        resolved: Path | None = None
        if candidate.is_file():
            resolved = candidate.resolve()
        elif not candidate.parent.name:
            found = shutil.which(argument, path=environment.get("PATH"))
            if found:
                resolved = Path(found).resolve()
        if resolved is not None:
            directory = str(resolved.parent)
            if directory not in compiler_directories:
                compiler_directories.append(directory)
    if compiler_directories:
        previous_path = environment.get("PATH", "")
        environment["PATH"] = os.pathsep.join(
            [*compiler_directories, previous_path]
            if previous_path
            else compiler_directories
        )
    return environment


def discover_version_gates(source_root: Path) -> dict[str, int]:
    discovered: dict[str, int] = {}
    runtime_root = source_root / "runtime" / "src"
    gate_pattern = re.compile(
        r"defined\s*\(\s*(?P<name>[A-Z][A-Z0-9_]*API_VERSION)\s*\)"
        r"\s*&&\s*\\?\s*(?P=name)\s*>=\s*(?P<minimum>[0-9]+)"
    )
    for path in sorted(runtime_root.glob("porpoise_libporpoise_*.[ch]")):
        text = path.read_text(encoding="utf-8")
        # Do not let a newly introduced versioned runtime gate silently escape
        # this checker. Minimums are checked against the explicit reviewed
        # EXPECTED_VERSION_GATES contract below.
        version_names = set(
            re.findall(r"\b[A-Z][A-Z0-9_]*API_VERSION\b", text)
        )
        untracked = sorted(version_names - set(EXPECTED_VERSION_GATES))
        if untracked:
            raise RuntimeError(
                f"untracked runtime API version gate in {path.name}: "
                + ", ".join(untracked)
            )
        for match in gate_pattern.finditer(text):
            name = match.group("name")
            minimum = int(match.group("minimum"))
            previous = discovered.get(name)
            if previous is not None and previous != minimum:
                raise RuntimeError(
                    f"inconsistent minimum for {name}: {previous} and {minimum}"
                )
            discovered[name] = minimum
    return discovered


def compiler_diagnostic(output: str) -> str:
    lines = [line.rstrip() for line in output.splitlines() if line.strip()]
    if not lines:
        return "compiler rejected the contract without a diagnostic"
    for line in lines:
        if "error:" in line.lower() or "fatal error" in line.lower():
            return line.strip()
    return lines[-1].strip()


def main() -> int:
    arguments = parse_arguments()
    source_root = arguments.source_root.resolve()
    checkout = arguments.libporpoise.resolve()
    include_root = checkout / "include"
    gx_header = include_root / "dolphin" / "gx.h"
    split_gx_header = include_root / "dolphin" / "gx" / "GXFrameBuffer.h"
    target = arguments.target
    if target == "auto":
        target = "win64" if os.name == "nt" else "linux"

    if not checkout.is_dir() or not include_root.is_dir():
        print(
            f"error: {checkout} is not a libPorpoise checkout with an include directory",
            file=sys.stderr,
        )
        return 2
    if not gx_header.is_file() and not split_gx_header.is_file():
        print(
            f"error: {checkout} has neither the umbrella nor split Dolphin GX headers",
            file=sys.stderr,
        )
        return 2
    if not (source_root / "runtime" / "src" / "porpoise_libporpoise_gx_headers.h").is_file():
        print(f"error: invalid Porpoise-Tool source root: {source_root}", file=sys.stderr)
        return 2

    command = list(arguments.cc) if arguments.cc else default_compiler()
    if not command:
        print("error: no GCC-compatible C compiler found", file=sys.stderr)
        return 2
    compiler_env = compiler_environment(command)
    try:
        compiler_check = subprocess.run(
            [*command, "--version"],
            env=compiler_env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
    except OSError as error:
        print(f"error: cannot execute C compiler: {error}", file=sys.stderr)
        return 2
    if compiler_check.returncode != 0:
        print(
            "error: C compiler command rejected --version: "
            + compiler_diagnostic(compiler_check.stdout),
            file=sys.stderr,
        )
        return 2

    try:
        discovered = discover_version_gates(source_root)
    except (OSError, UnicodeError, RuntimeError) as error:
        print(f"error: cannot audit runtime version gates: {error}", file=sys.stderr)
        return 2
    if discovered != EXPECTED_VERSION_GATES:
        missing = sorted(set(EXPECTED_VERSION_GATES) - set(discovered))
        print(
            "error: compatibility checker/runtime version-gate drift"
            + (f"; runtime no longer references: {', '.join(missing)}" if missing else ""),
            file=sys.stderr,
        )
        return 2

    target_args = ["-DLIBPORPOISE_PORT"]
    if target == "win64":
        target_args.append("-DLIBPORPOISE_BUILD_WIN64")
    else:
        target_args.extend(
            ["-DLIBPORPOISE_BUILD_LINUX", "-D_POSIX_C_SOURCE=200112L"]
        )

    print("libPorpoise compile-interface compatibility smoke")
    print(f"checkout: {checkout}")
    print(f"target: {target}")
    print(f"compiler: {' '.join(command)}")
    print("mode: read-only temporary compile probes; the checkout is not configured or built")
    print(
        "scope: compile interface only; declarations, version gates, enum "
        "values, and consumer source compatibility"
    )
    print(
        "semantic conformance: NOT TESTED (no linking or execution; full-span "
        "validation, canonical XFB materialization, VI latch/presentation, "
        "and exactly-once clear require libPorpoise runtime/integration tests)"
    )

    failures = 0
    gate_count = len(PROBES) + 1
    with tempfile.TemporaryDirectory(prefix="porpoise-libporpoise-compat-") as temporary:
        temporary_root = Path(temporary)
        health_probe = temporary_root / "compiler_health.c"
        health_probe.write_text("int main(void) { return 0; }\n", encoding="utf-8")
        try:
            health_result = subprocess.run(
                [
                    *command,
                    *STRICT_C_ARGS,
                    *target_args,
                    *arguments.c_arg,
                    str(health_probe),
                ],
                cwd=temporary_root,
                env=compiler_env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )
        except OSError as error:
            print(f"error: cannot execute C compiler: {error}", file=sys.stderr)
            return 2
        if health_result.returncode != 0:
            print(
                "error: compiler environment cannot compile a trivial strict "
                "C99 translation unit",
                file=sys.stderr,
            )
            print(
                "error: " + compiler_diagnostic(health_result.stdout),
                file=sys.stderr,
            )
            return 2

        (temporary_root / "porpoise_dispatch_private.h").write_text(
            "#ifndef PORPOISE_DISPATCH_PRIVATE_H\n"
            "#define PORPOISE_DISPATCH_PRIVATE_H\n"
            "#include <stdint.h>\n"
            "int porpoise_dispatch_available(uint32_t address);\n"
            "#endif\n",
            encoding="utf-8",
        )
        base_compile_command = [
            *command,
            *STRICT_C_ARGS,
            *target_args,
            *arguments.c_arg,
            "-I",
            str(temporary_root),
            "-I",
            str(source_root / "runtime" / "include"),
            "-I",
            str(source_root / "runtime" / "src"),
            "-isystem",
            str(include_root),
        ]

        runtime_failure = ""
        runtime_sources = sorted(
            (source_root / "runtime" / "src").glob(
                "porpoise_libporpoise_*.c"
            )
        )
        if not runtime_sources:
            print("error: no generated libPorpoise runtime sources found", file=sys.stderr)
            return 2
        for runtime_source in runtime_sources:
            result = subprocess.run(
                [*base_compile_command, str(runtime_source)],
                cwd=temporary_root,
                env=compiler_env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )
            if result.returncode != 0:
                runtime_failure = (
                    f"{runtime_source.name}: {compiler_diagnostic(result.stdout)}"
                )
                break
        if runtime_failure:
            failures += 1
            print(
                "FAIL generated-runtime-header-consumer: all generated adapter "
                "sources consume these headers under strict warnings"
            )
            print(f"     {runtime_failure}")
        else:
            print(
                "PASS generated-runtime-header-consumer: all generated adapter "
                "sources consume these headers under strict warnings"
            )

        for index, (name, description, source) in enumerate(PROBES):
            probe = temporary_root / f"probe_{index:02d}_{name}.c"
            probe.write_text(source, encoding="utf-8")
            compile_command = [
                *base_compile_command,
                str(probe),
            ]
            result = subprocess.run(
                compile_command,
                cwd=temporary_root,
                env=compiler_env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )
            if result.returncode == 0:
                print(f"PASS {name}: {description}")
            else:
                failures += 1
                print(f"FAIL {name}: {description}")
                print(f"     {compiler_diagnostic(result.stdout)}")

    if failures:
        print(
            f"result: COMPILE-INCOMPATIBLE "
            f"({failures} of {gate_count} gates failed)"
        )
        return 1
    print(
        f"result: COMPILE-COMPATIBLE "
        f"({gate_count} of {gate_count} gates passed)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
