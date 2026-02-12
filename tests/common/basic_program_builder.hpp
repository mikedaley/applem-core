/*
 * basic_program_builder.hpp - Programmatic BASIC token builders for testing
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace test {

/**
 * ApplesoftProgramBuilder - Build tokenized Applesoft BASIC programs
 *
 * Applesoft token format:
 *   Each line: [next_addr:2][line_num:2][tokens...][0x00]
 *   Program ends with: [0x00][0x00]
 *
 * The base address is typically $0801.
 */
class ApplesoftProgramBuilder {
public:
    explicit ApplesoftProgramBuilder(uint16_t baseAddr = 0x0801);

    // Add a raw tokenized line
    void addLine(uint16_t lineNum, const std::vector<uint8_t>& tokens);

    // Add a line from ASCII text (minimal tokenization - keywords in uppercase)
    void addLine(uint16_t lineNum, const std::string& text);

    // Build the complete tokenized program
    std::vector<uint8_t> build() const;

    // Build with a 2-byte length header prepended (as stored in DOS 3.3 files)
    std::vector<uint8_t> buildWithHeader() const;

private:
    struct Line {
        uint16_t lineNum;
        std::vector<uint8_t> tokens;
    };

    uint16_t baseAddr_;
    std::vector<Line> lines_;

    // Applesoft token values for common keywords
    static constexpr uint8_t TOK_END    = 0x80;
    static constexpr uint8_t TOK_FOR    = 0x81;
    static constexpr uint8_t TOK_NEXT   = 0x82;
    static constexpr uint8_t TOK_DATA   = 0x83;
    static constexpr uint8_t TOK_INPUT  = 0x84;
    static constexpr uint8_t TOK_DEL    = 0x85;
    static constexpr uint8_t TOK_DIM    = 0x86;
    static constexpr uint8_t TOK_READ   = 0x87;
    static constexpr uint8_t TOK_GR     = 0x88;
    static constexpr uint8_t TOK_TEXT   = 0x89;
    static constexpr uint8_t TOK_PR     = 0x8A;
    static constexpr uint8_t TOK_IN     = 0x8B;
    static constexpr uint8_t TOK_CALL   = 0x8C;
    static constexpr uint8_t TOK_PLOT   = 0x8D;
    static constexpr uint8_t TOK_HLIN   = 0x8E;
    static constexpr uint8_t TOK_VLIN   = 0x8F;
    static constexpr uint8_t TOK_HGR2   = 0x90;
    static constexpr uint8_t TOK_HGR    = 0x91;
    static constexpr uint8_t TOK_HCOLOR = 0x92;
    static constexpr uint8_t TOK_HPLOT  = 0x93;
    static constexpr uint8_t TOK_DRAW   = 0x94;
    static constexpr uint8_t TOK_XDRAW  = 0x95;
    static constexpr uint8_t TOK_HTAB   = 0x96;
    static constexpr uint8_t TOK_HOME   = 0x97;
    static constexpr uint8_t TOK_ROT    = 0x98;
    static constexpr uint8_t TOK_SCALE  = 0x99;
    static constexpr uint8_t TOK_SHLOAD = 0x9A;
    static constexpr uint8_t TOK_TRACE  = 0x9B;
    static constexpr uint8_t TOK_NOTRACE= 0x9C;
    static constexpr uint8_t TOK_NORMAL = 0x9D;
    static constexpr uint8_t TOK_INVERSE= 0x9E;
    static constexpr uint8_t TOK_FLASH  = 0x9F;
    static constexpr uint8_t TOK_COLOR  = 0xA0;
    static constexpr uint8_t TOK_POP    = 0xA1;
    static constexpr uint8_t TOK_VTAB   = 0xA2;
    static constexpr uint8_t TOK_HIMEM  = 0xA3;
    static constexpr uint8_t TOK_LOMEM  = 0xA4;
    static constexpr uint8_t TOK_ONERR  = 0xA5;
    static constexpr uint8_t TOK_RESUME = 0xA6;
    static constexpr uint8_t TOK_RECALL = 0xA7;
    static constexpr uint8_t TOK_STORE  = 0xA8;
    static constexpr uint8_t TOK_SPEED  = 0xA9;
    static constexpr uint8_t TOK_LET    = 0xAA;
    static constexpr uint8_t TOK_GOTO   = 0xAB;
    static constexpr uint8_t TOK_RUN    = 0xAC;
    static constexpr uint8_t TOK_IF     = 0xAD;
    static constexpr uint8_t TOK_RESTORE= 0xAE;
    static constexpr uint8_t TOK_AMP    = 0xAF;
    static constexpr uint8_t TOK_GOSUB  = 0xB0;
    static constexpr uint8_t TOK_RETURN = 0xB1;
    static constexpr uint8_t TOK_REM    = 0xB2;
    static constexpr uint8_t TOK_STOP   = 0xB3;
    static constexpr uint8_t TOK_ON     = 0xB4;
    static constexpr uint8_t TOK_WAIT   = 0xB5;
    static constexpr uint8_t TOK_LOAD   = 0xB6;
    static constexpr uint8_t TOK_SAVE   = 0xB7;
    static constexpr uint8_t TOK_DEF    = 0xB8;
    static constexpr uint8_t TOK_POKE   = 0xB9;
    static constexpr uint8_t TOK_PRINT  = 0xBA;
    static constexpr uint8_t TOK_CONT   = 0xBB;
    static constexpr uint8_t TOK_LIST   = 0xBC;
    static constexpr uint8_t TOK_CLEAR  = 0xBD;
    static constexpr uint8_t TOK_GET    = 0xBE;
    static constexpr uint8_t TOK_NEW    = 0xBF;
    // Functions
    static constexpr uint8_t TOK_TAB    = 0xC0;
    static constexpr uint8_t TOK_TO     = 0xC1;
    static constexpr uint8_t TOK_FN     = 0xC2;
    static constexpr uint8_t TOK_SPC    = 0xC3;
    static constexpr uint8_t TOK_THEN   = 0xC4;
    static constexpr uint8_t TOK_AT     = 0xC5;
    static constexpr uint8_t TOK_NOT    = 0xC6;
    static constexpr uint8_t TOK_STEP   = 0xC7;
    static constexpr uint8_t TOK_PLUS   = 0xC8;
    static constexpr uint8_t TOK_MINUS  = 0xC9;
    static constexpr uint8_t TOK_MULT   = 0xCA;
    static constexpr uint8_t TOK_DIV    = 0xCB;
    static constexpr uint8_t TOK_POWER  = 0xCC;
    static constexpr uint8_t TOK_AND    = 0xCD;
    static constexpr uint8_t TOK_OR     = 0xCE;
    static constexpr uint8_t TOK_GT     = 0xCF;
    static constexpr uint8_t TOK_EQ     = 0xD0;
    static constexpr uint8_t TOK_LT     = 0xD1;
    static constexpr uint8_t TOK_SGN    = 0xD2;
    static constexpr uint8_t TOK_INT    = 0xD3;
    static constexpr uint8_t TOK_ABS    = 0xD4;
    static constexpr uint8_t TOK_USR    = 0xD5;
    static constexpr uint8_t TOK_FRE    = 0xD6;
    static constexpr uint8_t TOK_SCRN   = 0xD7;
    static constexpr uint8_t TOK_PDL    = 0xD8;
    static constexpr uint8_t TOK_POS    = 0xD9;
    static constexpr uint8_t TOK_SQR    = 0xDA;
    static constexpr uint8_t TOK_RND    = 0xDB;
    static constexpr uint8_t TOK_LOG    = 0xDC;
    static constexpr uint8_t TOK_EXP    = 0xDD;
    static constexpr uint8_t TOK_COS    = 0xDE;
    static constexpr uint8_t TOK_SIN    = 0xDF;
    static constexpr uint8_t TOK_TAN    = 0xE0;
    static constexpr uint8_t TOK_ATN    = 0xE1;
    static constexpr uint8_t TOK_PEEK   = 0xE2;
    static constexpr uint8_t TOK_LEN    = 0xE3;
    static constexpr uint8_t TOK_STR    = 0xE4;
    static constexpr uint8_t TOK_VAL    = 0xE5;
    static constexpr uint8_t TOK_ASC    = 0xE6;
    static constexpr uint8_t TOK_CHR    = 0xE7;
    static constexpr uint8_t TOK_LEFT   = 0xE8;
    static constexpr uint8_t TOK_RIGHT  = 0xE9;
    static constexpr uint8_t TOK_MID    = 0xEA;
};

