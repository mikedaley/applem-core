/*
 * basic_tokens.hpp - Applesoft and Integer BASIC token tables and keyword helpers
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <cstring>

namespace a2e {

// Applesoft BASIC tokens - index = token byte - 0x80
static constexpr const char* APPLESOFT_TOKENS[] = {
  "END",      // $80
  "FOR",      // $81
  "NEXT",     // $82
  "DATA",     // $83
  "INPUT",    // $84
  "DEL",      // $85
  "DIM",      // $86
  "READ",     // $87
  "GR",       // $88
  "TEXT",     // $89
  "PR#",      // $8A
  "IN#",      // $8B
  "CALL",     // $8C
  "PLOT",     // $8D
  "HLIN",     // $8E
  "VLIN",     // $8F
  "HGR2",     // $90
  "HGR",      // $91
  "HCOLOR=",  // $92
  "HPLOT",    // $93
  "DRAW",     // $94
  "XDRAW",    // $95
  "HTAB",     // $96
  "HOME",     // $97
  "ROT=",     // $98
  "SCALE=",   // $99
  "SHLOAD",   // $9A
  "TRACE",    // $9B
  "NOTRACE",  // $9C
  "NORMAL",   // $9D
  "INVERSE",  // $9E
  "FLASH",    // $9F
  "COLOR=",   // $A0
  "POP",      // $A1
  "VTAB",     // $A2
  "HIMEM:",   // $A3
  "LOMEM:",   // $A4
  "ONERR",    // $A5
  "RESUME",   // $A6
  "RECALL",   // $A7
  "STORE",    // $A8
  "SPEED=",   // $A9
  "LET",      // $AA
  "GOTO",     // $AB
  "RUN",      // $AC
  "IF",       // $AD
  "RESTORE",  // $AE
  "&",        // $AF
  "GOSUB",    // $B0
  "RETURN",   // $B1
  "REM",      // $B2
  "STOP",     // $B3
  "ON",       // $B4
  "WAIT",     // $B5
  "LOAD",     // $B6
  "SAVE",     // $B7
  "DEF",      // $B8
  "POKE",     // $B9
  "PRINT",    // $BA
  "CONT",     // $BB
  "LIST",     // $BC
  "CLEAR",    // $BD
  "GET",      // $BE
  "NEW",      // $BF
  "TAB(",     // $C0
  "TO",       // $C1
  "FN",       // $C2
  "SPC(",     // $C3
  "THEN",     // $C4
  "AT",       // $C5
  "NOT",      // $C6
  "STEP",     // $C7
  "+",        // $C8
  "-",        // $C9
  "*",        // $CA
  "/",        // $CB
  "^",        // $CC
  "AND",      // $CD
  "OR",       // $CE
  ">",        // $CF
  "=",        // $D0
  "<",        // $D1
  "SGN",      // $D2
  "INT",      // $D3
  "ABS",      // $D4
  "USR",      // $D5
  "FRE",      // $D6
  "SCRN(",    // $D7
  "PDL",      // $D8
  "POS",      // $D9
  "SQR",      // $DA
  "RND",      // $DB
  "LOG",      // $DC
  "EXP",      // $DD
  "COS",      // $DE
  "SIN",      // $DF
  "TAN",      // $E0
  "ATN",      // $E1
  "PEEK",     // $E2
  "LEN",      // $E3
  "STR$",     // $E4
  "VAL",      // $E5
  "ASC",      // $E6
  "CHR$",     // $E7
  "LEFT$",    // $E8
  "RIGHT$",   // $E9
  "MID$",     // $EA
};

static constexpr int APPLESOFT_TOKEN_COUNT = sizeof(APPLESOFT_TOKENS) / sizeof(APPLESOFT_TOKENS[0]);

// Integer BASIC tokens ($00-$7F)
static constexpr const char* INTEGER_BASIC_TOKENS[] = {
  " HIMEM: ",  // $00
  nullptr,     // $01 (end of line)
  "_",         // $02
  ":",         // $03
  " LOAD ",    // $04
  " SAVE ",    // $05
  " CON ",     // $06
  " RUN ",     // $07
  " RUN ",     // $08
  " DEL ",     // $09
  ",",         // $0A
  " NEW ",     // $0B
  " CLR ",     // $0C
  " AUTO ",    // $0D
  ",",         // $0E
  " MAN ",     // $0F
  " HIMEM: ",  // $10
  " LOMEM: ",  // $11
  "+",         // $12
  "-",         // $13
  "*",         // $14
  "/",         // $15
  "=",         // $16
  "#",         // $17
  ">=",        // $18
  ">",         // $19
  "<=",        // $1A
  "<>",        // $1B
  "<",         // $1C
  " AND ",     // $1D
  " OR ",      // $1E
  " MOD ",     // $1F
  "^",         // $20
  "+",         // $21
  "(",         // $22
  ",",         // $23
  " THEN ",    // $24
  " THEN ",    // $25
  ",",         // $26
  ",",         // $27
  "\"",        // $28 (start quote)
  "\"",        // $29 (end quote)
  "(",         // $2A
  "!",         // $2B
  "!",         // $2C
  "(",         // $2D
  "PEEK",      // $2E
  "RND",       // $2F
  "SGN",       // $30
  "ABS",       // $31
  "PDL",       // $32
  "RNDX",      // $33
  "(",         // $34
  "+",         // $35
  "-",         // $36
  " NOT ",     // $37
  "(",         // $38
  "=",         // $39
  "#",         // $3A
  "LEN(",      // $3B
  "ASC(",      // $3C
  "SCRN(",     // $3D
  ",",         // $3E
  "(",         // $3F
  "$",         // $40
  "$",         // $41
  "(",         // $42
  ",",         // $43
  ",",         // $44
  ";",         // $45
  ";",         // $46
  ";",         // $47
  ",",         // $48
  ",",         // $49
  ",",         // $4A
  " TEXT ",    // $4B
  " GR ",      // $4C
  " CALL ",    // $4D
  " DIM ",     // $4E
  " DIM ",     // $4F
  " TAB ",     // $50
  " END ",     // $51
  " INPUT ",   // $52
  " INPUT ",   // $53
  " INPUT ",   // $54
  " FOR ",     // $55
  "=",         // $56
  " TO ",      // $57
  " STEP ",    // $58
  " NEXT ",    // $59
  ",",         // $5A
  " RETURN ",  // $5B
  " GOSUB ",   // $5C
  " REM ",     // $5D
  " LET ",     // $5E
  " GOTO ",    // $5F
  " IF ",      // $60
  " PRINT ",   // $61
  " PRINT ",   // $62
  " PRINT ",   // $63
  " POKE ",    // $64
  ",",         // $65
  " COLOR= ",  // $66
  " PLOT ",    // $67
  ",",         // $68
  " HLIN ",    // $69
  ",",         // $6A
  " AT ",      // $6B
  " VLIN ",    // $6C
  ",",         // $6D
  " AT ",      // $6E
  " VTAB ",    // $6F
  "=",         // $70
  "=",         // $71
  ")",         // $72
  ")",         // $73
  " LIST ",    // $74
  ",",         // $75
  " LIST ",    // $76
  " POP ",     // $77
  " NODSP ",   // $78
  " DSP ",     // $79
  " NOTRACE ", // $7A
  " DSP ",     // $7B
  " DSP ",     // $7C
  " TRACE ",   // $7D
  " PR# ",     // $7E
  " IN# ",     // $7F
};

static constexpr int INTEGER_BASIC_TOKEN_COUNT = 0x80;

// Keywords that need space before them (Applesoft)
inline bool needsSpaceBefore(const char* token) {
  // Check common keywords
  const char* keywords[] = {
    "FOR", "NEXT", "DATA", "INPUT", "DIM", "READ", "GR", "TEXT",
    "CALL", "PLOT", "HLIN", "VLIN", "HGR2", "HGR", "HPLOT",
    "DRAW", "XDRAW", "HTAB", "HOME", "SHLOAD", "TRACE", "NOTRACE",
    "NORMAL", "INVERSE", "FLASH", "POP", "VTAB", "ONERR", "RESUME",
    "RECALL", "STORE", "LET", "GOTO", "RUN", "IF", "RESTORE",
    "GOSUB", "RETURN", "REM", "STOP", "ON", "WAIT", "LOAD", "SAVE",
    "DEF", "POKE", "PRINT", "CONT", "LIST", "CLEAR", "GET", "NEW",
    "TO", "FN", "THEN", "AT", "NOT", "STEP", "AND", "OR", "END",
  };
  for (const char* kw : keywords) {
    if (strcmp(token, kw) == 0) return true;
  }
  return false;
}

// Keywords that need space after them (Applesoft)
inline bool needsSpaceAfter(const char* token) {
  const char* keywords[] = {
    "GOTO", "GOSUB", "THEN", "TO", "STEP", "AND", "OR", "NOT",
    "IF", "ON", "LET", "FOR", "NEXT", "PRINT", "INPUT", "READ",
    "DATA", "DIM", "DEF", "POKE", "CALL", "PLOT", "HLIN", "VLIN",
    "HPLOT", "DRAW", "XDRAW", "HTAB", "VTAB", "ONERR", "WAIT",
    "GET", "AT", "FN",
  };
  for (const char* kw : keywords) {
    if (strcmp(token, kw) == 0) return true;
  }
  return false;
}

} // namespace a2e
