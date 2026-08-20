# Annotated assembly input

Porpoise Tool reads a deliberately small annotated assembly dialect. It is not a general PowerPC assembler, preprocessor, disassembler, or DOL reader. The text must already contain function boundaries and an address/encoding comment for every instruction to lift.

## Input selection

`INPUT` may be one `.s`/`.S` file or a directory. Directories are scanned recursively, and matching relative paths are sorted before parsing so output and reports are deterministic.

Nested paths are preserved beneath `src/lifted` and the private declaration tree `src/generated` after each path component is sanitized as a portable C/output name. Inputs are rejected if two paths collide after case-insensitive sanitization. Function symbols must also be unique after sanitization, apart from the proven exact-duplicate records described below.

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

`.include`, `.text`, and `.global` are tolerated directives; Porpoise Tool does not execute or interpret them. The function's global/local status comes from the `.fn` line. In particular, `.global main` alone does not make a function eligible for automatic entry selection; use `.fn main, global`. The `.sym` address-alias directive described below is interpreted.

## Function blocks

A function begins with:

```asm
.fn symbol, global
```

or a local declaration such as:

```asm
.fn local_symbol, local
```

Weak declarations are also accepted:

```asm
.fn weak_symbol, weak
```

`global` and `weak` functions are externally visible for symbol resolution and entry detection; `local` functions are not. A name may be quoted, but the resulting symbol must remain unique after conversion to a C identifier.

Every function must:

- have a matching `.endfn`;
- contain at least one annotated instruction;
- not contain another `.fn` block;
- have a unique symbol and nonoverlapping address range among translated functions, apart from the proven exact-duplicate records described below.

The name following `.endfn` is optional. When present, it must match the open function exactly.

### Exact duplicate function records

A recursively scanned tree can contain redundant descriptions of the same linked code. Porpoise coalesces two function records only when their start address, size, item-kind sequence, instruction addresses, encoded words, mnemonics, and labels are identical. Operand text must also be identical, except that the symbol token immediately before the same supported `@h`, `@ha`, `@l`, or `@sda21` suffix may differ in an instruction and operand position that supports that suffix. For example, a branch target that merely ends in `@l` is not a relocation context and cannot make two bodies equivalent. Supported relocation operands embed the authoritative linked fields described below, so changing only that discarded symbol spelling does not change the instruction.

Path sorting and source order determine the retained record. A repeated declaration with the same name promotes the retained symbol to global visibility if either declaration is global. A different function name becomes an address alias at the shared entry, and aliases attached to redundant records are merged into the retained function. A coalesced function name retains its original function-selection behavior: it may name an explicit entry, ABI export, or skip-list target, and a global `main` declaration remains eligible for automatic entry selection. Only one C body and one address-registry entry are emitted.

This is deliberately not a general overlap rule. A partial overlap, nested range, shared tail, different boundary, different label, changed instruction word or mnemonic, ordinary operand difference, or different relocation suffix remains an error. Alternate entries within one body should be represented explicitly with `.sym`; Porpoise does not infer them from overlapping `.fn` blocks.

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
- Lowering is selected conservatively by mnemonic and validated operand form. For ordinary numeric operands, the encoding is retained for reporting. For the supported already-linked relocation spellings described below, the encoded immediate field is authoritative.
- Instructions must appear inside a `.fn` block.

An opcode known to the registry can still fail if its operand form is invalid or represents a case the lowering does not support. An unknown mnemonic always fails translation. There is intentionally no static “percentage supported” promise; the current registry and the per-run report are the sources of truth.

Inside an ordinary function, a `.4byte` record is not assumed to be data or silently decoded as an arbitrary instruction. Its numeric operand must match the annotated word. Records explicitly tagged by decomp-toolkit with `/* invalid */` or `/* illegal: ... */` become an approximate terminal illegal-instruction fault because the lifted shim does not dispatch a guest program-exception vector. The characterized reserved `lmw` overlap form is decoded only when the register fields actually overlap and the metadata begins `/* illegal: lmw ... */`; its effective address is captured before the overlapping register restore. Other raw words are unsupported and fail translation. `--strict` rejects both accepted raw-word approximations.

