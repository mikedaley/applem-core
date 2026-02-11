/*
 * basic_tokenizer.hpp - Applesoft BASIC tokenizer for direct memory insertion
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>

namespace a2e {

// Callback types for reading/writing emulator memory
using MemReadFn = std::function<uint8_t(uint16_t)>;
using MemWriteFn = std::function<void(uint16_t, uint8_t)>;

// Tokenize BASIC source text and load it into emulator memory.
// Writes the tokenized program at TXTTAB (0x0801), sets zero page pointers.
// Uses callbacks to read/write memory (main RAM, bypassing ALTZP).
// Returns the number of lines loaded, or -1 on error.
int loadBasicProgram(const char* source, MemReadFn readMem, MemWriteFn writeMem);

} // namespace a2e
