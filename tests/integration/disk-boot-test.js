/**
 * Headless Disk Boot Test
 *
 * This test loads the DOS 3.3 disk image and runs the emulator for enough
 * cycles to boot, then checks if "DOS" appears in screen memory.
 *
 * Apple II text screen memory is at $0400-$07FF
 * The screen is 40x24 characters, but the memory is not linear.
 *
 * Run with: node test/disk-boot-test.js
 */

import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, join, resolve } from 'path';
import { createContext, runInContext } from 'vm';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

/**
 * Load the WASM module using vm context (needed because of ES module compatibility)
 */
async function loadWasmModule() {
  const wasmJsPath = join(__dirname, '..', 'public', 'a2e.js');
  const wasmDir = dirname(wasmJsPath);

  const code = readFileSync(wasmJsPath, 'utf8');

  // Create a context with the necessary globals for the WASM module
  const context = {
    module: { exports: {} },
    exports: {},
    globalThis: global,
    console: console,
    require: (await import('module')).createRequire(import.meta.url),
    __dirname: wasmDir,
    __filename: wasmJsPath,
    process: process,
    WebAssembly: WebAssembly,
    URL: URL,
    TextDecoder: TextDecoder,
    setTimeout: setTimeout,
    clearTimeout: clearTimeout,
    performance: { now: () => Date.now() }
  };

  createContext(context);
  runInContext(code, context);

  return context.module.exports;
}

async function runTest() {
  console.log('Loading WASM module...');

  const createA2EModule = await loadWasmModule();

  if (typeof createA2EModule !== 'function') {
    console.error('Failed to load WASM module factory');
    process.exit(1);
  }

  // Create the module instance
  const Module = await createA2EModule();

  console.log('Initializing emulator...');
  Module._init();

  // Load the disk image
  const diskPath = join(__dirname, '..', 'public', 'Apple DOS 3.3 August 1980.dsk');
  console.log(`Loading disk image: ${diskPath}`);

  const diskData = readFileSync(diskPath);
  console.log(`Disk size: ${diskData.length} bytes`);

  // Allocate memory for disk data
  const diskPtr = Module._malloc(diskData.length);
  Module.HEAPU8.set(diskData, diskPtr);

  // Allocate memory for filename
  const filename = 'Apple DOS 3.3 August 1980.dsk';
  const filenamePtr = Module._malloc(filename.length + 1);
  Module.stringToUTF8(filename, filenamePtr, filename.length + 1);

  // Insert the disk
  const inserted = Module._insertDisk(0, diskPtr, diskData.length, filenamePtr);
  console.log(`Disk inserted: ${inserted}`);

  // Free the filename memory
  Module._free(filenamePtr);

  if (!inserted) {
    console.error('Failed to insert disk!');
    process.exit(1);
  }

  // Run emulation for enough cycles to boot
  // DOS 3.3 boot takes about 2-3 seconds at 1.023 MHz
  // That's roughly 2-3 million cycles
  const CYCLES_PER_BATCH = 100000;
  const MAX_BATCHES = 200; // ~20 million cycles (about 20 seconds of emulation)

  console.log('Running emulation...');

  let lastTrack = -1;
  let lastByte = 0;
  let readCount = 0;

  for (let batch = 0; batch < MAX_BATCHES; batch++) {
    Module._runCycles(CYCLES_PER_BATCH);

    // Check disk state
    const track = Module._getDiskTrack(0);
    const motorOn = Module._getDiskMotorOn(0);
    const lastDiskByte = Module._getLastDiskByte();

    if (track !== lastTrack) {
      console.log(`\n  Track changed: ${lastTrack} -> ${track}`);
      lastTrack = track;
    }

    if (lastDiskByte !== lastByte) {
      readCount++;
      if (readCount <= 20) {
        console.log(`  Disk byte: $${lastDiskByte.toString(16).toUpperCase().padStart(2, '0')} (track ${track}, motor: ${motorOn})`);
      }
      lastByte = lastDiskByte;
    }

    // Check for DOS in screen memory every batch
    const found = checkForDOS(Module);
    if (found) {
      const totalCycles = Module._getTotalCycles();
      console.log(`\nSUCCESS: Found "DOS" in screen memory after ${totalCycles} cycles!`);
      dumpScreenMemory(Module);
      Module._free(diskPtr);
      process.exit(0);
    }

    // Progress indicator
    if (batch % 10 === 0) {
      const totalCycles = Module._getTotalCycles();
      const pc = Module._getPC();
      process.stdout.write(`\rCycles: ${totalCycles}, PC: $${pc.toString(16).toUpperCase().padStart(4, '0')}, Track: ${track}, Motor: ${motorOn}`);
    }
  }

  console.log('\n\nFAILED: Did not find "DOS" in screen memory');
  console.log('\nScreen memory dump:');
  dumpScreenMemory(Module);

  // Also dump some debug info
  console.log('\nDebug info:');
  console.log(`PC: $${Module._getPC().toString(16).toUpperCase().padStart(4, '0')}`);
  console.log(`A: $${Module._getA().toString(16).toUpperCase().padStart(2, '0')}`);
  console.log(`X: $${Module._getX().toString(16).toUpperCase().padStart(2, '0')}`);
  console.log(`Y: $${Module._getY().toString(16).toUpperCase().padStart(2, '0')}`);
  console.log(`SP: $${Module._getSP().toString(16).toUpperCase().padStart(2, '0')}`);
  console.log(`Total cycles: ${Module._getTotalCycles()}`);

  // Dump zero page
  console.log('\nZero page ($00-$3F):');
  let zpLine = '';
  for (let i = 0; i < 0x40; i++) {
    if (i % 16 === 0) {
      if (zpLine) console.log(zpLine);
      zpLine = `$${i.toString(16).toUpperCase().padStart(2, '0')}: `;
    }
    zpLine += Module._readMemory(i).toString(16).toUpperCase().padStart(2, '0') + ' ';
  }
  console.log(zpLine);

  // Dump boot sector area ($0800-$08FF)
  console.log('\nBoot sector memory ($0800-$08FF):');
  for (let row = 0; row < 16; row++) {
    const addr = 0x0800 + row * 16;
    let line = `$${addr.toString(16).toUpperCase().padStart(4, '0')}: `;
    let ascii = '';
    for (let col = 0; col < 16; col++) {
      const byte = Module._readMemory(addr + col);
      line += byte.toString(16).toUpperCase().padStart(2, '0') + ' ';
      ascii += (byte >= 0x20 && byte < 0x7F) ? String.fromCharCode(byte) : '.';
    }
    console.log(line + ' ' + ascii);
  }

  Module._free(diskPtr);
  process.exit(1);
}

