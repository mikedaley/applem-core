cmake_minimum_required(VERSION 3.20)

# The oldest macOS the native builds run on. Without it the compiler targets
# whatever macOS builds them, and an app built on macOS 27 will not start on
# anything older. It is set before project(), so the core library is built
# for the same macOS as the app it is linked into, and set outright rather
# than defaulted, so a build directory that cached another value follows a
# change here. The WebAssembly build, which comes through Emscripten's
# toolchain, is left alone. As a subdirectory of a front end, the front end
# sets it.
set(A2E_MACOS_MINIMUM "15.0")
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND
   CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_TOOLCHAIN_FILE MATCHES "Emscripten")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "${A2E_MACOS_MINIMUM}" CACHE STRING "Oldest macOS the native builds run on" FORCE)
endif()

project(applem-core VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Generate compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Source files
set(CORE_SOURCES
    src/core/emulator.cpp
    src/core/debug/machine_debug.cpp
    src/core/debug/soft_switch_catalog.cpp
    src/core/emulator/emulator_state.cpp
    src/core/emulator/emulator_debug.cpp
    src/core/cpu/6502/cpu6502.cpp
    src/core/mmu/mmu.cpp
    src/core/video/video.cpp
    src/core/video/ntsc.cpp
    src/core/audio/audio.cpp
    src/core/disk-image/dsk_disk_image.cpp
    src/core/disk-image/woz_disk_image.cpp
    src/core/disk-image/disk_inspection.cpp
    src/core/disk-image/disk_converter.cpp
    src/core/disk-image/gcr_encoding.cpp
    src/core/input/joyport.cpp
    src/core/input/mouse_iou.cpp
    src/core/input/keyboard.cpp
    src/core/disassembler/disassembler.cpp
    src/core/disassembler/disassembler65816.cpp
    src/core/cards/mockingboard/ay8910.cpp
    src/core/debug/debug_log.cpp
    src/core/cards/mockingboard/via6522.cpp
    src/core/cards/mockingboard/mockingboard_card.cpp
    src/core/iigs/iigs_memory.cpp
    src/core/iigs/iigs_clock.cpp
    src/core/iigs/iigs_machine.cpp
    src/core/iigs/iigs_state.cpp
    src/core/disassembler/disassembler65816.cpp
    src/core/iigs/iigs_sound.cpp
    src/core/iigs/iigs_video.cpp
    src/core/iigs/iigs_adb.cpp
    src/core/iigs/iigs_scc.cpp
    src/core/cpu/65816/cpu65816.cpp
    src/core/cpu/65816/cpu65816_dispatch.cpp
    src/core/cards/disk_controller.cpp
    src/core/cards/disk2/disk2_card.cpp
    src/core/cards/iwm/iwm.cpp
    src/core/cards/iwm/sony_drive.cpp
    src/core/disk-image/gcr35.cpp
    src/core/cards/thunderclock/thunderclock_card.cpp
    src/core/cards/mouse/mouse_card.cpp
    src/core/cards/smartport/block_device.cpp
    src/core/cards/smartport/smartport_card.cpp
    src/core/cards/softcard/softcard_z80.cpp
    src/core/cards/ssc/acia6551.cpp
    src/core/cards/serial/serial_port.cpp
    src/core/cards/ssc/ssc_card.cpp
    src/core/cards/parallel/parallel_card.cpp
    src/core/cards/softcard/z80/z80.cpp
    src/core/cards/softcard/z80/z80_opcode_tables.cpp
    src/core/cards/softcard/z80/z80_opcodes_main.cpp
    src/core/cards/softcard/z80/z80_opcodes_cb.cpp
    src/core/cards/softcard/z80/z80_opcodes_dd.cpp
    src/core/cards/softcard/z80/z80_opcodes_ed.cpp
    src/core/cards/softcard/z80/z80_opcodes_fd.cpp
    src/core/cards/softcard/z80/z80_opcodes_ddcb.cpp
    src/core/cards/softcard/z80/z80_opcodes_fdcb.cpp
    src/core/noslot_clock.cpp
    src/core/basic/applesoft_vars.cpp
    src/core/debug/condition_evaluator.cpp
    src/core/filesystem/dos33.cpp
    src/core/filesystem/prodos.cpp
    src/core/filesystem/pascal.cpp
    src/core/basic/basic_detokenizer.cpp
    src/core/basic/basic_tokenizer.cpp
    src/core/assembler/assembler.cpp
)

# What every host asks of the running machine, whichever kind it is. Shared by
# the WebAssembly bindings and the native front end, so neither decides alone
# how a question reaches a //e or a IIgs.
set(HOST_SOURCES
    src/host/machine_host.cpp
    src/host/machine_host_debug.cpp
)

# ROM embedding - generate C arrays from ROM files
set(ROM_FILES
    roms/342-0349-B-C0-FF.bin                       # Combined 16KB system ROM (C0-FF)
    roms/342-0273-A-US-UK.bin                       # Character ROM (Apple IIe Enhanced)
    roms/341-0027.bin                               # Disk II ROM
    "roms/Thunderclock Plus ROM.bin"                 # Thunderclock card ROM
    "roms/Apple Mouse Interface Card ROM - 342-0270-C.bin"  # Mouse Interface Card ROM
    "roms/Super Serial Card ROM - 341-0065-A.bin"           # Super Serial Card ROM
    "roms/Apple Parallel Interface Card ROM - 341-0057.bin" # Parallel Interface Card PROM (341-0005/341-0019)
)

# Apple II Plus ROMs are optional — the generator emits empty arrays when they
# are missing, and the II+ profile then reports having no ROM. Listed separately
# so a missing file is not a broken dependency for the //e build.
set(ROM_FILES_OPTIONAL_CANDIDATES
    roms/341-0011.bin   # Applesoft $D000-$D7FF
    roms/341-0012.bin   # Applesoft $D800-$DFFF
    roms/341-0013.bin   # Applesoft $E000-$E7FF
    roms/341-0014.bin   # Applesoft $E800-$EFFF
    roms/341-0015.bin   # Applesoft $F000-$F7FF
    roms/341-0020.bin   # Autostart monitor $F800-$FFFF
    roms/apple2plus.rom # ...or one combined 12KB image instead of the six
    roms/341-0036.bin   # II+ character generator
)

# Only the candidates that are actually present become build dependencies. A
# DEPENDS entry with no rule to build it is a hard error, and these are
# alternatives — supplying the six motherboard ROMs means apple2plus.rom will
# never exist, and vice versa. Re-run cmake after adding ROMs.
set(ROM_FILES_OPTIONAL "")
foreach(candidate ${ROM_FILES_OPTIONAL_CANDIDATES})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${candidate})
        list(APPEND ROM_FILES_OPTIONAL ${candidate})
    endif()
