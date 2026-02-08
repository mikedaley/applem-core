/*
 * block_device.hpp - Block-oriented storage device for SmartPort
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace a2e {

class BlockDevice {
public:
    static constexpr size_t BLOCK_SIZE = 512;
    static constexpr size_t MAX_BLOCKS = 65535;

    BlockDevice() = default;
    ~BlockDevice() = default;

    bool load(const uint8_t* data, size_t size, const std::string& filename);
    bool readBlock(uint16_t blockNum, uint8_t* buffer) const;
    bool writeBlock(uint16_t blockNum, const uint8_t* buffer);

    uint16_t getTotalBlocks() const { return totalBlocks_; }
    bool isWriteProtected() const { return writeProtected_; }
    bool isModified() const { return modified_; }
    bool isLoaded() const { return !data_.empty(); }
    const std::string& getFilename() const { return filename_; }

    const uint8_t* exportData(size_t* size) const;
    const uint8_t* getBlockData(size_t* size) const;

    size_t getStateSize() const;
    size_t serialize(uint8_t* buffer, size_t maxSize) const;
    size_t deserialize(const uint8_t* buffer, size_t size);

    void eject() {
        data_.clear();
        totalBlocks_ = 0;
        modified_ = false;
        writeProtected_ = false;
        filename_.clear();
        dataOffset_ = 0;
    }

private:
    std::vector<uint8_t> data_;
    uint16_t totalBlocks_ = 0;
    bool writeProtected_ = false;
    bool modified_ = false;
    std::string filename_;
    size_t dataOffset_ = 0; // offset to block data (non-zero for 2IMG)
};

} // namespace a2e
