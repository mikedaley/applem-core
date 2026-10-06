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
#include <vector>

namespace a2e {

// Callback types for reading/writing emulator memory
using MemReadFn = std::function<uint8_t(uint16_t)>;
using MemWriteFn = std::function<void(uint16_t, uint8_t)>;

// A program tokenised as it would sit in memory from `txttab`: every line
// with its link and number, then the zero link that ends the program.
// Control characters are read from their tokens in braces
// (basic_control_text.hpp), and a line number given twice keeps the later
// line, as typing it into Applesoft would; replacedLines lists those numbers.
struct BasicProgramImage {
  std::vector<uint8_t> bytes;
  int lines = 0;
  std::vector<int> replacedLines;
};
BasicProgramImage tokenizeBasicProgram(const char* source, uint16_t txttab = 0x0801);

enum class BasicWriteStatus { Written, Empty, NoSource, TooLarge };

struct BasicWriteResult {
  BasicWriteStatus status = BasicWriteStatus::NoSource;
  int lines = 0;
  size_t size = 0;        // the program's bytes, end marker included
  uint16_t limit = 0;     // HIMEM, which the program must end at or below
  std::vector<int> replacedLines;
};

// Tokenize BASIC source text and write it into emulator memory at TXTTAB
// ($0801), setting the zero page pointers. Uses callbacks to read/write
// memory (main RAM, bypassing ALTZP). The program is checked against HIMEM
// before anything is written: one that does not fit writes nothing.
BasicWriteResult writeBasicProgram(const char* source, MemReadFn readMem, MemWriteFn writeMem);

// writeBasicProgram for callers that only want a count: the number of lines
// loaded, 0 for none, or -1 on error (nothing written).
int loadBasicProgram(const char* source, MemReadFn readMem, MemWriteFn writeMem);

} // namespace a2e
