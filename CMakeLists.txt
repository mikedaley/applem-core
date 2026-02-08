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
    src/core/disk-image/dsk_disk_image.cpp
    src/core/disk-image/woz_disk_image.cpp
    src/core/disk-image/gcr_encoding.cpp
    src/core/input/keyboard.cpp
    src/core/disassembler/disassembler.cpp
    src/core/cards/mockingboard/ay8910.cpp
    src/core/cards/mockingboard/via6522.cpp
    src/core/cards/disk2_card.cpp
    src/core/cards/mockingboard_card.cpp
    src/core/cards/thunderclock_card.cpp
    src/core/cards/mouse_card.cpp
    src/core/debug/condition_evaluator.cpp
    src/core/filesystem/dos33.cpp
    src/core/filesystem/prodos.cpp
    src/core/basic/basic_detokenizer.cpp
    src/core/assembler/assembler.cpp
    src/bindings/wasm_interface.cpp
)

# ROM embedding - generate C arrays from ROM files
set(ROM_FILES
    roms/342-0349-B-C0-FF.bin                       # Combined 16KB system ROM (C0-FF)
    roms/342-0273-A-US-UK.bin                       # Character ROM (Apple IIe Enhanced)
    roms/341-0027.bin                               # Disk II ROM
    "roms/Thunderclock Plus ROM.bin"                 # Thunderclock card ROM
    "roms/Apple Mouse Interface Card ROM - 342-0270-C.bin"  # Mouse Interface Card ROM
)

