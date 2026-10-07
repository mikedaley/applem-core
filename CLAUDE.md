# CLAUDE.md

Guidance for Claude Code in this repository: the emulation core shared by
ApplEm's two front ends, [web-a2e](https://github.com/mikedaley/web-a2e) (the
browser) and [applem](https://github.com/mikedaley/applem) (the native macOS
app). Each includes this repository as a submodule at `core/`, so a change
here reaches both once they update the submodule. The design detail behind
each rule lives in `docs/design/` (index at the end). Read the relevant
design doc before changing a subsystem.

## Building and testing

```bash
mkdir -p build && cd build
cmake .. && make -j 4 && ctest
```

Keep build parallelism at `-j 4`. Built on its own the core builds the
`a2e_core` library and its Catch2 tests (`tests/unit/`,
`tests/integration/`, `tests/conformance/`, helpers in `tests/common/`).
Some tests skip unless an external fixture is pointed at:
`A2E_65816_VECTORS` (SingleStepTests 65816 vectors, 3GB) and
`A2E_BANDITS_WOZ` (the Bandits flux disk). `scripts/check-core-purity.sh`
checks there are no host-platform dependencies in `src/core/` or
`src/host/`.

A front end includes the core with `add_subdirectory(core)` and links
`a2e_core`. Tests are then off, and under Emscripten no optimisation flags
are added, as the browser build always compiled the core.

### ROMs

ROMs are embedded into the library at compile time from `roms/`
(`scripts/generate_roms.sh`):

- `342-0349-B-C0-FF.bin` (16KB //e system ROM)
- `342-0273-A-US-UK.bin` (4KB character ROM, US/UK), `341-0160-A-US-UK.bin` (alternate)
- `341-0027.bin` (256 bytes Disk II ROM)
- `Thunderclock Plus ROM.bin`, `Apple Mouse Interface Card ROM - 342-0270-C.bin`,
  `Apple Parallel Interface Card ROM - 341-0057.bin`
- `342-0077-B.bin`, the IIgs ROM 01 (128KB)

**II Plus ROMs are optional**: `341-0011.bin` to `341-0015.bin` plus
`341-0020.bin`, or one 12KB `apple2plus.rom`, and `341-0036.bin` for the
character generator. Without them the II+ is listed but reports itself
unrunnable (`Emulator::isMachineRunnable`).

## Layout

```
src/core/      C++ emulation, namespace a2e::, no host dependencies
  cpu/6502/      cycle-accurate 6502/65C02 (models which cycle touches which address)
  cpu/65816/     the IIgs CPU, separate class (cycle counts only)
  mmu/           128KB map, soft switches, slots, video scanner / floating bus
  video/         signal stage (dot stream) + decode stage (ntsc.cpp)
  audio/         speaker
  disk-image/    DSK/DO/PO/NIB/WOZ, gcr_encoding, gcr35, disk_converter, disk_inspection
  disassembler/  6502 and 65816 disassemblers
  assembler/     Merlin-compatible 65C02 assembler
  input/         keyboard, Sirius Joyport, //c IOU mouse
  iigs/          everything only a IIgs has (memory, video, ADB, clock, Ensoniq, SCC, machine)
  machine/       machine_profile.hpp: per-machine data and the registry
  cards/         ExpansionCard implementations, disk_controller shared by Disk II and IWM
  filesystem/    DOS 3.3, ProDOS, Pascal readers; DOS 3.3/ProDOS writers
  basic/         Applesoft/Integer tokenizer, detokenizer, variables
  debug/         MachineDebug, profiler, condition evaluator, debug_log sink
  emulator.*     8-bit machine coordinator (+ emulator/ state and debug files)
src/host/      machine_host.*: which of Emulator and IIgsMachine is alive, and
               what every host asks of a machine. Logic both front ends need
               goes here, never into one front end.
```

## Rules

- **The core is host-free.** No console, no platform calls in `src/core/`;
  logging goes through `debug_log`, file access through callbacks the host
  installs.
- **A number or a flag goes in the machine profile; a different mechanism
  goes in a different class the profile names.** No virtual calls or
  per-machine branches on per-cycle paths. The IIgs is its own family built
  from `core/iigs/`; nothing outside it grows an `if (IIgs)`. See
  `docs/design/machines.md`.
- **Share a mechanism, not a resemblance**: the //c's IWM and the Disk II
  share `DiskController`; the //c's serial ports compose the SSC's
  `ACIA6551` rather than inheriting from a card.
- **Host preferences are not machine state.** Speed multiplier, game port
  device, video standard, Mockingboard phase lock and mono, paste buffer:
  none is in a save state, and `reset()` keeps them.
- **A peek must never read a card** (`peekROM`, not `readROM`): a
  SmartPort's entry points are traps.
- **Hardware behaviour is settled by the hardware's documentation**:
  Apple's manuals and Technical Notes, schematics, datasheets. Another
  emulator can point at a difference, never settle it. Cite the source in
  the code comment and the design doc.
- **Video**: the 8-bit machines emit a 1-bit dot stream that `ntsc.cpp`
  decodes; colour is made by the receiver. Read `docs/design/display.md`
  before touching `video.cpp` or `ntsc.*`.
- **Expansion cards** implement `ExpansionCard`
  (`src/core/cards/expansion_card.hpp`) and get the profile through
  `setMachine()`. A card that can hold IRQ must be in the `Emulator`'s IRQ
  predicate to re-interrupt a handler. See `docs/design/cards.md`.

When a change alters something a design doc describes, update that doc in
the same change. A change to the core's interface needs the front ends
updated to match before their submodule moves on.

## Design docs

| Doc | Covers |
| --- | ------ |
| `docs/design/machines.md` | Machine profiles, II Plus, //c, NTSC/PAL, choosing a machine, adding a machine |
| `docs/design/iigs.md` | 65816, IIgs memory and shadowing, clocks, video and border, ADB, clock chip, Ensoniq, SCC, slots, SmartPort, 3.5" drives |
| `docs/design/cards.md` | Interrupts, slot map, card interface, Mockingboard, printers |
| `docs/design/disks.md` | WOZ flux tracks, Disk Inspector |
| `docs/design/display.md` | Composite video and colour decoding, CRT shader, display settings |
| `docs/design/input.md` | Paste buffer, AKD, game port and Joyport, CPU speed, keyboard mapping |
| `docs/design/state.md` | Save state formats for both families, autosave |
| `docs/design/debugging.md` | MachineDebug, memory spaces, breakpoints, disassembly |
| `docs/design/assembler.md` | Merlin assembler semantics |
