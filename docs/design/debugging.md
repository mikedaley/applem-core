# Debugging

Moved out of CLAUDE.md; this is the detail behind the rules summarised there.

## Debugging

Built-in debug windows accessible via Debug menu:

- CPU Debugger: registers (REGS, FLAGS, TIMING, BEAM sections), breakpoints, stepping, disassembly with symbols. The Breakpoints/Watch/Beam panel under the disassembly has a splitter on its top edge and a fold button at the end of its tab bar; its height and whether it is folded live in the window state, and picking a tab on a folded panel opens it
- Memory Browser: hex/ASCII view of 128KB address space with search
- Memory Heat Map: real-time memory access visualization (read/write/combined modes)
- Memory Map: address space layout overview
- Stack Viewer: live stack contents
- Zero Page Watch: monitor zero page locations with predefined and custom watches
- Soft Switch Monitor: Apple II switch states ($C000-$C0FF)
- Mockingboard: unified channel-centric view with AY-3-8910 and VIA registers, inline waveforms, level meters, and per-channel mute controls
- Mouse Card: PIA registers, position, mode, interrupt state, protocol activity
- BASIC Program Viewer: view, load, and tokenize BASIC programs from memory, line heat map, trace toggle, statement-level breakpoints, conditional breakpoints on variables/arrays, condition-only rules, variable inspector, run/stop/pause/step controls
- Rule Builder: complex conditional breakpoints with C-style expressions, supports CPU registers/memory and BASIC variables/arrays as subjects

### Debugging any machine

**Every debug question is asked once, at the widest shape, and a machine
answers as much of it as it has.** A 6502's answer is a 65816's with the high
halves zero and no banks, so addresses are 24 bits throughout the debug layer
and A/X/Y/SP are 16. What a machine does not have — a program bank, a data
bank, a direct page, a second mode — reads as zero rather than as an error,
because "this machine has none" is the answer. The alternative was a second
set of exports and a second set of windows, and two of everything to keep in
step.

- **A memory view asks for a `MemorySpace`, not an address.**
  `MachineHost::memorySpaces()` lists what there is to browse: a //e's
  processor view, its main and auxiliary RAM and its ROM whatever the
  switches say (each RAM half laid out as a IIgs bank, language card bank 1
  at `$C000`), or a IIgs's banks. `pokeSpace` writes where `peekSpace` reads,
  through `MMU::poke` and `IIgsMemory::poke`, which touch no switch, ignore
  the card's write protect and charge no clock. The native Memory Viewer is
  built on it; `test_machine_host` pins it.
- **A display page can be looked at without being shown.**
  `Video::renderPage` runs the picture's own emitters and decoders over
  page 1 or 2 of any mode, whatever the switches say, saving and restoring
  the line in progress so the frame being drawn is untouched; a IIgs's
  `IIgsVideo::renderSuperHiResPicture` does the same into a frame of its
  own. `MachineHost::renderDisplayPage` routes to either and says which
  pages a machine has. The decode is `Video::decodeLine`, which
  `endScanline` uses too, so there is one. `test_machine_host` pins it.
- **A peek must never read a card.** While a watchpoint is armed,
  `MMU::read` peeks every address first so the callback has a value, so
  `MMU::peek` asks a slot card for `peekROM`, not `readROM`. A SmartPort's
  entry points are traps, and a peek that read them ran every block call
  twice: a //e booting a SmartPort image with any watchpoint set ended in
  the monitor. `test_mmu_slots.cpp` and `test_emulator_disk.cpp` pin it.
- **Execution ranges and stack pointer breakpoints fire on entry.** An exec
  breakpoint over `$2000-$20FF` stops when the PC moves from outside the range
  to inside it, and a stack breakpoint (`MachineDebug::addStackBreakpoint`)
  when SP does the same with its range; neither stops again while the value
  stays inside, or Run inside a range would be Step. A range is primed by the
  first check after it is added, so one put around the code the machine is
  paused in does not fire on resume. Both are measured on every instruction
  whatever else stops the machine, so entry is always relative to the
  instruction before. A stack hit has its own flag (`_isStackBreakpointHit`),
  because the host looks a PC hit up by address. The host keys stack entries
  at `STACK_KEY_BASE` plus the value (`breakpoint-manager.js`), so SP `$F0`
  and an exec breakpoint at `$00F0` can coexist. `test_machine_debug.cpp` pins
  the rules and `test_emulator_debug.cpp`/`test_iigs_debug.cpp` pin them on a
  running machine.
