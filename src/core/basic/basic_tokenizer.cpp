/*
 * basic_tokenizer.cpp - Applesoft BASIC tokenizer for direct memory insertion
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 *  Shawn Bullock <shawn@agenticexpert.ai>
 */

#include "basic_tokenizer.hpp"
#include "basic_tokens.hpp"
#include "basic_control_text.hpp"
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
    std::string content; // content after line number, original case
};

// Parse source into lines, extracting line numbers
static std::vector<BasicLine> parseSource(const char* source, std::vector<int>* replaced) {
    std::vector<BasicLine> lines;
    std::string src(source);

    size_t pos = 0;
    while (pos < src.size()) {
        // Find end of line. CR, LF and CRLF all end one, since a listing
        // saved on an Apple II, a Mac of the period or a PC ends its lines
        // each of those ways.
        size_t eol = src.find_first_of("\r\n", pos);
        if (eol == std::string::npos) eol = src.size();

        std::string rawLine = src.substr(pos, eol - pos);
        pos = eol + 1;
        if (eol < src.size() && src[eol] == '\r' && pos < src.size() && src[pos] == '\n') pos++;

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

        // Six digits or more is past 63999 whatever they are, and too many
        // for an int: std::stoi throws on them, which took the app with it.
        if (numEnd > 5) continue;
        int lineNum = std::stoi(trimmed.substr(0, numEnd));
        if (lineNum > 63999) continue;

        // Get content after line number, skip leading spaces
        std::string content = trimmed.substr(numEnd);
        size_t contentStart = content.find_first_not_of(" \t");
        if (contentStart != std::string::npos) {
            content = content.substr(contentStart);
        } else {
            content = "";
        }

        // Skip lines with only a line number and no content
        if (content.empty()) continue;

        // Store content with original case. tokenizeLine folds case only when
        // matching keywords; strings, REM comments, DATA and variable names are
        // emitted verbatim so nothing outside a keyword is altered.
        lines.push_back({lineNum, content});
    }

    // Sort by line number, keeping the order lines were written in among
    // those with the same number, and then keep only the last of each: typing
    // a line number Applesoft already has replaces that line, so a listing
    // that says 20 twice means the second.
    std::stable_sort(lines.begin(), lines.end(), [](const BasicLine& a, const BasicLine& b) {
        return a.lineNumber < b.lineNumber;
    });
    std::vector<BasicLine> kept;
    for (auto& line : lines) {
        if (!kept.empty() && kept.back().lineNumber == line.lineNumber) {
            if (replaced && (replaced->empty() || replaced->back() != line.lineNumber)) {
                replaced->push_back(line.lineNumber);
            }
            kept.back() = std::move(line);
        } else {
            kept.push_back(std::move(line));
        }
    }

    return kept;
}

// Case-insensitive prefix match: does `text` (length textLen) begin with the
// already-uppercase keyword `kw` (length kwLen)? Lets source keywords be typed
// in any case while leaving the source bytes themselves untouched.
static bool matchKeyword(const char* text, size_t textLen,
                         const char* kw, size_t kwLen) {
    if (kwLen > textLen) return false;
    for (size_t j = 0; j < kwLen; j++) {
        if (toupper(static_cast<unsigned char>(text[j])) !=
            static_cast<unsigned char>(kw[j])) {
            return false;
        }
    }
    return true;
}