# Custom command to generate ROM header
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/generated/roms.cpp
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/generate_roms.sh ${CMAKE_SOURCE_DIR}/roms ${CMAKE_BINARY_DIR}/generated/roms.cpp
    DEPENDS ${ROM_FILES} ${CMAKE_SOURCE_DIR}/scripts/generate_roms.sh
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
                \"_consumeFrameSamples\", \
                \"_getFramebuffer\", \
                \"_getFramebufferSize\", \
                \"_forceRenderFrame\", \
                \"_isFrameReady\", \
                \"_keyDown\", \
                \"_keyUp\", \
                \"_handleRawKeyDown\", \
                \"_handleRawKeyUp\", \
                \"_charToAppleKey\", \
                \"_insertDisk\", \
                \"_insertBlankDisk\", \
                \"_ejectDisk\", \
                \"_getDiskData\", \
                \"_addBreakpoint\", \
                \"_removeBreakpoint\", \
                \"_enableBreakpoint\", \
                \"_isBreakpointHit\", \
                \"_getBreakpointAddress\", \
                \"_addBasicBreakpoint\", \
                \"_removeBasicBreakpoint\", \
                \"_clearBasicBreakpoints\", \
                \"_clearBasicBreakpointHit\", \
                \"_hasBasicBreakpoints\", \
                \"_isBasicBreakpointHit\", \
                \"_getBasicBreakLine\", \
                \"_isBasicProgramRunning\", \
                \"_stepBasicLine\", \
                \"_stepBasicStatement\", \
                \"_getBasicTxtptr\", \
                \"_getBasicStatementIndex\", \
                \"_getBasicLineBytes\", \
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
                \"_screenCodeToAscii\", \
                \"_readScreenText\", \
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
                \"_setMonochrome\", \
                \"_isMonochrome\", \
                \"_disassembleRawData\", \
                \"_getDisasmInstructions\", \
                \"_getDisasmInstructionLength\", \
                \"_malloc\", \
                \"_free\", \
                \"_getSlotCard\", \
                \"_setSlotCard\", \
                \"_isSlotEmpty\", \
                \"_getMockingboardWaveform\", \
                \"_setMockingboardChannelMute\", \
                \"_getMockingboardChannelMute\", \
                \"_isKeyboardReady\", \
                \"_setSpeedMultiplier\", \
                \"_getSpeedMultiplier\", \
                \"_mouseMove\", \
                \"_mouseButton\", \
                \"_isMouseCardInstalled\", \
                \"_getMouseCardState\", \
                \"_getMouseCardPIARegister\", \
                \"_isIRQPending\", \
                \"_isNMIPending\", \
                \"_isNMIEdge\", \
                \"_setRegA\", \
                \"_setRegX\", \
                \"_setRegY\", \
                \"_setRegSP\", \
                \"_setRegPC\", \
                \"_setRegP\", \
                \"_peekMemory\", \
                \"_readMainRAM\", \
                \"_addWatchpoint\", \
                \"_removeWatchpoint\", \
                \"_clearWatchpoints\", \
                \"_isWatchpointHit\", \
                \"_getWatchpointAddress\", \
                \"_getWatchpointValue\", \
                \"_isWatchpointWrite\", \
                \"_setTraceEnabled\", \
                \"_clearTrace\", \
                \"_getTraceCount\", \
                \"_getTraceHead\", \
                \"_getTraceBuffer\", \
                \"_getTraceCapacity\", \
                \"_setProfileEnabled\", \
                \"_clearProfile\", \
                \"_getProfileCycles\", \
                \"_addBeamBreakpoint\", \
                \"_removeBeamBreakpoint\", \
                \"_enableBeamBreakpoint\", \
                \"_clearAllBeamBreakpoints\", \
                \"_isBeamBreakpointHit\", \
                \"_getBeamBreakpointHitId\", \
                \"_getBeamBreakScanline\", \
                \"_getBeamBreakHPos\", \
                \"_getFrameCycle\", \
                \"_getBeamScanline\", \
                \"_getBeamHPos\", \
                \"_getBeamColumn\", \
                \"_isInVBL\", \
                \"_isInHBLANK\", \
                \"_stepOver\", \
                \"_stepOut\", \
                \"_clearTempBreakpoint\", \
                \"_isTempBreakpointHit\", \
                \"_getOpcodeMnemonic\", \
                \"_getCallStack\", \
                \"_getCallStackBuffer\", \
                \"_isLikelyReturnAddress\", \
                \"_evaluateCondition\", \
                \"_evaluateExpression\", \
                \"_getConditionError\", \
                \"_isDOS33Format\", \
                \"_getDOS33Catalog\", \
                \"_getDOS33CatalogBuffer\", \
                \"_getDOS33CatalogEntrySize\", \
                \"_getDOS33EntryFilename\", \
                \"_getDOS33EntryFileType\", \
                \"_getDOS33EntryFileTypeName\", \
                \"_getDOS33EntryIsLocked\", \
                \"_getDOS33EntrySectorCount\", \
                \"_readDOS33File\", \
                \"_getDOS33FileBuffer\", \
                \"_isProDOSFormat\", \
                \"_getProDOSVolumeInfo\", \
                \"_getProDOSVolumeName\", \
                \"_getProDOSTotalBlocks\", \
                \"_getProDOSCatalog\", \
                \"_getProDOSEntryFilename\", \
                \"_getProDOSEntryPath\", \
                \"_getProDOSEntryFileType\", \
                \"_getProDOSEntryFileTypeName\", \
                \"_getProDOSEntryStorageType\", \
                \"_getProDOSEntryEOF\", \
                \"_getProDOSEntryAuxType\", \
                \"_getProDOSEntryIsLocked\", \
                \"_getProDOSEntryBlocksUsed\", \
                \"_getProDOSEntryIsDirectory\", \
                \"_readProDOSFile\", \
                \"_getProDOSFileBuffer\", \
                \"_mapProDOSFileType\", \
                \"_detokenizeApplesoft\", \
                \"_detokenizeIntegerBasic\", \
                \"_assembleSource\", \
                \"_getAsmOutputSize\", \
                \"_getAsmOutputBuffer\", \
                \"_getAsmOrigin\", \
                \"_getAsmErrorCount\", \
                \"_getAsmErrorLine\", \
                \"_getAsmErrorMessage\", \
                \"_loadAsmIntoMemory\", \
                \"_getAsmSymbolCount\", \
                \"_getAsmSymbolName\", \
                \"_getAsmSymbolValue\", \
                \"_isBasicErrorHit\", \
                \"_getBasicErrorLine\", \
                \"_getBasicErrorTxtptr\", \
                \"_getBasicErrorCode\", \
                \"_clearBasicError\" \
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

    # ==========================================================================
    # Thunderclock Card Tests
    # ==========================================================================

    # Ensure roms.cpp is generated for native builds too
    add_custom_target(generate_roms_native DEPENDS ${CMAKE_BINARY_DIR}/generated/roms.cpp)

    add_executable(thunderclock_test
        tests/thunderclock/thunderclock_test.cpp
        src/core/cards/thunderclock_card.cpp
    )
    add_dependencies(thunderclock_test generate_roms_native)
    target_include_directories(thunderclock_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/core/cards
        ${CMAKE_BINARY_DIR}/generated
    )
    target_compile_options(thunderclock_test PRIVATE -O3 -Wall -Wextra)
    add_test(NAME thunderclock_card_test
        COMMAND thunderclock_test
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )

    # Thunderclock MMU Integration Test
    add_executable(thunderclock_mmu_test
        tests/thunderclock/thunderclock_mmu_test.cpp
        src/core/cards/thunderclock_card.cpp
        src/core/mmu/mmu.cpp
    )
    add_dependencies(thunderclock_mmu_test generate_roms_native)
    target_include_directories(thunderclock_mmu_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/core/cards
        ${CMAKE_BINARY_DIR}/generated
    )
    target_compile_options(thunderclock_mmu_test PRIVATE -O3 -Wall -Wextra)
    add_test(NAME thunderclock_mmu_integration_test
        COMMAND thunderclock_mmu_test
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )

endif()
