/**
 * Klaus Dormann 6502 Functional Test
 *
 * This test runs the Klaus Dormann 6502 functional test against the
 * web-a2e CPU6502 emulator to verify correct implementation of the
 * basic 6502 instruction set.
 *
 * Test binary source: https://github.com/Klaus2m5/6502_65C02_functional_tests
 */

#include "cpu6502.hpp"
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

// 64KB memory for the test
static std::array<uint8_t, 65536> memory{};

// Memory access callbacks
uint8_t readMemory(uint16_t address) { return memory[address]; }
void writeMemory(uint16_t address, uint8_t value) { memory[address] = value; }

int main(int argc, char* argv[]) {
    std::cout << "Klaus Dormann 6502 Functional Test\n";
    std::cout << "===================================\n\n";

    // Determine test ROM path
    std::string testPath = "tests/klaus/6502_functional_test.bin";
    if (argc > 1) {
        testPath = argv[1];
    }

    // Load the test ROM
    std::ifstream testFile(testPath, std::ios::binary);
    if (!testFile) {
        std::cerr << "ERROR: Could not open " << testPath << "\n";
        std::cerr << "Usage: " << argv[0] << " [path_to_test.bin]\n";
        return 1;
    }

    testFile.read(reinterpret_cast<char*>(memory.data()), 65536);
    testFile.close();

    std::cout << "Test ROM loaded: " << testPath << "\n\n";

    // Create NMOS 6502 CPU instance (the functional test targets NMOS)
    // Using CMOS_65C02 variant to test our 65C02 implementation with basic opcodes
    a2e::CPU6502 cpu(readMemory, writeMemory, a2e::CPUVariant::CMOS_65C02);

    // The test starts at $0400
    cpu.setPC(0x0400);
    cpu.setP(0x00);  // Clear all flags

    std::cout << "Starting test at PC=$0400\n";
    std::cout << "This test runs in an infinite loop when successful.\n";
    std::cout << "If it gets stuck elsewhere, the PC shows where it failed.\n\n";

    uint16_t lastPC = 0;
    uint32_t stuckCount = 0;
    const uint32_t MAX_STUCK = 100000;

    // Track restart attempts
    uint16_t restartCount = 0;
    uint16_t lastRestartPC = 0;
    std::vector<uint16_t> recentPCs;
    const size_t RECENT_PC_COUNT = 20;

    uint64_t instructionCount = 0;
    // Known success loop address for 6502 functional test
    const uint16_t SUCCESS_PC = 0x3469;

    while (true) {
        uint16_t currentPC = cpu.getPC();

        // Check if we've reached the success loop
        if (currentPC == SUCCESS_PC) {
            std::cout << "\n\033[32m✓ SUCCESS!\033[0m\n";
            std::cout << "Test passed after " << instructionCount << " instructions\n";
            std::cout << "Total cycles: " << cpu.getTotalCycles() << "\n";
            std::cout << "CPU is in the success loop at $" << std::hex
                      << std::setw(4) << std::setfill('0') << currentPC << "\n";
            return 0;
        }

        // Detect if we're restarting the test
        if (currentPC == 0x0400 && lastPC != 0x0400) {
            restartCount++;
            if (restartCount > 3) {
                std::cout << "\n\033[31m✗ FAILED!\033[0m\n";
                std::cout << "Test is restarting repeatedly (restart #" << restartCount << ")\n";
                std::cout << "Last PC before restart: $" << std::hex
                          << std::setw(4) << std::setfill('0') << lastRestartPC << "\n";

                std::cout << "\nCPU State:\n";
                std::cout << "  A:  $" << std::setw(2) << static_cast<int>(cpu.getA()) << "\n";
                std::cout << "  X:  $" << std::setw(2) << static_cast<int>(cpu.getX()) << "\n";
                std::cout << "  Y:  $" << std::setw(2) << static_cast<int>(cpu.getY()) << "\n";
                std::cout << "  SP: $" << std::setw(2) << static_cast<int>(cpu.getSP()) << "\n";
                std::cout << "  P:  $" << std::setw(2) << static_cast<int>(cpu.getP()) << "\n";
                std::cout << "  Total instructions: " << std::dec << instructionCount << "\n";

                // Show recent PC history
                std::cout << "\nRecent PC history:\n";
                for (size_t i = 0; i < recentPCs.size(); i++) {
                    std::cout << "  $" << std::hex << std::setw(4) << std::setfill('0') << recentPCs[i];
                    if (i == recentPCs.size() - 1) std::cout << " <-- most recent";
                    std::cout << "\n";
                }

                return 1;
            }
            lastRestartPC = lastPC;
        }

        // Track recent PCs
        recentPCs.push_back(currentPC);
        if (recentPCs.size() > RECENT_PC_COUNT) {
            recentPCs.erase(recentPCs.begin());
        }

        // Check if CPU is stuck
        if (currentPC == lastPC) {
            stuckCount++;
            if (stuckCount > MAX_STUCK) {
                std::cout << "\n\033[31m✗ FAILED!\033[0m\n";
                std::cout << "CPU stuck at PC=$" << std::hex << std::setw(4)
                          << std::setfill('0') << currentPC << "\n";

                std::cout << "\nCPU State:\n";
                std::cout << "  A:  $" << std::setw(2) << static_cast<int>(cpu.getA()) << "\n";
                std::cout << "  X:  $" << std::setw(2) << static_cast<int>(cpu.getX()) << "\n";
                std::cout << "  Y:  $" << std::setw(2) << static_cast<int>(cpu.getY()) << "\n";
                std::cout << "  SP: $" << std::setw(2) << static_cast<int>(cpu.getSP()) << "\n";
                std::cout << "  P:  $" << std::setw(2) << static_cast<int>(cpu.getP()) << "\n";
                std::cout << "  Total instructions: " << std::dec << instructionCount << "\n";

                // Show surrounding memory
                std::cout << "\nMemory around PC:\n";
                for (int i = -5; i <= 5; i++) {
                    uint16_t addr = static_cast<uint16_t>(currentPC + i);
                    std::cout << "  $" << std::hex << std::setw(4) << std::setfill('0') << addr
                              << ": $" << std::setw(2) << static_cast<int>(memory[addr]);
                    if (i == 0) std::cout << " <-- PC";
                    std::cout << "\n";
                }

                return 1;
            }
        } else {
            stuckCount = 0;
            lastPC = currentPC;
        }

        // Progress indicator
        if (instructionCount % 1000000 == 0 && instructionCount > 0) {
            std::cout << "Progress: " << std::dec << instructionCount
                      << " instructions, PC=$" << std::hex << std::setw(4)
                      << std::setfill('0') << currentPC << "\r" << std::flush;
        }

        // Execute one instruction
        try {
            cpu.executeInstruction();
            instructionCount++;
        } catch (...) {
            std::cout << "\n\033[31m✗ EXCEPTION during execution at PC=$" << std::hex
                      << std::setw(4) << std::setfill('0') << currentPC << "\033[0m\n";
            return 1;
        }

        // Safety limit
        if (instructionCount > 500000000) {
            std::cout << "\n\033[31m✗ TIMEOUT: Exceeded 500M instructions\033[0m\n";
            std::cout << "Final PC: $" << std::hex << std::setw(4)
                      << std::setfill('0') << currentPC << "\n";
            return 1;
        }
    }

    return 0;
}