### Already-linked relocation operands

Porpoise Tool does not link symbols or emit relocation records. It accepts a small set of relocation-decorated operands only when the annotation already contains the final linked instruction word:

```asm
/* 80002000 00001000  3C 60 80 01 */ lis r3, object@ha
/* 80002004 00001004  38 63 FF FC */ addi r3, r3, object@l
/* 80002008 00001008  80 8D 00 20 */ lwz r4, small_object@sda21(r0)
```

The supported suffixes are instruction-specific: `@h` and `@ha` for high-immediate forms, `@l` for low-immediate forms, and `@sda21` for supported small-data immediate and memory forms. The runtime C embeds the linked 16-bit field from the annotation; it does not need or retain the symbol name. For `@sda21`, the annotation's encoded RA field is also authoritative because common disassembly text writes `(r0)` while the linked instruction actually addresses through `r2` or `r13`. Unsupported suffix/instruction combinations are errors.

## Labels and branches

Labels inside a function must end in a colon:

```asm
.loop:
/* 80001100 00000100  42 00 FF FC */  bdnz .loop
```

A label may begin with `.`, `_`, or an ASCII letter. Legacy dot-label lines without a colon are treated as directives rather than branch targets. A label is directly visible in its containing function. A uniquely named label may also be used as a cross-function branch target; an ambiguous label name is not resolved globally.

An address alias uses the strict form:

```asm
.sym alternate_entry, global
/* 80001100 00000100  42 00 FF FC */  bdnz .loop
```

The scope must be exactly `global` or `local`, and quoted names are accepted. An alias immediately before a `.fn` binds to that function's first instruction. An alias inside a function binds to the next annotated instruction. Multiple aliases may bind to the same address, but alias names must remain unique and must not collide with function or label names after C-identifier sanitization. A dangling alias is an error.

Direct and conditional branch targets may resolve to:

- a label in the current function;
- another translated, unskipped input function or one of its address aliases;
- a uniquely named label in another translated, unskipped function; or
- an import declared by the ABI manifest.

An unresolved or ambiguous named target is an error. Unlinked branches to local labels use direct C labels; linked branches to a local label remain unsupported because they require a guest LR continuation within the same lifted C function. Function starts, address aliases, and labeled instruction entry points use the generated 32-bit address registry. Unique cross-function labels enter the owning lifted function at the bound instruction through that same registry. Indirect `bctr`/`bctrl` dispatches and modeled interrupt return fault at runtime when the address is unknown.

The current `blr` lowering returns through the host C call stack. It cannot reproduce a guest function that rewrites LR and then returns to an arbitrary guest address, so every accepted `blr` is reported as `approximate` and is rejected by `--strict`.

## Directives and data

Porpoise materializes static title data from decomp-toolkit contribution and
object metadata. A contribution starts with an exact linked range followed by
its section directive:

```asm
# 0x80300000..0x80300018 | size: 0x18
.section .rodata, "a"
.balign 8

# .rodata:0x0 | 0x80300000 | size: 0x18
.obj sample_data, global
    .4byte other_object
    .float 1.0
    .double -2.0
    .string "hello\n"
.endobj sample_data
```

Every `.obj` needs fresh metadata of the form
`# SECTION:0xOFFSET | 0xADDRESS | size: 0xSIZE`. Its `.endobj` name must match,
and its directives must emit exactly the declared size. `global` and `weak`
objects participate in global resolution. `local` objects and labels are
resolved in their source file first, so two files may legitimately contain the
same local name.

The interpreted byte emitters are:

- `.byte`, `.2byte`, and `.4byte`, with comma-separated integer literals;
- `.4byte SYMBOL` and `.4byte SYMBOL+ADDEND` absolute-address fixups;
- `.float` and `.double` encoded as big-endian IEEE-754 values;
- `.string` and `.string16`, including the supported C-style, octal, and quote
  escapes and their terminating zero;
- `.rel BASE, TARGET`, whose existing dialect contract stores the resolved
  absolute target address;
