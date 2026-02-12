/*
 * disk_image_builder.hpp - Programmatic disk image builders for testing
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace test {

/**
 * DOS33DiskBuilder - Build minimal DOS 3.3 disk images for testing
 *
 * Creates 143360-byte images with a valid VTOC and catalog.
 */
class DOS33DiskBuilder {
public:
    static constexpr int TRACKS = 35;
    static constexpr int SECTORS = 16;
    static constexpr int SECTOR_SIZE = 256;
    static constexpr int DISK_SIZE = TRACKS * SECTORS * SECTOR_SIZE; // 143360

    DOS33DiskBuilder();

    // Write raw data to a specific sector
    void writeSector(int track, int sector, const uint8_t* data, int len);

    // Add a file to the catalog. fileType: 0x00=T, 0x01=I, 0x02=A, 0x04=B
    // Data is written sequentially to available sectors.
    // Returns true on success.
    bool addFile(const std::string& name, uint8_t fileType,
                 const uint8_t* data, int dataLen, bool locked = false);

    // Get the built disk image
    const std::vector<uint8_t>& build() const { return data_; }
    const uint8_t* data() const { return data_.data(); }
    size_t size() const { return data_.size(); }

private:
    std::vector<uint8_t> data_;

    // Track/sector allocation
    int nextAllocTrack_ = 20;  // Start allocating from track 20
    int nextAllocSector_ = 0;

    // Catalog state
    int catalogTrack_ = 17;
    int catalogSector_ = 15;
    int catalogEntryIndex_ = 0;   // 0-6 entries per sector

    int getSectorOffset(int track, int sector) const;
    bool allocateSector(int& track, int& sector);
    void initVTOC();
    void initCatalog();
};

/**
 * ProDOSDiskBuilder - Build minimal ProDOS disk images for testing
 */
class ProDOSDiskBuilder {
public:
    static constexpr int BLOCK_SIZE = 512;
    static constexpr int DISK_SIZE = 143360; // 280 blocks

    ProDOSDiskBuilder(const std::string& volumeName = "TEST");

    // Write raw data to a block
    void writeBlock(int blockNum, const uint8_t* data, int len);

    // Add a file to the volume directory
    // Returns true on success.
    bool addFile(const std::string& name, uint8_t fileType, uint16_t auxType,
                 const uint8_t* data, int dataLen);

    const std::vector<uint8_t>& build() const { return data_; }
    const uint8_t* data() const { return data_.data(); }
    size_t size() const { return data_.size(); }

private:
    std::vector<uint8_t> data_;
    std::string volumeName_;

    int nextAllocBlock_ = 10;
    int dirEntryCount_ = 0;

    void readBlock(int blockNum, uint8_t* out) const;
    void initVolumeDirectory();
    void initBitmap();
    bool allocateBlock(int& blockNum);
};

/**
 * PascalDiskBuilder - Build minimal Apple Pascal disk images for testing
 */
class PascalDiskBuilder {
public:
    static constexpr int BLOCK_SIZE = 512;
    static constexpr int DISK_SIZE = 143360;

    PascalDiskBuilder(const std::string& volumeName = "TEST");

    bool addFile(const std::string& name, uint8_t fileType,
                 const uint8_t* data, int dataLen);

    const std::vector<uint8_t>& build() const { return data_; }
    const uint8_t* data() const { return data_.data(); }
    size_t size() const { return data_.size(); }

private:
    std::vector<uint8_t> data_;
    std::string volumeName_;
    int nextBlock_ = 6;     // First usable block after directory
    int dirEntryCount_ = 0;

    void initVolumeHeader();
};

/**
 * BlockImageBuilder - Build variable-size HDV block device images
 */
class BlockImageBuilder {
public:
    static constexpr int BLOCK_SIZE = 512;

    explicit BlockImageBuilder(int totalBlocks = 65535);

    void writeBlock(int blockNum, const uint8_t* data, int len);

    const std::vector<uint8_t>& build() const { return data_; }
    const uint8_t* data() const { return data_.data(); }
    size_t size() const { return data_.size(); }

private:
    std::vector<uint8_t> data_;
};

} // namespace test
