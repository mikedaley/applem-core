/*
 * block_device.cpp - Block-oriented storage device for SmartPort
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "block_device.hpp"
#include <cstring>
#include <algorithm>

namespace a2e {

bool BlockDevice::load(const uint8_t* data, size_t size, const std::string& filename) {
    if (!data || size == 0) return false;

    filename_ = filename;
    modified_ = false;
    writeProtected_ = false;
    dataOffset_ = 0;

    // Check for 2IMG format (64-byte header with "2IMG" magic)
    if (size > 64 && data[0] == '2' && data[1] == 'I' && data[2] == 'M' && data[3] == 'G') {
        // Parse 2IMG header
        uint32_t headerSize = data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);
        uint32_t dataOffset = data[24] | (data[25] << 8) | (data[26] << 16) | (data[27] << 24);
        uint32_t dataLength = data[28] | (data[29] << 8) | (data[30] << 16) | (data[31] << 24);
        uint32_t flags = data[16] | (data[17] << 8) | (data[18] << 16) | (data[19] << 24);

        // Validate
        if (headerSize < 64 || dataOffset >= size) return false;
        if (dataOffset + dataLength > size) return false;

        // Image format in header byte 12: 0=DOS order, 1=ProDOS order, 2=nibble
        uint32_t imageFormat = data[12] | (data[13] << 8) | (data[14] << 16) | (data[15] << 24);
        if (imageFormat == 2) return false; // nibble format not supported as block device

        writeProtected_ = (flags & 0x80000000) != 0;

        // Block count from header
        uint32_t blockCount = data[20] | (data[21] << 8) | (data[22] << 16) | (data[23] << 24);
        if (blockCount == 0 && dataLength > 0) {
            blockCount = dataLength / BLOCK_SIZE;
        }
        if (blockCount > MAX_BLOCKS) blockCount = MAX_BLOCKS;

        dataOffset_ = dataOffset;
        totalBlocks_ = static_cast<uint16_t>(blockCount);

        // Store the entire file (header + data) so we can export it back
        data_.assign(data, data + size);
        return true;
    }

    // Raw ProDOS order (.hdv, .po) - just raw blocks
    if (size < BLOCK_SIZE) return false;

    uint32_t blockCount = size / BLOCK_SIZE;
    if (blockCount > MAX_BLOCKS) blockCount = MAX_BLOCKS;

    totalBlocks_ = static_cast<uint16_t>(blockCount);
    dataOffset_ = 0;
    data_.assign(data, data + size);
    return true;
}

bool BlockDevice::readBlock(uint16_t blockNum, uint8_t* buffer) const {
    if (!buffer || blockNum >= totalBlocks_) return false;

    size_t offset = dataOffset_ + static_cast<size_t>(blockNum) * BLOCK_SIZE;
    if (offset + BLOCK_SIZE > data_.size()) return false;

    std::memcpy(buffer, data_.data() + offset, BLOCK_SIZE);
    return true;
}

bool BlockDevice::writeBlock(uint16_t blockNum, const uint8_t* buffer) {
    if (!buffer || blockNum >= totalBlocks_ || writeProtected_) return false;

    size_t offset = dataOffset_ + static_cast<size_t>(blockNum) * BLOCK_SIZE;
    if (offset + BLOCK_SIZE > data_.size()) return false;

    std::memcpy(data_.data() + offset, buffer, BLOCK_SIZE);
    modified_ = true;
    return true;
}

const uint8_t* BlockDevice::exportData(size_t* size) const {
    if (data_.empty()) {
        if (size) *size = 0;
        return nullptr;
    }
    if (size) *size = data_.size();
    return data_.data();
}

size_t BlockDevice::getStateSize() const {
    // flags(1) + totalBlocks(2) + dataOffset(4) + filenameLen(2) + filename + dataSize(4) + data
    return 1 + 2 + 4 + 2 + filename_.size() + 4 + data_.size();
}

size_t BlockDevice::serialize(uint8_t* buffer, size_t maxSize) const {
    size_t needed = getStateSize();
    if (maxSize < needed) return 0;

    size_t offset = 0;

    // Flags: bit 0 = loaded, bit 1 = writeProtected, bit 2 = modified
    uint8_t flags = 0;
    if (!data_.empty()) flags |= 0x01;
    if (writeProtected_) flags |= 0x02;
    if (modified_) flags |= 0x04;
    buffer[offset++] = flags;

    // Total blocks (LE16)
    buffer[offset++] = totalBlocks_ & 0xFF;
    buffer[offset++] = (totalBlocks_ >> 8) & 0xFF;

    // Data offset (LE32)
    uint32_t doff = static_cast<uint32_t>(dataOffset_);
    buffer[offset++] = doff & 0xFF;
    buffer[offset++] = (doff >> 8) & 0xFF;
    buffer[offset++] = (doff >> 16) & 0xFF;
    buffer[offset++] = (doff >> 24) & 0xFF;

    // Filename
    uint16_t fnLen = static_cast<uint16_t>(filename_.size());
    buffer[offset++] = fnLen & 0xFF;
    buffer[offset++] = (fnLen >> 8) & 0xFF;
    if (fnLen > 0) {
        std::memcpy(buffer + offset, filename_.data(), fnLen);
        offset += fnLen;
    }

    // Data
    uint32_t dataSize = static_cast<uint32_t>(data_.size());
    buffer[offset++] = dataSize & 0xFF;
    buffer[offset++] = (dataSize >> 8) & 0xFF;
    buffer[offset++] = (dataSize >> 16) & 0xFF;
    buffer[offset++] = (dataSize >> 24) & 0xFF;
    if (dataSize > 0) {
        std::memcpy(buffer + offset, data_.data(), dataSize);
        offset += dataSize;
    }

    return offset;
}

size_t BlockDevice::deserialize(const uint8_t* buffer, size_t size) {
    if (size < 13) return 0; // minimum: flags + blocks + offset + fnLen + dataSize

    size_t offset = 0;

    uint8_t flags = buffer[offset++];
    bool loaded = (flags & 0x01) != 0;
    writeProtected_ = (flags & 0x02) != 0;
    modified_ = (flags & 0x04) != 0;

    totalBlocks_ = buffer[offset] | (buffer[offset + 1] << 8);
    offset += 2;

    dataOffset_ = buffer[offset] | (buffer[offset + 1] << 8) |
                  (buffer[offset + 2] << 16) | (buffer[offset + 3] << 24);
    offset += 4;

    uint16_t fnLen = buffer[offset] | (buffer[offset + 1] << 8);
    offset += 2;
    if (offset + fnLen > size) return 0;
    if (fnLen > 0) {
        filename_.assign(reinterpret_cast<const char*>(buffer + offset), fnLen);
        offset += fnLen;
    } else {
        filename_.clear();
    }

    if (offset + 4 > size) return 0;
    uint32_t dataSize = buffer[offset] | (buffer[offset + 1] << 8) |
                        (buffer[offset + 2] << 16) | (buffer[offset + 3] << 24);
    offset += 4;

    if (loaded && dataSize > 0) {
        if (offset + dataSize > size) return 0;
        data_.assign(buffer + offset, buffer + offset + dataSize);
        offset += dataSize;
    } else {
        data_.clear();
        totalBlocks_ = 0;
    }

    return offset;
}

} // namespace a2e