- `.skip SIZE` for explicit zero-fill storage; and
- `.balign ALIGNMENT[, FILL]` inside an object.

Decomp-toolkit also emits explicit padding directly between objects. Such a
byte emitter is accepted only while an authoritative linked contribution range
is active and its address can be derived exactly from that contribution's
cursor. The report retains it as anonymous contribution provenance. The same
directive without a usable linked range is an error.

Symbolic data expressions are resolved after every input has been parsed. The
resolver checks object-local labels, same-file objects/functions/aliases and
labels, then unique global definitions. An unresolved, ambiguous, overflowing,
or out-of-object fixup fails translation.

Legacy address-annotated four-byte records remain accepted in data sections:

```asm
.section .rodata
sample_data:
/* 80300000 00000000  12 34 56 78 */ .4byte 0x12345678
```

The annotation's linked address and encoded bytes are authoritative. Synthetic
`gap_SS_AAAAAAAA_section` function records described below are handled through
that same legacy path.

Successful projects generate C byte arrays under `src/data/` and a private
`porpoise_initialize_data(PorpoisePpcState *)` helper. It writes initialized
spans and zeroes BSS/padding through the endian-safe host adapter after
libPorpoise memory setup and before the lifted entry. Porpoise allocates no
console-memory buffer and embeds no ELF/DOL image.

Linker-synthesized tables or sentinels that do not appear in the disassembly
must be supplied explicitly as annotated assembly. Porpoise never guesses them
or recovers them from a linked executable. Known but unsupported byte emitters
such as `.incbin`, malformed metadata, overlapping spans, and spans crossing
the 32-bit address boundary fail translation rather than being ignored.

decomp-toolkit may wrap otherwise unclaimed section bytes in a synthetic function record named `gap_SS_AAAAAAAA_section`, for example `gap_03_80004000_text`. Porpoise recognizes that exact naming shape as dialect metadata only when `AAAAAAAA` equals the first annotated address: every annotated word in the record is preserved as guest data, the record is excluded from lifting and address dispatch, and its function-report status is `data`. This prevents strings, vector tables, and padding that happen to decode as valid opcodes from being reported as executable code. Similar-looking names and records whose embedded address does not match remain ordinary functions. If an exact duplicate has both a synthetic gap name and an ordinary function name, the ordinary declaration is retained as the canonical lifted function regardless of input-file order.

Blank lines, ordinary `#` comment lines, and complete C-style block comments are ignored. A block comment may span physical lines and may appear inside or outside a function; it is metadata only. Annotated instruction records remain active because their mnemonic follows `*/` on the same physical line. An unterminated block comment is an error reported at its opening line. Lines longer than 4095 bytes and unrecognized non-directive lines inside a function are errors.

## Skip lists

`--skip-list FILE` names an exact-symbol list:

```text
# Known title function supplied elsewhere
title_specific_stub
unused_debug_path
```

Whitespace is trimmed and `#` starts an inline comment. Every nonempty symbol must exist in the parsed input; a typo is an error rather than a silent skip. Ordinarily, skipped functions are excluded from lowering, entry selection, the generated static library, and indirect function dispatch, and remain listed as `skipped` in `porpoise-report.json`.

If a schema-validated ABI import names the exact skipped function (or a coalesced duplicate function name at the same entry), its guest entry address is instead bound to that typed import bridge. Named branches, numeric-address calls, and indirect `bctr`/`bctrl` dispatch then use the host implementation, and the function is reported as `imported`. An ABI import that names an ordinary `.sym` alternate entry is rejected because it cannot replace the whole owning function consistently. The binding must be explicit: skip lists do not guess SDK families or ABIs, and an ABI import may not shadow an unskipped function. Skipping every function remains a translation error because a generated project must retain lifted title code.

## Failure behavior

Malformed functions, ambiguous duplicate symbols or address ranges, unsupported instructions, unresolved branches, and strict-mode approximations cause exit code `3`. The generated project is built in a sibling staging directory and is not published on failure, so a pre-existing destination is not partially updated.
