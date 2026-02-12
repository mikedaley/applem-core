/*
 * basic_program_builder.cpp - Programmatic BASIC token builders
 */

#include "basic_program_builder.hpp"
#include <cstring>
#include <algorithm>

namespace test {

// ==================== ApplesoftProgramBuilder ====================

ApplesoftProgramBuilder::ApplesoftProgramBuilder(uint16_t baseAddr)
    : baseAddr_(baseAddr) {}

void ApplesoftProgramBuilder::addLine(uint16_t lineNum, const std::vector<uint8_t>& tokens) {
    lines_.push_back({lineNum, tokens});
}

void ApplesoftProgramBuilder::addLine(uint16_t lineNum, const std::string& text) {
    // Simple tokenizer: just store the ASCII text as-is
    // For proper tokenization, use the real tokenizer
    std::vector<uint8_t> tokens(text.begin(), text.end());
    lines_.push_back({lineNum, tokens});
}

std::vector<uint8_t> ApplesoftProgramBuilder::build() const {
    std::vector<uint8_t> result;
    uint16_t addr = baseAddr_;

    for (const auto& line : lines_) {
        // Calculate next line address
        // Format: [next_addr:2][line_num:2][tokens...][0x00]
        uint16_t nextAddr = addr + 4 + static_cast<uint16_t>(line.tokens.size()) + 1;

        // Next line address
        result.push_back(nextAddr & 0xFF);
        result.push_back((nextAddr >> 8) & 0xFF);

        // Line number
        result.push_back(line.lineNum & 0xFF);
        result.push_back((line.lineNum >> 8) & 0xFF);

        // Tokens
        result.insert(result.end(), line.tokens.begin(), line.tokens.end());

        // Line terminator
        result.push_back(0x00);

        addr = nextAddr;
    }

    // Program terminator
    result.push_back(0x00);
    result.push_back(0x00);

    return result;
}

std::vector<uint8_t> ApplesoftProgramBuilder::buildWithHeader() const {
    auto program = build();
    uint16_t len = static_cast<uint16_t>(program.size());

    std::vector<uint8_t> result;
    result.push_back(len & 0xFF);
    result.push_back((len >> 8) & 0xFF);
    result.insert(result.end(), program.begin(), program.end());

    return result;
}

// ==================== IntegerBasicProgramBuilder ====================

IntegerBasicProgramBuilder::IntegerBasicProgramBuilder(uint16_t baseAddr)
    : baseAddr_(baseAddr) {}

void IntegerBasicProgramBuilder::addLine(uint16_t lineNum, const std::vector<uint8_t>& tokens) {
    lines_.push_back({lineNum, tokens});
}

std::vector<uint8_t> IntegerBasicProgramBuilder::build() const {
    std::vector<uint8_t> result;

    for (const auto& line : lines_) {
        // Integer BASIC format: [length:1][line_num_lo:1][line_num_hi:1][tokens...][0x01]
        uint8_t lineLen = 3 + static_cast<uint8_t>(line.tokens.size()) + 1;

        result.push_back(lineLen);

        // Line number (little-endian, same as Applesoft)
        result.push_back(line.lineNum & 0xFF);
        result.push_back((line.lineNum >> 8) & 0xFF);

        // Tokens
        result.insert(result.end(), line.tokens.begin(), line.tokens.end());

        // Line terminator (0x01 for Integer BASIC)
        result.push_back(0x01);
    }

    // Program terminator
    result.push_back(0x00);

    return result;
}

std::vector<uint8_t> IntegerBasicProgramBuilder::buildWithHeader() const {
    auto program = build();
    uint16_t len = static_cast<uint16_t>(program.size());

    std::vector<uint8_t> result;
    result.push_back(len & 0xFF);
    result.push_back((len >> 8) & 0xFF);
    result.insert(result.end(), program.begin(), program.end());

    return result;
}

} // namespace test
