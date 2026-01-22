cmake_minimum_required(VERSION 3.20)
project(web-a2e VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Generate compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Source files
set(CORE_SOURCES
    src/core/emulator.cpp
    src/core/cpu/cpu6502.cpp
    src/core/mmu/mmu.cpp
    src/core/video/video.cpp
    src/core/audio/audio.cpp
    src/core/disk/disk2.cpp
    src/core/disk/dsk_disk_image.cpp
    src/core/disk/woz_disk_image.cpp
    src/core/disk/gcr_encoding.cpp
    src/core/input/keyboard.cpp
    src/bindings/wasm_interface.cpp
)

# ROM embedding - generate C arrays from ROM files
set(ROM_FILES
    roms/342-0349-B-C0-FF.bin # Combined 16KB system ROM (C0-FF)
    roms/342-0273-A-US-UK.bin # Character ROM (Apple IIe Enhanced)
    roms/341-0027.bin         # Disk II ROM
)

# Custom command to generate ROM header
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/generated/roms.cpp
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/generate_roms.sh ${CMAKE_SOURCE_DIR}/roms ${CMAKE_BINARY_DIR}/generated/roms.cpp
    DEPENDS ${ROM_FILES}
    COMMENT "Generating ROM data arrays"
)

if(EMSCRIPTEN)
    add_executable(a2e
        ${CORE_SOURCES}
    )

    # Ensure roms.cpp is generated before building
    add_custom_target(generate_roms DEPENDS ${CMAKE_BINARY_DIR}/generated/roms.cpp)
    add_dependencies(a2e generate_roms)

    target_include_directories(a2e PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/core/cpu
        ${CMAKE_BINARY_DIR}/generated
    )

    # Emscripten-specific flags
    set_target_properties(a2e PROPERTIES
        SUFFIX ".js"
        LINK_FLAGS "\
            -s WASM=1 \
            -s MODULARIZE=1 \
            -s EXPORT_NAME='createA2EModule' \
            -s EXPORTED_FUNCTIONS='[ \
                \"_init\", \
                \"_reset\", \
                \"_warmReset\", \
                \"_runCycles\", \
                \"_generateAudioSamples\", \
                \"_consumeFrameSamples\", \
                \"_getFramebuffer\", \
                \"_getFramebufferSize\", \
                \"_isFrameReady\", \
                \"_keyDown\", \
                \"_keyUp\", \
                \"_handleRawKeyDown\", \
                \"_handleRawKeyUp\", \
                \"_insertDisk\", \
                \"_insertBlankDisk\", \
                \"_ejectDisk\", \
                \"_getDiskData\", \
                \"_addBreakpoint\", \
                \"_removeBreakpoint\", \
                \"_enableBreakpoint\", \
                \"_isBreakpointHit\", \
                \"_getBreakpointAddress\", \
                \"_getPC\", \
                \"_getSP\", \
                \"_getA\", \
                \"_getX\", \
                \"_getY\", \
                \"_getP\", \
                \"_getTotalCycles\", \
                \"_isPaused\", \
                \"_setPaused\", \
                \"_stepInstruction\", \
                \"_readMemory\", \
                \"_writeMemory\", \
                \"_disassembleAt\", \
                \"_getSoftSwitchState\", \
                \"_getDiskTrack\", \
                \"_getDiskPhase\", \
                \"_getDiskMotorOn\", \
                \"_getDiskWriteMode\", \
                \"_getDiskHeadPosition\", \
                \"_getSelectedDrive\", \
                \"_isDiskInserted\", \
                \"_isDiskModified\", \
                \"_getDiskFilename\", \
                \"_getLastDiskByte\", \
                \"_exportState\", \
                \"_importState\", \
                \"_malloc\", \
                \"_free\" \
            ]' \
            -s EXPORTED_RUNTIME_METHODS='[\"ccall\", \"cwrap\", \"UTF8ToString\", \"stringToUTF8\", \"HEAPU8\", \"HEAPF32\"]' \
            -s ALLOW_MEMORY_GROWTH=1 \
            -s INITIAL_MEMORY=33554432 \
            -s MAXIMUM_MEMORY=67108864 \
            -s NO_EXIT_RUNTIME=1 \
            -s ASYNCIFY=0 \
            --no-entry \
            -O3 \
            -flto \
        "
    )

    # Copy output to public directory
    add_custom_command(TARGET a2e POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:a2e> ${CMAKE_SOURCE_DIR}/public/a2e.js
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/a2e.wasm ${CMAKE_SOURCE_DIR}/public/a2e.wasm
        COMMENT "Copying WASM files to public directory"
    )

else()
    # Native build for testing
    # Note: a2e-native requires src/native/main.cpp which may not exist
    # The tests below can be built independently

    # ==========================================================================
    # Klaus Dormann CPU Functional Tests
    # ==========================================================================

    enable_testing()

    # 65C02 Extended Opcodes Test
    add_executable(klaus_65c02_test
        tests/klaus/klaus_65c02_test.cpp
        src/core/cpu/cpu6502.cpp
    )
    target_include_directories(klaus_65c02_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/core/cpu
    )
    target_compile_options(klaus_65c02_test PRIVATE -O3 -Wall -Wextra)
    add_test(NAME klaus_65c02_extended_test
        COMMAND klaus_65c02_test
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )

    # 6502 Functional Test
    add_executable(klaus_6502_test
        tests/klaus/klaus_6502_test.cpp
        src/core/cpu/cpu6502.cpp
    )
    target_include_directories(klaus_6502_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/core/cpu
    )
    target_compile_options(klaus_6502_test PRIVATE -O3 -Wall -Wextra)
    add_test(NAME klaus_6502_functional_test
        COMMAND klaus_6502_test
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )

endif()
