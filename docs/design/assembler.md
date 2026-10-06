# Assembler

Moved out of CLAUDE.md; this is the detail behind the rules summarised there.

## Assembler

`src/core/assembler/` is a Merlin-compatible 65C02 assembler, not a generic
one. Four things in it are load-bearing.

**There is no "pass 1 sizes, pass 2 emits" split.** `assemble()` runs the whole
source to completion repeatedly until the symbol table and the object size stop
moving, then once more with diagnostics on. Macros, conditionals and `LUP`
blocks make the *line stream itself* depend on symbol values, so a sizing pass
that did not emit could not have followed the same path as the pass that did. A
forward reference reads its value from `lastPassSymbols_` — the previous pass's
table — and sets `unresolved_`, which forces absolute addressing so an
instruction never shrinks to zero page on a guess and oscillates.

**Expressions run strictly left to right with no operator precedence.** `1+2*3`
is 9. That is Merlin, and real Merlin sources are written assuming it, so adding
precedence would silently change the bytes they produce. The operators are
`+ - * /` plus `.` (or), `!` (exclusive or) and `&` (and); `<`, `>` and `^`
select the low, high and bank byte of the *whole* expression, which is why the
selector is parsed once in `evaluate()` and applied to the total rather than
being a term-level unary. Outside an immediate, a leading `<` or `|` forces the
address width instead.

**A line is four whitespace-separated fields and the comment needs no
semicolon.** `parseLine` ends the operand at the first space outside a string —
that is why a Merlin operand never contains a space, and why the editor's
validator has no "unexpected extra token" rule. String directives are the
exception: their operand opens with a delimiter of the author's choosing, so it
is scanned to the matching character first. `STRING_DIRECTIVES` in
`merlin-highlighting.js` holds the same list for the editor.

**The editor no longer assembles anything itself.** `AsmLineInfo` reports each
main-source line's address, cycle count and bytes, and
`assembler-editor-window.js` reads that block through one `heapRead`. It used to
carry its own 65C02 opcode table, operand parser and expression evaluator to
fill the gutter; those could not survive macros or conditional assembly, and a
second encoder is a second thing to be wrong. A macro call site is credited with
the bytes its expansion produced (`expandAndRecord`), because the body's lines
are not in the source being edited. Cycle counts come from `CYCLE_TABLE`, per
opcode rather than per mnemonic, so `LDA $10` and `LDA $1000` differ correctly.

`PUT` and `USE` resolve through an `AsmIncludeProvider` callback so the core
stays host-free; `wasm_interface.cpp` installs one that reads text files off the
disk in drive 1 then drive 2, on DOS 3.3 or ProDOS, honouring Merlin's `T.`
prefix convention. Multiple `ORG`s produce multiple `AsmSegment`s, and
`loadAsmIntoMemory` places each where it belongs instead of assuming one block.
`REL`/`ENT`/`EXT`/`LNK` need a linker and are reported as errors; `KBD` and a
second `XC` are reported as *warnings*, a severity `AsmError::warning` carries
so an assembly still succeeds.