/**
 * IntegerBasicProgramBuilder - Build tokenized Integer BASIC programs
 *
 * Integer BASIC token format (different from Applesoft):
 *   Each line: [length:1][line_num:2][tokens...][0x01]
 *   Program ends with: [0x00]
 *
 * Integer BASIC tokens are quite different from Applesoft.
 * Line numbers are stored in little-endian format (low byte first).
 */
class IntegerBasicProgramBuilder {
public:
    explicit IntegerBasicProgramBuilder(uint16_t baseAddr = 0x0801);

    // Add a raw tokenized line
    void addLine(uint16_t lineNum, const std::vector<uint8_t>& tokens);

    // Build the complete tokenized program
    std::vector<uint8_t> build() const;

    // Build with a 2-byte length header prepended
    std::vector<uint8_t> buildWithHeader() const;

    // Integer BASIC token constants
    static constexpr uint8_t TOK_FOR      = 0x20;
    static constexpr uint8_t TOK_GOTO     = 0x22;
    static constexpr uint8_t TOK_GOSUB    = 0x24;
    static constexpr uint8_t TOK_RETURN   = 0x25;
    static constexpr uint8_t TOK_REM      = 0x5D;
    static constexpr uint8_t TOK_LET      = 0x2A;
    static constexpr uint8_t TOK_IF       = 0x28;
    static constexpr uint8_t TOK_PRINT    = 0x32;
    static constexpr uint8_t TOK_INPUT    = 0x21;
    static constexpr uint8_t TOK_END      = 0x51;
    static constexpr uint8_t TOK_THEN     = 0x29;
    static constexpr uint8_t TOK_TO       = 0x5A;
    static constexpr uint8_t TOK_STEP     = 0x5B;
    static constexpr uint8_t TOK_NEXT     = 0x23;
    // Integer BASIC uses 0xB0-0xB9 for digits in numeric tokens
    static constexpr uint8_t TOK_NUM_PREFIX = 0xB0;

private:
    struct Line {
        uint16_t lineNum;
        std::vector<uint8_t> tokens;
    };

    uint16_t baseAddr_;
    std::vector<Line> lines_;
};

} // namespace test
