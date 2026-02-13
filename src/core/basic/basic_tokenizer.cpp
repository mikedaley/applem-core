/*
 * basic_tokenizer.cpp - Applesoft BASIC tokenizer for direct memory insertion
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "basic_tokenizer.hpp"
#include "basic_tokens.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace a2e {

// A keyword entry for greedy longest-match tokenization
struct KeywordEntry {
    const char* keyword;
    uint8_t token;
    size_t length;
};

// Build sorted keyword list (longest first) for greedy matching
static std::vector<KeywordEntry> buildKeywordList() {
    std::vector<KeywordEntry> list;
    for (int i = 0; i < APPLESOFT_TOKEN_COUNT; i++) {
        KeywordEntry e;
        e.keyword = APPLESOFT_TOKENS[i];
        e.token = static_cast<uint8_t>(0x80 + i);
        e.length = strlen(APPLESOFT_TOKENS[i]);
        list.push_back(e);
    }
    // Sort by length descending for greedy longest-match
    std::sort(list.begin(), list.end(), [](const KeywordEntry& a, const KeywordEntry& b) {
        return a.length > b.length;
    });
    return list;
}

// Parsed BASIC line
struct BasicLine {
    int lineNumber;
    std::string content; // uppercase content after line number
};

// Parse source into lines, extracting line numbers
static std::vector<BasicLine> parseSource(const char* source) {
    std::vector<BasicLine> lines;
    std::string src(source);

    size_t pos = 0;
    while (pos < src.size()) {
        // Find end of line
        size_t eol = src.find('\n', pos);
        if (eol == std::string::npos) eol = src.size();

        std::string rawLine = src.substr(pos, eol - pos);
        pos = eol + 1;

        // Trim leading/trailing whitespace
        size_t start = rawLine.find_first_not_of(" \t\r");
        if (start == std::string::npos) continue;
        std::string trimmed = rawLine.substr(start);

        // Extract line number
        if (trimmed.empty() || !isdigit(static_cast<unsigned char>(trimmed[0]))) continue;

        size_t numEnd = 0;
        while (numEnd < trimmed.size() && isdigit(static_cast<unsigned char>(trimmed[numEnd]))) {
            numEnd++;
        }

        int lineNum = std::stoi(trimmed.substr(0, numEnd));
        if (lineNum < 0 || lineNum > 63999) continue;

        // Get content after line number, skip leading spaces
        std::string content = trimmed.substr(numEnd);
        size_t contentStart = content.find_first_not_of(" \t");
        if (contentStart != std::string::npos) {
            content = content.substr(contentStart);
        } else {
            content = "";
        }

        // Convert to uppercase, preserving case inside quoted strings
        std::string upper;
        upper.reserve(content.size());
        bool inQuote = false;
        for (char c : content) {
            if (c == '"') {
                inQuote = !inQuote;
                upper += c;
            } else if (inQuote) {
                upper += c;
            } else {
                upper += static_cast<char>(toupper(static_cast<unsigned char>(c)));
            }
        }

        lines.push_back({lineNum, upper});
    }

    // Sort by line number
    std::sort(lines.begin(), lines.end(), [](const BasicLine& a, const BasicLine& b) {
        return a.lineNumber < b.lineNumber;
    });

    return lines;
}

// Tokenize a single line's content
static std::vector<uint8_t> tokenizeLine(const std::string& text,
                                          const std::vector<KeywordEntry>& keywords) {
    std::vector<uint8_t> bytes;
    size_t i = 0;
    bool inRem = false;
    bool inData = false;
    bool inQuote = false;

    while (i < text.size()) {
        char ch = text[i];

        // Inside a quoted string - emit as-is until closing quote
        if (inQuote) {
            bytes.push_back(static_cast<uint8_t>(text[i]));
            if (ch == '"') inQuote = false;
            i++;
            continue;
        }

        // After REM token - emit rest of line as raw ASCII
        if (inRem) {
            bytes.push_back(static_cast<uint8_t>(text[i]));
            i++;
            continue;
        }

        // After DATA token - emit as-is until colon
        if (inData) {
            if (ch == ':') {
                inData = false;
                // Fall through to normal processing for the colon
            } else {
                bytes.push_back(static_cast<uint8_t>(text[i]));
                i++;
                continue;
            }
        }

        // Opening quote
        if (ch == '"') {
            inQuote = true;
            bytes.push_back(static_cast<uint8_t>(text[i]));
            i++;
            continue;
        }

        // ? is shorthand for PRINT
        if (ch == '?') {
            bytes.push_back(0xBA); // PRINT token
            i++;
            continue;
        }

        // Skip spaces outside strings (Apple II tokenizer ignores spaces)
        if (ch == ' ') {
            i++;
            continue;
        }

        // Try greedy longest-match against keywords
        bool matched = false;
        const char* remaining = text.c_str() + i;
        size_t remainingLen = text.size() - i;

        for (const auto& kw : keywords) {
            if (kw.length <= remainingLen &&
                strncmp(remaining, kw.keyword, kw.length) == 0) {
                bytes.push_back(kw.token);
                i += kw.length;
                matched = true;

                if (kw.token == 0xB2) { // REM
                    inRem = true;
                } else if (kw.token == 0x83) { // DATA
                    inData = true;
                }
                break;
            }
        }

        if (!matched) {
            bytes.push_back(static_cast<uint8_t>(text[i]));
            i++;
        }
    }

    return bytes;
}

int loadBasicProgram(const char* source, MemReadFn readMem, MemWriteFn writeMem) {
    if (!source) return -1;

    auto lines = parseSource(source);
    if (lines.empty()) return 0;

    auto keywords = buildKeywordList();

    constexpr uint16_t txttab = 0x0801;
    uint16_t addr = txttab;

    // First pass: tokenize all lines and compute layout
    struct TokenizedLine {
        int lineNumber;
        std::vector<uint8_t> tokens;
        uint16_t lineSize; // 2 (next-ptr) + 2 (line-num) + tokens + 1 (terminator)
    };

    std::vector<TokenizedLine> tokenizedLines;
    for (const auto& line : lines) {
        auto tokens = tokenizeLine(line.content, keywords);
        uint16_t lineSize = static_cast<uint16_t>(2 + 2 + tokens.size() + 1);
        tokenizedLines.push_back({line.lineNumber, std::move(tokens), lineSize});
    }

    // Second pass: write into memory
    for (const auto& line : tokenizedLines) {
        uint16_t nextAddr = addr + line.lineSize;

        // Bounds check
        if (nextAddr >= 0xC000) return -1;

        // Next-pointer (little-endian)
        writeMem(addr,     nextAddr & 0xFF);
        writeMem(addr + 1, (nextAddr >> 8) & 0xFF);

        // Line number (little-endian)
        writeMem(addr + 2, line.lineNumber & 0xFF);
        writeMem(addr + 3, (line.lineNumber >> 8) & 0xFF);

        // Token bytes
        for (size_t i = 0; i < line.tokens.size(); i++) {
            writeMem(static_cast<uint16_t>(addr + 4 + i), line.tokens[i]);
        }

        // Line terminator
        writeMem(static_cast<uint16_t>(addr + 4 + line.tokens.size()), 0x00);

        addr = nextAddr;
    }

    // End-of-program marker
    writeMem(addr,     0x00);
    writeMem(addr + 1, 0x00);

    uint16_t endAddr = addr + 2;

    // Read MEMSIZE ($73/$74) from memory
    uint16_t memsize = readMem(0x73) | (readMem(0x74) << 8);

    // Set zero page pointers
    auto writePtr = [&](uint16_t zpAddr, uint16_t value) {
        writeMem(zpAddr,     value & 0xFF);
        writeMem(zpAddr + 1, (value >> 8) & 0xFF);
    };

    writePtr(0x67, txttab);   // TXTTAB - start of program
    writePtr(0x69, endAddr);  // VARTAB - start of variable space
    writePtr(0x6B, endAddr);  // ARYTAB - start of array space
    writePtr(0x6D, endAddr);  // STREND - end of numeric storage
    writePtr(0x6F, memsize);  // FRETOP - end of string storage
    writePtr(0xAF, endAddr);  // PRGEND - end of program

    // Set interpreter state for direct mode
    writePtr(0xB8, txttab - 1); // TXTPTR
    writeMem(0x76, 0xFF);       // CURLIN+1 high byte = $FF (direct mode)

    return static_cast<int>(tokenizedLines.size());
}

} // namespace a2e
