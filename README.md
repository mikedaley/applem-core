# ApplEm core

The emulation core shared by ApplEm's two front ends:

- [web-a2e](https://github.com/mikedaley/web-a2e), the browser emulator
  (WebAssembly and WebGL)
- [applem](https://github.com/mikedaley/applem), the native macOS app (Dear
  ImGui over Metal)

It models four machines: the Apple IIe Enhanced, the Apple II Plus, the Apple
IIc and the Apple IIgs, cycle by cycle, in C++20 with no host dependencies.
Each front end includes this repository as a git submodule at `core/`.

## Layout

```
src/core/      the emulation, namespace a2e::, no host dependencies
  cpu/6502/      cycle-accurate 6502/65C02
  cpu/65816/     the IIgs CPU
  mmu/           128KB map, soft switches, slots, video scanner, floating bus
  video/         signal stage (dot stream) and decode stage (ntsc.cpp)
  audio/         speaker
  disk-image/    DSK/DO/PO/NIB/WOZ, GCR, 3.5" disks, conversion, inspection
  disassembler/  6502 and 65816
  assembler/     Merlin-compatible 65C02 assembler
  input/         keyboard, Sirius Joyport, the //c's IOU mouse
  iigs/          everything only a IIgs has
  machine/       machine profiles and the registry
  cards/         expansion cards
  filesystem/    DOS 3.3, ProDOS and Pascal
  basic/         Applesoft and Integer BASIC tokenizer, detokenizer, variables
  debug/         MachineDebug, the profiler, the condition evaluator, logging
src/host/      MachineHost: which machine is alive, and what every host asks of it
roms/          ROM images, embedded at build time
tests/         Catch2 tests: unit, integration, conformance
docs/design/   design notes, one per subsystem
scripts/       ROM embedding and the purity check
```

## Building and testing

```bash
mkdir -p build && cd build
cmake .. && make -j 4 && ctest
```

Built on its own, the core builds the `a2e_core` library and its tests. A few
tests skip unless a large external fixture is pointed at:
`A2E_65816_VECTORS` (the SingleStepTests 65816 vectors, 3GB) and
`A2E_BANDITS_WOZ` (the Bandits flux disk).

`scripts/check-core-purity.sh` checks that nothing in `src/core` or
`src/host` depends on a host platform.

## Using it from a front end

```cmake
add_subdirectory(core)
target_link_libraries(my_front_end PRIVATE a2e_core)
```

As a subdirectory the tests are off (`A2E_CORE_TESTS`), and under Emscripten
the library is compiled with no optimisation flags of its own, as the browser
build always has been. The ROMs in `roms/` are embedded into the library;
the II Plus ROMs are optional (see `CMakeLists.txt`).

## License

MIT License. See [LICENSE](LICENSE).