endforeach()

# Custom command to generate ROM header
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/roms.cpp
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_roms.sh ${CMAKE_CURRENT_SOURCE_DIR}/roms ${CMAKE_CURRENT_BINARY_DIR}/generated/roms.cpp
    # ROM_FILES_OPTIONAL must be listed too. Without it, dropping the II+ ROMs
    # into roms/ changed nothing the build was watching, so roms.cpp stayed as
    # it was and the machine reported having no ROM long after its ROM existed.
    DEPENDS ${ROM_FILES} ${ROM_FILES_OPTIONAL}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_roms.sh
    COMMENT "Generating ROM data arrays"
)

# ============================================================================
# The library
# ============================================================================

# All core include directories
set(ALL_CORE_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/cpu/6502
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/cpu/65816
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/machine
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/iigs
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/mmu
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/video
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/input
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/cards
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/cards/mockingboard
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/cards/disk2
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/cards/iwm
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/cards/serial
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/cards/ssc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/cards/thunderclock
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/cards/mouse
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/cards/softcard
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/cards/softcard/z80
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/cards/smartport
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/disk-image
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/disassembler
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/debug
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/filesystem
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/basic
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/assembler
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# roms.cpp is generated before anything that includes it is compiled.
add_custom_target(generate_roms_native DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/generated/roms.cpp)

# The core and the host layer as one library, which each front end links: the
# browser's WebAssembly module and the native app. The tests below still
# compile the core into each binary themselves.
add_library(a2e_core STATIC ${CORE_SOURCES} ${HOST_SOURCES})
add_dependencies(a2e_core generate_roms_native)
target_include_directories(a2e_core PUBLIC ${ALL_CORE_INCLUDES})
# And src/ itself, so a front end names a header by where it lives:
# "core/emulator.hpp", "host/machine_host.hpp".
target_include_directories(a2e_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(a2e_core PUBLIC cxx_std_20)
# The browser build has always compiled the core with no optimisation flags of
# its own, its link step carrying -O3; that is kept as it was.
if(NOT EMSCRIPTEN)
    target_compile_options(a2e_core PRIVATE -O2 -Wall -Wextra)
endif()

# ============================================================================
# Tests: built when the core is the top-level project, not when a front end
# includes it as a subdirectory, and never for WebAssembly.
# ============================================================================

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(A2E_CORE_TESTS_DEFAULT ON)
else()
    set(A2E_CORE_TESTS_DEFAULT OFF)
endif()
option(A2E_CORE_TESTS "Build the core's tests" ${A2E_CORE_TESTS_DEFAULT})

if(A2E_CORE_TESTS AND NOT EMSCRIPTEN)
    enable_testing()

    # ==========================================================================
    # Common paths and source lists
    # ==========================================================================

    set(CATCH2_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/tests/catch2)
    set(TEST_COMMON_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/tests/common)


    # All core source files (excluding wasm_interface.cpp)
    set(CORE_TEST_SOURCES
        src/core/emulator.cpp
        src/core/debug/machine_debug.cpp
        src/core/emulator/emulator_state.cpp
        src/core/emulator/emulator_debug.cpp
        src/core/cpu/6502/cpu6502.cpp
        src/core/mmu/mmu.cpp
        src/core/video/video.cpp
        src/core/video/ntsc.cpp
        src/core/audio/audio.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/disk_inspection.cpp
        src/core/disk-image/disk_converter.cpp
        src/core/disk-image/gcr_encoding.cpp
        src/core/input/joyport.cpp
        src/core/input/mouse_iou.cpp
        src/core/input/keyboard.cpp
        src/core/disassembler/disassembler.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/debug/debug_log.cpp
        src/core/cards/mockingboard/via6522.cpp
        src/core/cards/mockingboard/mockingboard_card.cpp
        src/core/iigs/iigs_memory.cpp
        src/core/iigs/iigs_clock.cpp
        src/core/iigs/iigs_machine.cpp
        src/core/iigs/iigs_state.cpp
        src/core/disassembler/disassembler65816.cpp
        src/core/audio/audio.cpp
        src/core/disk-image/disk_converter.cpp
        src/core/cards/mockingboard/mockingboard_card.cpp
        src/core/cards/mockingboard/via6522.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/debug/debug_log.cpp
        src/core/cards/smartport/smartport_card.cpp
        src/core/cards/smartport/block_device.cpp
        src/core/cards/disk_controller.cpp
        src/core/cards/iwm/iwm.cpp
        src/core/cards/iwm/sony_drive.cpp
        src/core/disk-image/gcr35.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
        src/core/iigs/iigs_sound.cpp
        src/core/iigs/iigs_video.cpp
        src/core/iigs/iigs_adb.cpp
        src/core/iigs/iigs_scc.cpp
        src/core/cpu/65816/cpu65816.cpp
        src/core/cpu/65816/cpu65816_dispatch.cpp
        src/core/cards/disk_controller.cpp
        src/core/cards/disk2/disk2_card.cpp
        src/core/cards/iwm/iwm.cpp
        src/core/cards/iwm/sony_drive.cpp
        src/core/disk-image/gcr35.cpp
        src/core/cards/thunderclock/thunderclock_card.cpp
        src/core/cards/mouse/mouse_card.cpp
        src/core/cards/smartport/block_device.cpp
        src/core/cards/smartport/smartport_card.cpp
        src/core/cards/softcard/softcard_z80.cpp
        src/core/cards/ssc/acia6551.cpp
        src/core/cards/serial/serial_port.cpp
        src/core/cards/ssc/ssc_card.cpp
        src/core/cards/parallel/parallel_card.cpp
        src/core/cards/softcard/z80/z80.cpp
        src/core/cards/softcard/z80/z80_opcode_tables.cpp
        src/core/cards/softcard/z80/z80_opcodes_main.cpp
        src/core/cards/softcard/z80/z80_opcodes_cb.cpp
        src/core/cards/softcard/z80/z80_opcodes_dd.cpp
        src/core/cards/softcard/z80/z80_opcodes_ed.cpp
        src/core/cards/softcard/z80/z80_opcodes_fd.cpp
        src/core/cards/softcard/z80/z80_opcodes_ddcb.cpp
        src/core/cards/softcard/z80/z80_opcodes_fdcb.cpp
        src/core/noslot_clock.cpp
        src/core/basic/applesoft_vars.cpp
        src/core/debug/condition_evaluator.cpp
        src/core/filesystem/dos33.cpp
        src/core/filesystem/prodos.cpp
        src/core/filesystem/pascal.cpp
        src/core/basic/basic_detokenizer.cpp
        src/core/basic/basic_tokenizer.cpp
        src/core/assembler/assembler.cpp
    )

    # Common test helper sources
    set(TEST_COMMON_SOURCES
        tests/common/disk_image_builder.cpp
        tests/common/basic_program_builder.cpp
    )


    # ==========================================================================
    # Helper function: add a Catch2 test with specified sources
    # ==========================================================================
    function(add_catch2_test TEST_NAME TEST_SOURCE)
        # Remaining arguments are extra source files
        set(EXTRA_SOURCES ${ARGN})

        add_executable(${TEST_NAME}
            ${TEST_SOURCE}
            ${EXTRA_SOURCES}
        )
        add_dependencies(${TEST_NAME} generate_roms_native)
        target_include_directories(${TEST_NAME} PRIVATE
            ${ALL_CORE_INCLUDES}
            ${CATCH2_INCLUDE}
            ${TEST_COMMON_INCLUDE}
        )
        target_compile_options(${TEST_NAME} PRIVATE -O2 -Wall -Wextra)
        add_test(NAME ${TEST_NAME}
            COMMAND ${TEST_NAME}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
    endfunction()

    # ==========================================================================
    # Catch2 Unit Tests - Pure (no ROM dependencies)
    # ==========================================================================

    # Super Hi-Res, drawn and then looked at.
    add_catch2_test(test_iigs_video
        tests/unit/test_iigs_video.cpp
        src/core/iigs/iigs_machine.cpp
        src/core/cards/mouse/mouse_card.cpp
        src/core/cards/parallel/parallel_card.cpp
        src/core/cards/ssc/ssc_card.cpp
        src/core/cards/ssc/acia6551.cpp
        src/core/cards/thunderclock/thunderclock_card.cpp
        src/core/iigs/iigs_state.cpp
        src/core/disassembler/disassembler65816.cpp
        src/core/debug/machine_debug.cpp
        src/core/disassembler/disassembler65816.cpp
        src/core/audio/audio.cpp
        src/core/disk-image/disk_converter.cpp
        src/core/cards/mockingboard/mockingboard_card.cpp
        src/core/cards/mockingboard/via6522.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/debug/debug_log.cpp
        src/core/cards/smartport/smartport_card.cpp
        src/core/cards/smartport/block_device.cpp
        src/core/cards/disk_controller.cpp
        src/core/cards/iwm/iwm.cpp
        src/core/cards/iwm/sony_drive.cpp
        src/core/disk-image/disk_inspection.cpp
        src/core/disk-image/gcr35.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
        src/core/iigs/iigs_memory.cpp
        src/core/iigs/iigs_clock.cpp
        src/core/input/keyboard.cpp
        src/core/input/joyport.cpp
        src/core/iigs/iigs_video.cpp
        src/core/iigs/iigs_sound.cpp
        src/core/iigs/iigs_adb.cpp
        src/core/iigs/iigs_scc.cpp
        src/core/cpu/65816/cpu65816.cpp
        src/core/cpu/65816/cpu65816_dispatch.cpp
        src/core/mmu/mmu.cpp
        src/core/video/video.cpp
        src/core/video/ntsc.cpp
        src/core/input/mouse_iou.cpp
        src/core/noslot_clock.cpp
    )

    # The two devices a IIgs will not boot without.
    # Debugging a IIgs: breakpoints with banks, watchpoints on the processor's
    # own bus, a step over that knows a long call, a trace entry twice as wide.
    add_catch2_test(test_iigs_debug
        tests/unit/test_iigs_debug.cpp
        src/core/iigs/iigs_machine.cpp
        src/core/cards/mouse/mouse_card.cpp
        src/core/cards/parallel/parallel_card.cpp
        src/core/cards/ssc/ssc_card.cpp
        src/core/cards/ssc/acia6551.cpp
        src/core/cards/thunderclock/thunderclock_card.cpp
        src/core/iigs/iigs_state.cpp
        src/core/disassembler/disassembler65816.cpp
        src/core/debug/machine_debug.cpp
        src/core/iigs/iigs_memory.cpp
        src/core/iigs/iigs_clock.cpp
        src/core/iigs/iigs_scc.cpp
        src/core/iigs/iigs_sound.cpp
        src/core/iigs/iigs_video.cpp
        src/core/iigs/iigs_adb.cpp
        src/core/cpu/65816/cpu65816.cpp
        src/core/cpu/65816/cpu65816_dispatch.cpp
        src/core/mmu/mmu.cpp
        src/core/video/video.cpp
        src/core/video/ntsc.cpp
        src/core/audio/audio.cpp
        src/core/input/keyboard.cpp
        src/core/input/joyport.cpp
        src/core/input/mouse_iou.cpp
        src/core/noslot_clock.cpp
        src/core/debug/debug_log.cpp
        src/core/cards/disk_controller.cpp
        src/core/cards/iwm/iwm.cpp
        src/core/cards/iwm/sony_drive.cpp
        src/core/disk-image/disk_inspection.cpp
        src/core/disk-image/gcr35.cpp
        src/core/cards/smartport/smartport_card.cpp
        src/core/cards/smartport/block_device.cpp
        src/core/cards/mockingboard/mockingboard_card.cpp
        src/core/cards/mockingboard/via6522.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
        src/core/disk-image/disk_converter.cpp
    )

    add_catch2_test(test_iigs_devices
        tests/unit/test_iigs_devices.cpp
        src/core/iigs/iigs_adb.cpp
        src/core/iigs/iigs_scc.cpp
        src/core/iigs/iigs_sound.cpp
        src/core/iigs/iigs_clock.cpp
    )

    # A IIgs running its own ROM, which is every part of it at once.
    add_catch2_test(test_iigs_boot
        tests/integration/test_iigs_boot.cpp
        src/core/iigs/iigs_machine.cpp
        src/core/cards/mouse/mouse_card.cpp
        src/core/cards/parallel/parallel_card.cpp
        src/core/cards/ssc/ssc_card.cpp
        src/core/cards/ssc/acia6551.cpp
        src/core/cards/thunderclock/thunderclock_card.cpp
        src/core/iigs/iigs_state.cpp
        src/core/disassembler/disassembler65816.cpp
        src/core/debug/machine_debug.cpp
        src/core/disassembler/disassembler65816.cpp
        src/core/audio/audio.cpp
        src/core/disk-image/disk_converter.cpp
        src/core/cards/mockingboard/mockingboard_card.cpp
        src/core/cards/mockingboard/via6522.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/debug/debug_log.cpp
        src/core/cards/smartport/smartport_card.cpp
        src/core/cards/smartport/block_device.cpp
        src/core/cards/disk_controller.cpp
        src/core/cards/iwm/iwm.cpp
        src/core/cards/iwm/sony_drive.cpp
        src/core/disk-image/disk_inspection.cpp
        src/core/disk-image/gcr35.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
        src/core/iigs/iigs_memory.cpp
        src/core/iigs/iigs_clock.cpp
        src/core/input/keyboard.cpp
        src/core/input/joyport.cpp
        src/core/iigs/iigs_sound.cpp
        src/core/iigs/iigs_video.cpp
        src/core/iigs/iigs_adb.cpp
        src/core/iigs/iigs_scc.cpp
        src/core/cpu/65816/cpu65816.cpp
        src/core/cpu/65816/cpu65816_dispatch.cpp
        src/core/mmu/mmu.cpp
        src/core/video/video.cpp
        src/core/video/ntsc.cpp
        src/core/input/mouse_iou.cpp
        src/core/noslot_clock.cpp
    )

    # A IIgs saved and restored: the processor, both kinds of RAM, the devices.
    add_catch2_test(test_iigs_state
        tests/integration/test_iigs_state.cpp
        src/core/iigs/iigs_machine.cpp
        src/core/cards/mouse/mouse_card.cpp
        src/core/cards/parallel/parallel_card.cpp
        src/core/cards/ssc/ssc_card.cpp
        src/core/cards/ssc/acia6551.cpp
        src/core/cards/thunderclock/thunderclock_card.cpp
        src/core/iigs/iigs_state.cpp
        src/core/disassembler/disassembler65816.cpp
        src/core/debug/machine_debug.cpp
        src/core/disassembler/disassembler65816.cpp
        src/core/audio/audio.cpp
        src/core/disk-image/disk_converter.cpp
        src/core/cards/mockingboard/mockingboard_card.cpp
        src/core/cards/mockingboard/via6522.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/debug/debug_log.cpp
        src/core/cards/smartport/smartport_card.cpp
        src/core/cards/smartport/block_device.cpp
        src/core/cards/disk_controller.cpp
        src/core/cards/iwm/iwm.cpp
        src/core/cards/iwm/sony_drive.cpp
        src/core/disk-image/disk_inspection.cpp
        src/core/disk-image/gcr35.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
        src/core/iigs/iigs_memory.cpp
        src/core/iigs/iigs_clock.cpp
        src/core/input/keyboard.cpp
        src/core/input/joyport.cpp
        src/core/iigs/iigs_sound.cpp
        src/core/iigs/iigs_video.cpp
        src/core/iigs/iigs_adb.cpp
        src/core/iigs/iigs_scc.cpp
        src/core/cpu/65816/cpu65816.cpp
        src/core/cpu/65816/cpu65816_dispatch.cpp
        src/core/mmu/mmu.cpp
        src/core/video/video.cpp
        src/core/video/ntsc.cpp
        src/core/input/mouse_iou.cpp
        src/core/noslot_clock.cpp
    )

    # A IIgs's address space, which needs no CPU to test: banks, shadowing,
    # and the Mega II underneath it.
    add_catch2_test(test_iigs_memory
        tests/unit/test_iigs_memory.cpp
        src/core/iigs/iigs_memory.cpp
        src/core/iigs/iigs_clock.cpp
        src/core/iigs/iigs_sound.cpp
        src/core/iigs/iigs_adb.cpp
        src/core/iigs/iigs_scc.cpp
        src/core/mmu/mmu.cpp
        src/core/input/mouse_iou.cpp
        src/core/noslot_clock.cpp
    )

    # The 65816 against 5.1 million recorded states from a real one. Skips
    # unless A2E_65816_VECTORS points at the vector files, which are 3GB and
    # are not in the repository.
    add_catch2_test(test_65816_vectors
        tests/conformance/test_65816_vectors.cpp
        src/core/cpu/65816/cpu65816.cpp
        src/core/cpu/65816/cpu65816_dispatch.cpp
    )

    # The 65816, which needs nothing but itself: no machine, no MMU, no IIgs.
    add_catch2_test(test_cpu65816
        tests/unit/test_cpu65816.cpp
        src/core/cpu/65816/cpu65816.cpp
        src/core/cpu/65816/cpu65816_dispatch.cpp
    )

    # CPU tests (only need cpu6502.cpp)
    add_catch2_test(test_cpu6502
        tests/unit/test_cpu6502.cpp
        src/core/cpu/6502/cpu6502.cpp
    )
    add_catch2_test(test_cpu_addressing
        tests/unit/test_cpu_addressing.cpp
        src/core/cpu/6502/cpu6502.cpp
    )
    add_catch2_test(test_cpu_interrupts
        tests/unit/test_cpu_interrupts.cpp
        src/core/cpu/6502/cpu6502.cpp
    )
    add_catch2_test(test_cpu_decimal
        tests/unit/test_cpu_decimal.cpp
        src/core/cpu/6502/cpu6502.cpp
    )
    add_catch2_test(test_cpu_cycle_counts
        tests/unit/test_cpu_cycle_counts.cpp
        src/core/cpu/6502/cpu6502.cpp
    )

    # GCR encoding (only gcr_encoding.cpp)
    add_catch2_test(test_gcr_encoding
        tests/unit/test_gcr_encoding.cpp
        src/core/disk-image/gcr_encoding.cpp
    )

    # Disassembler (only disassembler.cpp)
    add_catch2_test(test_disassembler
        tests/unit/test_disassembler.cpp
        src/core/disassembler/disassembler.cpp
    )

    # The 65816 disassembler, checked against the 65816 itself.
    add_catch2_test(test_disassembler65816
        tests/unit/test_disassembler65816.cpp
        src/core/disassembler/disassembler65816.cpp
        src/core/cpu/65816/cpu65816.cpp
        src/core/cpu/65816/cpu65816_dispatch.cpp
    )

    # Assembler (only assembler.cpp + disassembler for opcode table)
    add_catch2_test(test_assembler
        tests/unit/test_assembler.cpp
        src/core/assembler/assembler.cpp
        src/core/disassembler/disassembler.cpp
    )

    # Audio (audio.cpp + mockingboard deps for consumeStereoSamples)
    add_catch2_test(test_audio
        tests/unit/test_audio.cpp
        src/core/audio/audio.cpp
        src/core/cards/mockingboard/mockingboard_card.cpp
        src/core/cards/mockingboard/via6522.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/debug/debug_log.cpp
    )

    # The limiter that keeps a mix under full scale (header only)
    add_catch2_test(test_peak_limiter
        tests/unit/test_peak_limiter.cpp
    )

    # Keyboard (only keyboard.cpp)
    add_catch2_test(test_keyboard
        tests/unit/test_keyboard.cpp
        src/core/input/keyboard.cpp
    )

    # A //c's IOU mouse (only mouse_iou.cpp)
    add_catch2_test(test_mouse_iou
        tests/unit/test_mouse_iou.cpp
        src/core/input/mouse_iou.cpp
    )

    # Sirius Joyport (only joyport.cpp)
    add_catch2_test(test_joyport
        tests/unit/test_joyport.cpp
        src/core/input/joyport.cpp
        src/core/input/mouse_iou.cpp
    )

    # AY-3-8910 (only ay8910.cpp)
    add_catch2_test(test_ay8910
        tests/unit/test_ay8910.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/debug/debug_log.cpp
    )

    # VIA 6522 (via6522.cpp + ay8910.cpp since VIA connects to PSG)
    add_catch2_test(test_via6522
        tests/unit/test_via6522.cpp
        src/core/cards/mockingboard/via6522.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/debug/debug_log.cpp
    )

    # Block device (only block_device.cpp)
    add_catch2_test(test_block_device
        tests/unit/test_block_device.cpp
        src/core/cards/smartport/block_device.cpp
    )

    # BASIC detokenizer (only basic_detokenizer.cpp + common helpers)
    add_catch2_test(test_basic_detokenizer
        tests/unit/test_basic_detokenizer.cpp
        src/core/basic/basic_detokenizer.cpp
        ${TEST_COMMON_SOURCES}
    )

    # BASIC tokenizer (basic_tokenizer.cpp + basic_detokenizer.cpp for round-trip)
    add_catch2_test(test_basic_tokenizer
        tests/unit/test_basic_tokenizer.cpp
        src/core/basic/basic_tokenizer.cpp
        src/core/basic/basic_detokenizer.cpp
        ${TEST_COMMON_SOURCES}
    )

    # ==========================================================================
    # Catch2 Unit Tests - ROM-dependent
    # ==========================================================================

    # MMU (mmu.cpp + noslot_clock.cpp)
    add_catch2_test(test_mmu
        tests/unit/test_mmu.cpp
        src/core/mmu/mmu.cpp
        src/core/input/mouse_iou.cpp
        src/core/noslot_clock.cpp
    )

    # MMU Slots (mmu.cpp + thunderclock for testing)
    add_catch2_test(test_mmu_slots
        tests/unit/test_mmu_slots.cpp
        src/core/mmu/mmu.cpp
        src/core/input/mouse_iou.cpp
        src/core/noslot_clock.cpp
        src/core/cards/thunderclock/thunderclock_card.cpp
    )

    # Machine profile. Links the whole core: the point of these tests is that a
    # profile reaches the subsystems that read it, including the Emulator that
    # selects the CPU and the ROMs.
    add_catch2_test(test_machine_profile
        tests/unit/test_machine_profile.cpp
        ${CORE_TEST_SOURCES}
        ${TEST_COMMON_SOURCES}
    )

    # NTSC composite decoding (standalone — no MMU needed)
    add_catch2_test(test_ntsc
        tests/unit/test_ntsc.cpp
        src/core/video/ntsc.cpp
    )

    # Video (video.cpp + mmu.cpp)
    add_catch2_test(test_video
        tests/unit/test_video.cpp
        src/core/video/video.cpp
        src/core/video/ntsc.cpp
        src/core/mmu/mmu.cpp
        src/core/input/mouse_iou.cpp
        src/core/noslot_clock.cpp
    )

    # Disk2 Card (disk2_card.cpp + disk images + gcr)
    add_catch2_test(test_disk2_card
        tests/unit/test_disk2_card.cpp
        src/core/cards/disk_controller.cpp
        src/core/cards/disk2/disk2_card.cpp
        src/core/cards/iwm/iwm.cpp
        src/core/cards/iwm/sony_drive.cpp
        src/core/disk-image/disk_inspection.cpp
        src/core/disk-image/gcr35.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
    )

    # A //c's serial ports — the SSC's ACIA with no card around it
    add_catch2_test(test_serial_port
        tests/unit/test_serial_port.cpp
        src/core/cards/serial/serial_port.cpp
        src/core/cards/ssc/acia6551.cpp
    )

    # IWM — a //c's drive controller, sharing DiskController with the card
    add_catch2_test(test_iwm
        tests/unit/test_iwm.cpp
        src/core/cards/disk_controller.cpp
        src/core/cards/disk2/disk2_card.cpp
        src/core/cards/iwm/iwm.cpp
        src/core/cards/iwm/sony_drive.cpp
        src/core/disk-image/disk_inspection.cpp
        src/core/disk-image/gcr35.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
    )

    # The Apple 3.5" drive, its recording, and the IWM's 3.5" port
    add_catch2_test(test_disk35
        tests/unit/test_disk35.cpp
        src/core/disk-image/disk_inspection.cpp
        src/core/cards/disk_controller.cpp
        src/core/cards/iwm/iwm.cpp
        src/core/cards/iwm/sony_drive.cpp
        src/core/disk-image/gcr35.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
    )

    # DSK disk image
    add_catch2_test(test_dsk_disk_image
        tests/unit/test_dsk_disk_image.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
    )

    # Disk Inspector's track analysis
    add_catch2_test(test_disk_inspection
        tests/unit/test_disk_inspection.cpp
        src/core/disk-image/disk_inspection.cpp
        src/core/disk-image/gcr35.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
    )

    # WOZ disk image
    add_catch2_test(test_woz_disk_image
        tests/unit/test_woz_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
    )

    # Disk image format conversion (DSK/DO/PO/WOZ)
    add_catch2_test(test_disk_converter
        tests/unit/test_disk_converter.cpp
        src/core/disk-image/disk_converter.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
        src/core/filesystem/dos33.cpp
        src/core/filesystem/prodos.cpp
        ${TEST_COMMON_SOURCES}
    )

    # Mockingboard (mockingboard_card.cpp + via + ay)
    add_catch2_test(test_mockingboard
        tests/unit/test_mockingboard.cpp
        src/core/cards/mockingboard/mockingboard_card.cpp
        src/core/cards/mockingboard/via6522.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/debug/debug_log.cpp
    )

    # Thunderclock (Catch2 version)
    add_catch2_test(test_thunderclock
        tests/unit/test_thunderclock.cpp
        src/core/cards/thunderclock/thunderclock_card.cpp
    )

    # Mouse Card (mouse_card.cpp)
    add_catch2_test(test_mouse_card
        tests/unit/test_mouse_card.cpp
        src/core/cards/mouse/mouse_card.cpp
    )

    # SmartPort Card (smartport + block_device)
    add_catch2_test(test_smartport_card
        tests/unit/test_smartport_card.cpp
        src/core/cards/smartport/smartport_card.cpp
        src/core/cards/smartport/block_device.cpp
    )

    # ==========================================================================
    # Catch2 Unit Tests - Filesystem
    # ==========================================================================

    # DOS 3.3 parser
    add_catch2_test(test_dos33
        tests/unit/test_dos33.cpp
        src/core/filesystem/dos33.cpp
        ${TEST_COMMON_SOURCES}
    )

    # ProDOS parser
    add_catch2_test(test_prodos
        tests/unit/test_prodos.cpp
        src/core/filesystem/prodos.cpp
        ${TEST_COMMON_SOURCES}
    )

    # Pascal parser
    add_catch2_test(test_pascal
        tests/unit/test_pascal.cpp
        src/core/filesystem/pascal.cpp
        ${TEST_COMMON_SOURCES}
    )

    # ==========================================================================
    # Catch2 Unit Tests - Expansion Card Interface
    # ==========================================================================

    add_catch2_test(test_expansion_card
        tests/unit/test_expansion_card.cpp
        src/core/cards/disk_controller.cpp
        src/core/cards/disk2/disk2_card.cpp
        src/core/cards/iwm/iwm.cpp
        src/core/cards/iwm/sony_drive.cpp
        src/core/disk-image/disk_inspection.cpp
        src/core/disk-image/gcr35.cpp
        src/core/cards/mockingboard/mockingboard_card.cpp
        src/core/cards/thunderclock/thunderclock_card.cpp
        src/core/cards/mouse/mouse_card.cpp
        src/core/cards/smartport/smartport_card.cpp
        src/core/cards/smartport/block_device.cpp
        src/core/cards/mockingboard/via6522.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/debug/debug_log.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
    )

    # ==========================================================================
    # Catch2 Integration Tests (full emulator)
    # ==========================================================================

    add_catch2_test(test_emulator
        tests/integration/test_emulator.cpp
        ${CORE_TEST_SOURCES}
        ${TEST_COMMON_SOURCES}
    )

    add_catch2_test(test_emulator_debug
        tests/integration/test_emulator_debug.cpp
        ${CORE_TEST_SOURCES}
        ${TEST_COMMON_SOURCES}
    )

    add_catch2_test(test_emulator_disk
        tests/integration/test_emulator_disk.cpp
        ${CORE_TEST_SOURCES}
        ${TEST_COMMON_SOURCES}
    )

    # The host layer, linked as a front end links it: through a2e_core.
    add_catch2_test(test_machine_host
        tests/integration/test_machine_host.cpp
    )
    target_link_libraries(test_machine_host PRIVATE a2e_core)

    add_catch2_test(test_emulator_basic
        tests/integration/test_emulator_basic.cpp
        ${CORE_TEST_SOURCES}
        ${TEST_COMMON_SOURCES}
    )

    add_catch2_test(test_emulator_state
        tests/integration/test_emulator_state.cpp
        ${CORE_TEST_SOURCES}
        ${TEST_COMMON_SOURCES}
    )

    add_catch2_test(test_basic_load_trace
        tests/integration/test_basic_load_trace.cpp
        ${CORE_TEST_SOURCES}
        ${TEST_COMMON_SOURCES}
    )

    # Super Serial Card
    add_catch2_test(test_ssc
        tests/integration/test_ssc.cpp
        ${CORE_TEST_SOURCES}
        ${TEST_COMMON_SOURCES}
    )

    # Parallel card PR#1 firmware drive (PROMSEL ACK-modulated ROM addressing)
    add_catch2_test(test_parallel_pr1
        tests/integration/test_parallel_pr1.cpp
        ${CORE_TEST_SOURCES}
        ${TEST_COMMON_SOURCES}
    )

    # Condition evaluator (needs full emulator)
    add_catch2_test(test_condition_evaluator
        tests/unit/test_condition_evaluator.cpp
        ${CORE_TEST_SOURCES}
    )

    # Execution ranges, stack pointer and soft switch breakpoints
    # (MachineDebug alone), and the switch catalog every host shows
    add_catch2_test(test_machine_debug
        tests/unit/test_machine_debug.cpp
        src/core/debug/machine_debug.cpp
        src/core/debug/soft_switch_catalog.cpp
    )

    # The profiler's call tree, timeline and per-line costs (header only)
    add_catch2_test(test_profiler
        tests/unit/test_profiler.cpp
    )

    # Debug log sink (only debug_log.cpp)
    add_catch2_test(test_debug_log
        tests/unit/test_debug_log.cpp
        src/core/debug/debug_log.cpp
    )

    # Applesoft variable representation (only applesoft_vars.cpp)
    add_catch2_test(test_applesoft_vars
        tests/unit/test_applesoft_vars.cpp
        src/core/basic/applesoft_vars.cpp
    )
endif()
