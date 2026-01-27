// Simple GCR encoding/decoding test
// Compile: g++ -std=c++17 -I../src/core/disk -o gcr-test gcr-test.cpp ../src/core/disk/gcr_encoding.cpp

#include <iostream>
#include <iomanip>
#include <cstring>
#include "../src/core/disk/gcr_encoding.hpp"

using namespace a2e;

// 6-and-2 decoding table (reverse of ENCODE_6_AND_2)
static constexpr std::array<int8_t, 256> DECODE_6_AND_2 = []() {
  std::array<int8_t, 256> table{};
  for (int i = 0; i < 256; i++) {
    table[i] = -1; // Invalid by default
  }
  // Fill in valid mappings from the encode table
  for (int i = 0; i < 64; i++) {
    table[GCR::ENCODE_6_AND_2[i]] = static_cast<int8_t>(i);
  }
  return table;
}();

bool decode6and2(const uint8_t *encoded, uint8_t *output) {
  // Decode 343 nibbles back to 256 bytes
  uint8_t buffer[342];

  // XOR decode (reverse of encode)
  uint8_t prev = 0;
  for (int i = 0; i < 342; i++) {
    int8_t decoded = DECODE_6_AND_2[encoded[i]];
    if (decoded < 0) {
      std::cerr << "Invalid nibble at position " << i << ": 0x"
                << std::hex << (int)encoded[i] << std::dec << std::endl;
      return false;
    }
    buffer[i] = decoded ^ prev;
    prev = buffer[i];
  }

  // Verify checksum
  int8_t checksum_decoded = DECODE_6_AND_2[encoded[342]];
  if (checksum_decoded < 0) {
    std::cerr << "Invalid checksum nibble: 0x" << std::hex << (int)encoded[342] << std::dec << std::endl;
  } else if ((prev & 0x3F) != (checksum_decoded & 0x3F)) {
    std::cerr << "Checksum mismatch: expected 0x" << std::hex << (int)(prev & 0x3F)
              << " got 0x" << (int)(checksum_decoded & 0x3F) << std::dec << std::endl;
  }

  // Reconstruct 256 bytes from auxiliary (86) and primary (256) buffers
  for (int i = 0; i < 256; i++) {
    // High 6 bits from primary buffer
    uint8_t high = buffer[86 + i] << 2;

    // Low 2 bits from auxiliary buffer
    // NOTE: encode swaps bits 0,1, so decode must unswap
    uint8_t aux_byte = buffer[i % 86];
    int shift = (i / 86) * 2;
    uint8_t raw = (aux_byte >> shift) & 0x03;
    // Unswap: bit 0 <-> bit 1
    uint8_t low = ((raw & 0x01) << 1) | ((raw & 0x02) >> 1);

    output[i] = high | low;
  }

  return true;
}

int main() {
  // Create a test sector with known data
  uint8_t sector_data[256];
  for (int i = 0; i < 256; i++) {
    sector_data[i] = i; // Simple pattern: 0, 1, 2, ..., 255
  }

  std::cout << "Original sector data (first 32 bytes):" << std::endl;
  for (int i = 0; i < 32; i++) {
    std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)sector_data[i] << " ";
    if ((i + 1) % 16 == 0) std::cout << std::endl;
  }
  std::cout << std::dec << std::endl;

  // Encode using 6-and-2
  std::vector<uint8_t> encoded = GCR::encode6and2(sector_data);

  std::cout << "Encoded nibbles: " << encoded.size() << " bytes" << std::endl;
  std::cout << "First 48 nibbles:" << std::endl;
  for (int i = 0; i < 48; i++) {
    std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)encoded[i] << " ";
    if ((i + 1) % 16 == 0) std::cout << std::endl;
  }
  std::cout << std::dec << std::endl;

  // Verify all nibbles have bit 7 set
  int invalid_count = 0;
  for (size_t i = 0; i < encoded.size(); i++) {
    if (!(encoded[i] & 0x80)) {
      std::cout << "Warning: nibble at position " << i << " doesn't have bit 7 set: 0x"
                << std::hex << (int)encoded[i] << std::dec << std::endl;
      invalid_count++;
    }
  }
  if (invalid_count == 0) {
    std::cout << "All nibbles have bit 7 set (good)" << std::endl;
  }
  std::cout << std::endl;

  // Decode
  uint8_t decoded[256];
  if (!decode6and2(encoded.data(), decoded)) {
    std::cerr << "Decode failed!" << std::endl;
    return 1;
  }

  std::cout << "Decoded sector data (first 32 bytes):" << std::endl;
  for (int i = 0; i < 32; i++) {
    std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)decoded[i] << " ";
    if ((i + 1) % 16 == 0) std::cout << std::endl;
  }
  std::cout << std::dec << std::endl;

  // Compare
  int mismatches = 0;
  for (int i = 0; i < 256; i++) {
    if (sector_data[i] != decoded[i]) {
      if (mismatches < 10) {
        std::cout << "Mismatch at position " << i << ": expected 0x"
                  << std::hex << (int)sector_data[i] << " got 0x" << (int)decoded[i]
                  << std::dec << std::endl;
      }
      mismatches++;
    }
  }

  if (mismatches == 0) {
    std::cout << "SUCCESS: All 256 bytes match!" << std::endl;
    return 0;
  } else {
    std::cout << "FAILED: " << mismatches << " mismatches" << std::endl;
    return 1;
  }
}
