#include "basic_detokenizer.hpp"
#include "basic_tokens.hpp"
#include <cstdio>
#include <cstring>

namespace a2e {

char BasicDetokenizer::outputBuffer_[MAX_OUTPUT];
int BasicDetokenizer::outputLen_ = 0;

void BasicDetokenizer::appendChar(char c) {
  if (outputLen_ < MAX_OUTPUT - 1) {
    outputBuffer_[outputLen_++] = c;
  }
}

void BasicDetokenizer::appendStr(const char* s) {
  while (*s && outputLen_ < MAX_OUTPUT - 1) {
    outputBuffer_[outputLen_++] = *s++;
  }
}

void BasicDetokenizer::appendInt(int n) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%d", n);
  appendStr(buf);
}

void BasicDetokenizer::appendPaddedLineNum(int n) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%5d", n);
  appendStr(buf);
}

const char* BasicDetokenizer::detokenizeApplesoft(const uint8_t* data, int size,
                                                   bool hasLengthHeader) {
  outputLen_ = 0;

  // DOS 3.3 files have a 2-byte file length header, ProDOS files do not
  int offset = hasLengthHeader ? 2 : 0;
  int prevLineNum = -1;
  bool firstLine = true;

  // Track indentation
  int indentLevel = 0;

  // Collect lines first, then format with indentation
  struct LineInfo {
    int lineNum;
    int contentStart; // offset into outputBuffer_
    int contentLen;
    int indent;
  };
  LineInfo lines[4096];
  int lineCount = 0;

  // Temporary buffer for line content
  char lineBuf[8192];
  int lineBufLen = 0;

  auto lineAppendChar = [&](char c) {
    if (lineBufLen < (int)sizeof(lineBuf) - 1)
      lineBuf[lineBufLen++] = c;
  };
  auto lineAppendStr = [&](const char* s) {
    while (*s && lineBufLen < (int)sizeof(lineBuf) - 1)
      lineBuf[lineBufLen++] = *s++;
  };

  while (offset < size - 4) {
    // Read next line pointer (2 bytes)
    int nextLine = data[offset] | (data[offset + 1] << 8);
    if (nextLine == 0) break;

    // Read line number (2 bytes)
    int lineNum = data[offset + 2] | (data[offset + 3] << 8);

    // Sanity checks
    if (lineNum > 63999) break;
    if (lineNum <= prevLineNum && prevLineNum >= 0) break;
    prevLineNum = lineNum;

    offset += 4;

    // Track keywords for indentation
    int forCount = 0;
    int nextCount = 0;

    lineBufLen = 0;
    bool inString = false;
    bool inRem = false;
    bool inData = false;
    const char* lastType = "start";

    while (offset < size && data[offset] != 0x00) {
      uint8_t byte = data[offset++];

      if (inRem) {
        char ch = byte & 0x7F;
        if (ch >= 0x20 && ch < 0x7F) {
          lineAppendChar(ch);
        }
      } else if (inString) {
        if (byte == 0x22) {
          lineAppendChar('"');
          inString = false;
          lastType = "string";
        } else {
          char ch = byte & 0x7F;
          if (ch >= 0x20 && ch < 0x7F) {
            lineAppendChar(ch);
          }
        }
      } else if (inData) {
        if (byte == 0x3A) {
          lineAppendStr(" : ");
          inData = false;
          lastType = "punct";
        } else {
          char ch = byte & 0x7F;
          if (ch >= 0x20 && ch < 0x7F) {
            lineAppendChar(ch);
          }
        }
      } else if (byte >= 0x80) {
        int tokenIdx = byte - 0x80;
        if (tokenIdx >= APPLESOFT_TOKEN_COUNT) continue;
        const char* token = APPLESOFT_TOKENS[tokenIdx];
        if (!token) continue;

        // Track FOR/NEXT
        if (strcmp(token, "FOR") == 0) forCount++;
        if (strcmp(token, "NEXT") == 0) nextCount++;

        // Add space before keyword if needed
        if (needsSpaceBefore(token) &&
            strcmp(lastType, "start") != 0 &&
            strcmp(lastType, "punct") != 0) {
          lineAppendChar(' ');
        }

        if (strcmp(token, "REM") == 0) {
          lineAppendStr(token);
          inRem = true;
          lastType = "keyword";
        } else if (strcmp(token, "DATA") == 0) {
          lineAppendStr(token);
          inData = true;
          lastType = "keyword";
        } else if (strlen(token) == 1 && strchr("+-*/^=<>", token[0])) {
          // Operator tokens
          lineAppendChar(' ');
          lineAppendStr(token);
          lineAppendChar(' ');
          lastType = "operator";
        } else {
          lineAppendStr(token);
          lastType = "keyword";

          if (needsSpaceAfter(token)) {
            lineAppendChar(' ');
            lastType = "space";
          }
        }
      } else if (byte == 0x22) {
        lineAppendChar('"');
        inString = true;
      } else if (byte == 0x3A) {
        lineAppendStr(" : ");
        lastType = "punct";
      } else if (byte >= 0x30 && byte <= 0x39) {
        // Number
        lineAppendChar((char)byte);
        while (offset < size && data[offset] != 0x00 &&
               data[offset] >= 0x30 && data[offset] <= 0x39) {
          lineAppendChar((char)data[offset++]);
        }
        // Decimal point
        if (offset < size && data[offset] == 0x2E) {
          lineAppendChar('.');
          offset++;
          while (offset < size && data[offset] != 0x00 &&
                 data[offset] >= 0x30 && data[offset] <= 0x39) {
            lineAppendChar((char)data[offset++]);
          }
        }
        lastType = "number";
      } else if ((byte >= 0x41 && byte <= 0x5A) ||
                 (byte >= 0x61 && byte <= 0x7A)) {
        // Variable name
        lineAppendChar((char)byte);
        while (offset < size && data[offset] != 0x00) {
          uint8_t next = data[offset];
          if ((next >= 0x41 && next <= 0x5A) ||
              (next >= 0x61 && next <= 0x7A) ||
              (next >= 0x30 && next <= 0x39) ||
              next == 0x24 || next == 0x25) {
            lineAppendChar((char)next);
            offset++;
          } else {
            break;
          }
        }
        lastType = "variable";
      } else if (byte == 0x20) {
        if (strcmp(lastType, "space") != 0 &&
            strcmp(lastType, "punct") != 0 &&
            strcmp(lastType, "start") != 0) {
          lineAppendChar(' ');
          lastType = "space";
        }
      } else {
        char ch = (char)byte;
        if (strchr("+-*/^=<>", ch)) {
          lineAppendChar(' ');
          lineAppendChar(ch);
          lineAppendChar(' ');
          lastType = "operator";
        } else if (strchr("(),;", ch)) {
          lineAppendChar(ch);
          lastType = "punct";
        } else if (byte >= 0x20 && byte < 0x7F) {
          lineAppendChar(ch);
          lastType = "text";
        }
      }
    }

    // Flush remaining
    if (inString) {
      // Unterminated string - already in buffer
    }

    if (offset < size) offset++; // Skip end-of-line marker

    // Strip leading whitespace
    int contentStart = 0;
    while (contentStart < lineBufLen && lineBuf[contentStart] == ' ')
      contentStart++;

    // Adjust indentation
    if (nextCount > 0) {
      indentLevel -= nextCount;
      if (indentLevel < 0) indentLevel = 0;
    }

    if (lineCount < 4096) {
      lines[lineCount].lineNum = lineNum;
      lines[lineCount].contentStart = contentStart;
      lines[lineCount].contentLen = lineBufLen - contentStart;
      lines[lineCount].indent = indentLevel;

      // Now append to output buffer
      if (!firstLine) appendChar('\n');
      firstLine = false;

      appendPaddedLineNum(lineNum);
      appendChar(' ');

      // Indent
      int indentChars = indentLevel * 3;
      for (int i = 0; i < indentChars && outputLen_ < MAX_OUTPUT - 1; i++)
        appendChar(' ');

      // Content
      for (int i = contentStart; i < lineBufLen && outputLen_ < MAX_OUTPUT - 1; i++)
        appendChar(lineBuf[i]);

      lineCount++;
    }

    if (forCount > 0) {
      indentLevel += forCount;
    }
  }

  outputBuffer_[outputLen_] = '\0';
  return outputBuffer_;
}