/**
 * Check if "DOS" appears in screen memory
 * Apple II screen memory is at $0400-$07FF
 */
function checkForDOS(Module) {
  // Apple II character codes for "DOS" (high bit may be set)
  // D = 0x44 or 0xC4, O = 0x4F or 0xCF, S = 0x53 or 0xD3

  // Scan all of screen memory
  for (let addr = 0x0400; addr < 0x0800 - 2; addr++) {
    const c1 = Module._readMemory(addr) & 0x7F;
    const c2 = Module._readMemory(addr + 1) & 0x7F;
    const c3 = Module._readMemory(addr + 2) & 0x7F;

    if (c1 === 0x44 && c2 === 0x4F && c3 === 0x53) { // "DOS"
      return true;
    }
  }

  return false;
}

/**
 * Dump screen memory as text
 */
function dumpScreenMemory(Module) {
  // Apple II screen memory layout is complex
  // Each row is split into 3 groups of 8 rows
  const rowBases = [
    0x0400, 0x0480, 0x0500, 0x0580, 0x0600, 0x0680, 0x0700, 0x0780,
    0x0428, 0x04A8, 0x0528, 0x05A8, 0x0628, 0x06A8, 0x0728, 0x07A8,
    0x0450, 0x04D0, 0x0550, 0x05D0, 0x0650, 0x06D0, 0x0750, 0x07D0
  ];

  console.log('┌────────────────────────────────────────┐');

  for (let row = 0; row < 24; row++) {
    let line = '│';
    const base = rowBases[row];

    for (let col = 0; col < 40; col++) {
      const char = Module._readMemory(base + col);
      const ascii = appleCharToAscii(char);
      line += ascii;
    }

    console.log(line + '│');
  }

  console.log('└────────────────────────────────────────┘');
}

/**
 * Convert Apple II character code to ASCII
 */
function appleCharToAscii(char) {
  const c = char & 0x7F;

  if (c >= 0x20 && c < 0x60) {
    return String.fromCharCode(c);
  } else if (c >= 0x00 && c < 0x20) {
    return String.fromCharCode(c + 0x40);
  } else if (c >= 0x60) {
    return String.fromCharCode(c);
  }

  return ' ';
}

runTest().catch(err => {
  console.error('Test error:', err);
  process.exit(1);
});
