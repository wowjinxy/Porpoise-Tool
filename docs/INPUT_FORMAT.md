# Annotated assembly input

Porpoise Tool reads a deliberately small annotated assembly dialect. It is not a general PowerPC assembler, preprocessor, disassembler, or DOL reader. The text must already contain function boundaries and an address/encoding comment for every instruction to lift.

## Input selection

`INPUT` may be one `.s`/`.S` file or a directory. Directories are scanned recursively, and matching relative paths are sorted before parsing so output and reports are deterministic.

Nested paths are preserved beneath `src/lifted` and `include/porpoise/generated` after each path component is sanitized as a portable C/output name. Inputs are rejected if two paths collide after case-insensitive sanitization, or if two functions have the same original or sanitized C symbol.

## Minimal example

```asm
.include "macros.inc"

.text
.global add_one
.fn add_one, global
/* 80001000 00000000  38 63 00 01 */  addi r3, r3, 1
.return_zero:
/* 80001004 00000004  4E 80 00 20 */  blr
.endfn add_one
```

`.include`, `.text`, and `.global` are tolerated directives; Porpoise Tool does not execute or interpret them. The function's global/local status comes from the `.fn` line. In particular, `.global main` alone does not make a function eligible for automatic entry selection; use `.fn main, global`.

## Function blocks

A function begins with:

```asm
.fn symbol, global
```

or a local declaration such as:

```asm
.fn local_symbol, local
```

The parser treats a function as global only when `global` appears after its name. A name may be quoted, but the resulting symbol must remain unique after conversion to a C identifier.

Every function must:

- have a matching `.endfn`;
- contain at least one annotated instruction;
- not contain another `.fn` block;
- have a unique symbol and start address among translated functions.

The name following `.endfn` is optional. When present, it must match the open function exactly.

## Instruction annotations

Each instruction line has one of these forms:

```asm
/* ADDRESS OFFSET  B0 B1 B2 B3 */  mnemonic operands
/* ADDRESS OFFSET  WORD        */  mnemonic operands
```

For example:

```asm
/* 80001000 00000000  38 63 00 01 */  addi r3, r3, 1
/* 80001004 00000004  4E800020    */  blr
```

Rules:

- `ADDRESS` is a 32-bit hexadecimal guest address.
- `ADDRESS` and `OFFSET` are each exactly eight hexadecimal digits. `OFFSET` is retained for compatibility with common decompilation output but is not used for lowering.
- The encoding is either exactly four two-digit hexadecimal byte fields or one eight-digit hexadecimal word; extra metadata fields are rejected.
- A nonempty mnemonic must follow `*/`; the remainder of the line is parsed as its operand string.
- Instruction encodings are retained for reporting, while current lowering is selected conservatively by mnemonic and validated operand form.
- Instructions must appear inside a `.fn` block.

An opcode known to the registry can still fail if its operand form is invalid or represents a case the lowering does not support. An unknown mnemonic always fails translation. There is intentionally no static “percentage supported” promise; the current registry and the per-run report are the sources of truth.

## Labels and branches

Labels inside a function must end in a colon:

```asm
.loop:
/* 80001100 00000100  42 00 FF FC */  bdnz .loop
```

A label may begin with `.`, `_`, or an ASCII letter. Labels are local to their containing function. Legacy dot-label lines without a colon are treated as directives rather than branch targets.

Direct and conditional branch targets may resolve to:

- a label in the current function;
- another translated, unskipped input function; or
- an import declared by the ABI manifest.

An unresolved named target is an error. Linked branches to local labels are not currently supported. Indirect `bctr`/`bctrl` dispatches use the generated 32-bit function-address registry and fault at runtime when the address is unknown.

The current `blr` lowering returns through the host C call stack. It cannot reproduce a guest function that rewrites LR and then returns to an arbitrary guest address, so every accepted `blr` is reported as `approximate` and is rejected by `--strict`.

## Directives and data

Most dot-prefixed assembler directives other than `.fn` and `.endfn` are tolerated but not interpreted. In `.data`, `.rodata`, `.sdata`, `.sdata2`, `.bss`, `.sbss`, `.sbss2`, and equivalent `.section` regions, annotated four-byte records are preserved:

```asm
.section .rodata
sample_data:
/* 80300000 00000000  12 34 56 78 */ .4byte 0x12345678
```

The four encoded bytes in the annotation are authoritative. Successful projects expose `porpoise_initialize_data(PorpoisePpcState *)`, which writes each word to its 32-bit guest address through the same endian-safe host adapter used by lifted loads and stores. `DolphinMain` calls this initializer after host memory setup and before the lifted entry. This does not allocate memory or compete with `libPorpoise`.

Unannotated data directives, BSS sizing, relocations, macros, and linker metadata are not emitted. Overlapping annotated words and words crossing the 32-bit address boundary are rejected rather than guessed.

Blank lines and ordinary `#` comment lines are ignored. Lines longer than 4095 bytes and unrecognized non-directive lines inside a function are errors.

## Skip lists

`--skip-list FILE` names an exact-symbol list:

```text
# Known title function supplied elsewhere
title_specific_stub
unused_debug_path
```

Whitespace is trimmed and `#` starts an inline comment. Every nonempty symbol must exist in the parsed input; a typo is an error rather than a silent skip. Skipped functions are excluded from lowering, entry selection, the generated static library, and indirect function dispatch, but remain listed as `skipped` in `porpoise-report.json`. Skipping every function is a translation error.

## Failure behavior

Malformed functions, duplicate symbols or addresses, unsupported instructions, unresolved branches, and strict-mode approximations cause exit code `3`. The generated project is built in a sibling staging directory and is not published on failure, so a pre-existing destination is not partially updated.