- **`MachineDebug` (`core/debug/machine_debug.*`) is the mechanism**, owned by
  both `Emulator` and `IIgsMachine`: breakpoints (with the temporary one
  behind step over and step out), watchpoints, the trace ring and beam
  breakpoints. None of them is about an instruction set, so none belongs to a
  machine. `beamPosition()` is the beam arithmetic, which both derive from
  their own profile's timing. The //e's older 16-bit methods forward to it.
- **The Profiler is shared** (`core/debug/profiler.hpp`, in `MachineDebug`):
  a call tree kept by a shadow stack popped by the stack pointer, a frame by
  frame timeline, and time per address in banks allocated as code runs in
  them, which is what lets a IIgs have it. Both machines feed it one
  instruction at a time while it is enabled; the //e in CPU cycles, the IIgs
  in the slow clock's time. `docs/NATIVE.md` describes the window.
- **Two things are deliberately not shared.** Cycle profiling is a counter per
  address — 256KB for a 6502 and 64MB for a 65816 — so it stays //e-only and
  the host's heat overlay simply switches itself off. A IIgs records
  *coverage* instead, a bit per address (2MB) held only while it is on
  (`IIgsMachine::setCoverageEnabled`), and `MachineHost::setProfiling` /
  `wasExecuted` answer for either machine. The call-stack summary is
  built by the //e's run loop as it executes JSRs and the IIgs machine keeps no
  such list, so it reports none rather than showing a //e's.
- **A watchpoint on a IIgs is checked on the processor's bus, not inside the
  memory.** That is the difference between the program touching an address and
  anything touching it: the Mega II's video reads the text page on every one of
  192 lines, and a watchpoint there that fired for the scanner would stop the
  machine before a program had run.
- **`ConditionEvaluator` takes a `MachineView`** — a peek function and the
  registers — rather than a `const Emulator&`. Before that, a conditional
  breakpoint on a IIgs was evaluated against a machine that did not exist: it
  silently never fired and every expression read zero.
- **The profile describes the processor** (`processor` in the JSON: address
  bits, register bits, whether there are banks, a direct page and modes, and
  the two sets of flag names a 65816 has), and the host builds its panels from
  it. `machineProcessor()`, `formatMachineAddress()` and `machineAddressMask()`
  in `src/js/machine/machine-profile.js` are how; `BaseWindow.formatAddr()`
  goes through the same formatter so every window writes an address the same
  way — four digits, or a bank and a slash as the machine's own monitor writes
  it. A machine change reaches every window through
  `WindowManager.notifyMachineChanged()`, so a window added later is included
  without anyone remembering.
- **Disassembly is chosen by the core, not the host**, because only something
  holding the live processor can walk a 65816's code stream: its instruction
  lengths depend on the M and X flags. `_disassembleRange` emits three
  tab-separated fields — address, bytes, text — rather than one fixed-width
  string the caller sliced by column, which stopped working the moment an
  address needed six digits and would have failed silently.
- **The listing always shows the centre as an instruction.** `_disassembleRange`
  chooses its start by an alignment search (`disasm_align.hpp`): it tries every
  lookback from the furthest inwards and keeps the first forward walk that
  lands exactly on the centre, falling back to the centre itself with no
  context. A fixed lookback walked forward from a random byte and trusted
  wherever it ended up, so with slot 5 empty and every byte above `$C600`
  reading `$A0`, a misread `LDY #$A2` at `$C5FF` ate the first byte of the boot
  ROM and the PC never appeared in the listing (issue #76).
  `test_disassembler.cpp` pins that case.
- **The trace's rows are formatted in the core** (`_formatTraceRange`), which
  is one round trip for the visible window instead of one heap read per row,
  and one operand formatter per processor rather than one per place that wants
  one. `_getTraceEntrySize` is asked for rather than assumed, because the entry
  grew when it had to hold a 65816's registers.
- **An instruction's cost is the core's own arithmetic.**
  `MachineHost::cycleCost` starts from `CPU6502::baseCycles` or
  `CPU65816::baseCycles` (the tables the CPUs charge from) and adds what each
  core adds where it adds it; at the PC the registers settle page crossings
  and branches, elsewhere they are a range. `test_machine_host` checks every
  opcode's cost at the PC against the cycles the core then charged, on both
  processors. Change how a CPU charges a cycle and that test says so.
- **What only covers part of a IIgs says so.** The heat map tracks the Mega
  II's MMU — the side where the video, the firmware's workspace and Applesoft
  live — and its titles name the banks and note that fast RAM is not covered,
  rather than letting a sparse map read as an idle machine. The zero page watch
  shows the direct page register and marks it when it has moved, because its
  addresses are absolute bank-zero ones.
