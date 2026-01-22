#pragma once

#include <cstdint>
#include <string>

namespace a2e {

/**
 * Standalone 6502/65C02 disassembler
 *
 * This module provides disassembly functionality independent of the emulator,
 * suitable for use with raw binary data (e.g., file browser, external tools).
 */

/**
 * Disassemble a single instruction from raw data
 *
 * @param data Pointer to the instruction bytes
 * @param size Number of bytes available in data
 * @param baseAddress Address to display (and for relative branch calculations)
 * @param html If true, output HTML with syntax highlighting classes
 * @return Formatted disassembly string
 */
std::string disassembleInstruction(const uint8_t *data, size_t size,
                                   uint16_t baseAddress, bool html = false);

/**
 * Get the length of an instruction given its opcode
 *
 * @param opcode The instruction opcode byte
 * @return Number of bytes for this instruction (1-3)
 */
int getInstructionLength(uint8_t opcode);

/**
 * Disassemble a block of raw binary data
 *
 * @param data Pointer to the binary data
 * @param size Number of bytes in the data
 * @param baseAddress Starting address for display
 * @param html If true, output HTML with syntax highlighting classes
 * @return Full disassembly listing
 */
std::string disassembleBlock(const uint8_t *data, size_t size,
                             uint16_t baseAddress, bool html = false);

} // namespace a2e