const char* BasicDetokenizer::detokenizeIntegerBasic(const uint8_t* data, int size,
                                                      bool hasLengthHeader) {
  outputLen_ = 0;

  // DOS 3.3 files have a 2-byte program length header, ProDOS files do not
  int offset = hasLengthHeader ? 2 : 0;
  bool firstLine = true;
  int indentLevel = 0;

  while (offset < size) {
    int lineLength = data[offset];
    if (lineLength == 0 || lineLength < 4 || offset + lineLength > size) break;

    int lineNum = data[offset + 1] | (data[offset + 2] << 8);
    if (lineNum > 32767) break;

    int pos = offset + 3;
    int lineEnd = offset + lineLength;

    // Temporary line buffer
    char lineBuf[4096];
    int lineBufLen = 0;

    auto lineAppendChar = [&](char c) {
      if (lineBufLen < (int)sizeof(lineBuf) - 1)
        lineBuf[lineBufLen++] = c;
    };
    auto lineAppendStr = [&](const char* s) {
      while (*s && lineBufLen < (int)sizeof(lineBuf) - 1)
        lineBuf[lineBufLen++] = *s++;
    };

    bool inRem = false;
    bool inQuote = false;
    int forCount = 0;
    int nextCount = 0;

    while (pos < lineEnd) {
      uint8_t byte = data[pos++];

      if (byte == 0x01) {
        break; // End of line
      } else if (inRem) {
        char ch = byte >= 0x80 ? (byte & 0x7F) : byte;
        if (ch >= 0x20 && ch < 0x7F) {
          lineAppendChar(ch);
        }
      } else if (inQuote) {
        if (byte == 0x29) {
          // End quote token
          lineAppendChar('"');
          inQuote = false;
        } else {
          char ch = byte >= 0x80 ? (byte & 0x7F) : byte;
          if (ch >= 0x20 && ch < 0x7F) {
            lineAppendChar(ch);
          }
        }
      } else if (byte >= 0xB0 && byte <= 0xB9) {
        // Numeric constant: $B0-$B9 followed by 2-byte integer
        if (pos + 1 < lineEnd) {
          int num = data[pos] | (data[pos + 1] << 8);
          int value = num > 32767 ? num - 65536 : num;
          char numBuf[12];
          snprintf(numBuf, sizeof(numBuf), "%d", value);
          lineAppendStr(numBuf);
          pos += 2;
        }
      } else if (byte == 0x28) {
        // Start quote
        lineAppendChar('"');
        inQuote = true;
      } else if (byte == 0x5D) {
        // REM token
        lineAppendStr(" REM ");
        inRem = true;
      } else if (byte < INTEGER_BASIC_TOKEN_COUNT && INTEGER_BASIC_TOKENS[byte]) {
        const char* token = INTEGER_BASIC_TOKENS[byte];
        const char* trimmed = token;
        // Skip leading spaces for identification
        while (*trimmed == ' ') trimmed++;

        if (strcmp(trimmed, "FOR ") == 0 || strncmp(trimmed, "FOR", 3) == 0) {
          // Check if it's FOR (token $55)
          if (byte == 0x55) forCount++;
        }
        if (byte == 0x59) nextCount++; // NEXT

        lineAppendStr(token);
      } else if (byte >= 0x80) {
        // High-bit ASCII character (variable name)
        char ch = byte & 0x7F;
        lineAppendChar(ch);
        while (pos < lineEnd) {
          uint8_t next = data[pos];
          if (next >= 0x80) {
            char nc = next & 0x7F;
            if ((nc >= 'A' && nc <= 'Z') || (nc >= 'a' && nc <= 'z') ||
                (nc >= '0' && nc <= '9')) {
              lineAppendChar(nc);
              pos++;
            } else {
              break;
            }
          } else {
            break;
          }
        }
      } else if (byte >= 0x20 && byte < 0x80) {
        lineAppendChar((char)byte);
      }
    }

    // Strip leading whitespace
    int contentStart = 0;
    while (contentStart < lineBufLen && lineBuf[contentStart] == ' ')
      contentStart++;

    // Adjust indentation
    if (nextCount > 0) {
      indentLevel -= nextCount;
      if (indentLevel < 0) indentLevel = 0;
    }

    if (!firstLine) appendChar('\n');
    firstLine = false;

    appendPaddedLineNum(lineNum);
    appendChar(' ');

    int indentChars = indentLevel * 2;
    for (int i = 0; i < indentChars && outputLen_ < MAX_OUTPUT - 1; i++)
      appendChar(' ');

    for (int i = contentStart; i < lineBufLen && outputLen_ < MAX_OUTPUT - 1; i++)
      appendChar(lineBuf[i]);

    if (forCount > 0) {
      indentLevel += forCount;
    }

    offset += lineLength;
  }

  outputBuffer_[outputLen_] = '\0';
  return outputBuffer_;
}

} // namespace a2e
