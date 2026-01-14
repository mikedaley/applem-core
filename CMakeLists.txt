cmake_minimum_required(VERSION 3.20)
project(web-a2e VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
    src/bindings/wasm_interface.cpp
)

# ROM embedding - generate C arrays from ROM files
set(ROM_FILES
    roms/342-0349-B-C0-FF.bin # Combined 16KB system ROM (C0-FF)
    roms/341-0160-A.bin       # Character ROM
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
                \"_insertDisk\", \
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
                \"_getLastDiskByte\", \
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
    add_executable(a2e-native
        ${CORE_SOURCES}
        ${CMAKE_BINARY_DIR}/generated/roms.cpp
        src/native/main.cpp
    )

    target_include_directories(a2e-native PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/core/cpu
        ${CMAKE_BINARY_DIR}/generated
    )

    target_compile_options(a2e-native PRIVATE -O3 -Wall -Wextra)
endif()