// Tokenize a single line's content
static std::vector<uint8_t> tokenizeLine(const std::string& text,
                                          const std::vector<KeywordEntry>& keywords) {
    std::vector<uint8_t> bytes;
    size_t i = 0;
    bool inRem = false;
    bool inData = false;
    bool inQuote = false;

    // A character as the program holds it: a control character's token in
    // braces becomes the byte it names (basic_control_text.hpp).
    auto literal = [&]() {
        uint8_t value = 0;
        size_t length = 0;
        if (basic_text::decodeToken(text, i, value, length)) {
            bytes.push_back(value);
            i += length;
        } else {
            bytes.push_back(static_cast<uint8_t>(text[i]));
            i++;
        }
    };

    while (i < text.size()) {
        char ch = text[i];

        // Inside a quoted string - emit as-is until closing quote
        if (inQuote) {
            if (ch == '"') inQuote = false;
            literal();
            continue;
        }

        // After REM token - emit rest of line as raw ASCII
        if (inRem) {
            literal();
            continue;
        }

        // After DATA token - emit as-is until a colon outside quotes. A quote
        // opens a string within the DATA, and a colon in it is part of the
        // item, as Applesoft's own parser sees it (PARSE tests for a quote
        // before it tests the DATA flag).
        if (inData) {
            if (ch == '"') {
                inQuote = true;
                bytes.push_back(static_cast<uint8_t>(ch));
                i++;
                continue;
            }
            if (ch == ':') {
                inData = false;
                // Fall through to normal processing for the colon
            } else {
                literal();
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

        // A control character outside a string is kept too: a listing shows
        // it as a token wherever it is.
        if (ch == '{') {
            literal();
            continue;
        }

        // Try greedy longest-match against keywords
        bool matched = false;
        const char* remaining = text.c_str() + i;
        size_t remainingLen = text.size() - i;

        for (const auto& kw : keywords) {
            if (matchKeyword(remaining, remainingLen, kw.keyword, kw.length)) {
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

BasicProgramImage tokenizeBasicProgram(const char* source, uint16_t txttab) {
    BasicProgramImage image;
    if (!source) return image;

    const auto lines = parseSource(source, &image.replacedLines);
    if (lines.empty()) return image;

    static const auto keywords = buildKeywordList();

    // Each line is [next-ptr:2][line-num:2][tokens...][00], and the program
    // ends with a zero next-pointer. The pointers are absolute, so the image
    // is laid out for the address it will be written at.
    size_t addr = txttab;
    for (const auto& line : lines) {
        const auto tokens = tokenizeLine(line.content, keywords);
        const size_t next = addr + 2 + 2 + tokens.size() + 1;
        image.bytes.push_back(static_cast<uint8_t>(next & 0xFF));
        image.bytes.push_back(static_cast<uint8_t>((next >> 8) & 0xFF));
        image.bytes.push_back(static_cast<uint8_t>(line.lineNumber & 0xFF));
        image.bytes.push_back(static_cast<uint8_t>((line.lineNumber >> 8) & 0xFF));
        image.bytes.insert(image.bytes.end(), tokens.begin(), tokens.end());
        image.bytes.push_back(0x00);
        addr = next;
    }
    image.bytes.push_back(0x00);
    image.bytes.push_back(0x00);
    image.lines = static_cast<int>(lines.size());
    return image;
}

BasicWriteResult writeBasicProgram(const char* source, MemReadFn readMem, MemWriteFn writeMem) {
    BasicWriteResult result;
    if (!source) {
        result.status = BasicWriteStatus::NoSource;
        return result;
    }

    constexpr uint16_t txttab = 0x0801;

    // The whole program is tokenised before a byte of memory is touched, so
    // a program that does not fit leaves the one in memory exactly as it was
    // rather than half overwritten under pointers that no longer match it.
    BasicProgramImage image = tokenizeBasicProgram(source, txttab);
    result.replacedLines = std::move(image.replacedLines);
    if (image.lines == 0) {
        result.status = BasicWriteStatus::Empty;
        return result;
    }

    // The program may run up to HIMEM (MEMSIZ, $73/$74) and no further. Under
    // DOS 3.3 and ProDOS that is $9600, with DOS's buffers and code above it,
    // which a program running on to $C000 overwrote. A MEMSIZ that cannot be
    // Applesoft's (it has not started) falls back to the I/O space.
    uint16_t memsize = static_cast<uint16_t>(readMem(0x73) | (readMem(0x74) << 8));
    if (memsize <= txttab + 2 || memsize > 0xC000) memsize = 0xC000;
    result.limit = memsize;
    result.size = image.bytes.size();
    const size_t endAddr = txttab + image.bytes.size();
    if (endAddr > memsize) {
        result.status = BasicWriteStatus::TooLarge;
        return result;
    }

    for (size_t i = 0; i < image.bytes.size(); i++) {
        writeMem(static_cast<uint16_t>(txttab + i), image.bytes[i]);
    }

    // Set zero page pointers
    auto writePtr = [&](uint16_t zpAddr, uint16_t value) {
        writeMem(zpAddr,     value & 0xFF);
        writeMem(zpAddr + 1, (value >> 8) & 0xFF);
    };

    const uint16_t end = static_cast<uint16_t>(endAddr);
    writePtr(0x67, txttab);   // TXTTAB - start of program
    writePtr(0x69, end);      // VARTAB - start of variable space
    writePtr(0x6B, end);      // ARYTAB - start of array space
    writePtr(0x6D, end);      // STREND - end of numeric storage
    // FRETOP - string space starts empty at HIMEM, as Applesoft's own CLEAR
    // leaves it.
    writePtr(0x6F, memsize);
    writePtr(0xAF, end);      // PRGEND - end of program

    // Set interpreter state for direct mode
    writePtr(0xB8, txttab - 1); // TXTPTR
    writeMem(0x76, 0xFF);       // CURLIN+1 high byte = $FF (direct mode)

    // Clear Applesoft TRACE so a freshly injected program never starts traced.
    // Injecting bypasses the ROM's program-entry path, so any stale trace flag
    // survives and RUN then prints "#<line>" before every line.
    //
    // The flag lives in different places depending on the environment:
    //  - Raw Applesoft: TRCFLG is $F2.
    //  - ProDOS BASIC.SYSTEM: $F2 is repurposed as an $A5 "sentinel"; the live
    //    TRCFLG is parked in BASIC.SYSTEM's save slot at $BE41. Clearing $F2 there
    //    only appears to work because BASIC.SYSTEM re-saves $F2 into $BE41 on the
    //    next carriage return — fragile. So when the sentinel is present, clear
    //    $BE41 directly, independent of output timing.
    if (readMem(0x00F2) == 0xA5) {
        writeMem(0xBE41, 0x00); // BASIC.SYSTEM live TRCFLG save slot
    }
    writeMem(0x00F2, 0x00);

    result.status = BasicWriteStatus::Written;
    result.lines = image.lines;
    return result;
}

int loadBasicProgram(const char* source, MemReadFn readMem, MemWriteFn writeMem) {
    const BasicWriteResult result = writeBasicProgram(source, readMem, writeMem);
    switch (result.status) {
    case BasicWriteStatus::Written: return result.lines;
    case BasicWriteStatus::Empty: return 0;
    case BasicWriteStatus::NoSource:
    case BasicWriteStatus::TooLarge: return -1;
    }
    return -1;
}

} // namespace a2e
