/**
 * Nibble Read Test - Track actual disk reads during boot
 */

import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';
import { createContext, runInContext } from 'vm';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

async function loadWasmModule() {
  const wasmJsPath = join(__dirname, '..', 'public', 'a2e.js');
  const wasmDir = dirname(wasmJsPath);
  const code = readFileSync(wasmJsPath, 'utf8');

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
  const Module = await createA2EModule();

  console.log('Initializing emulator...');
  Module._init();

  // Load disk
  const diskPath = join(__dirname, '..', 'public', 'Apple DOS 3.3 August 1980.dsk');
  const diskData = readFileSync(diskPath);
  const diskPtr = Module._malloc(diskData.length);
  Module.HEAPU8.set(diskData, diskPtr);
  const filename = 'test.dsk';
  const filenamePtr = Module._malloc(filename.length + 1);
  Module.stringToUTF8(filename, filenamePtr, filename.length + 1);
  Module._insertDisk(0, diskPtr, diskData.length, filenamePtr);
  Module._free(filenamePtr);

  // Get track 0 nibbles for comparison
  const track0Nibbles = [];
  const count = Module._getTrackNibbleCount(0, 0);
  for (let i = 0; i < count; i++) {
    track0Nibbles.push(Module._getTrackNibble(0, 0, i));
  }

  console.log('Track 0 has ' + count + ' nibbles');

  // Find first sync run and address prologue
  let syncEnd = 0;
  for (let i = 0; i < count; i++) {
    if (track0Nibbles[i] !== 0xFF) {
      syncEnd = i;
      break;
    }
  }
  console.log('Sync bytes end at position ' + syncEnd);

  let nibblesStr = '';
  for (let i = syncEnd; i < syncEnd + 10; i++) {
    nibblesStr += track0Nibbles[i].toString(16).toUpperCase().padStart(2, '0') + ' ';
  }
  console.log('Nibbles at sync end: ' + nibblesStr);

  // Run to disk boot ROM
  console.log('\nRunning to disk boot ROM...');
  for (let i = 0; i < 2000; i++) {
    Module._runCycles(1000);
    const pc = Module._getPC();
    if (pc >= 0xC600 && pc < 0xC700) {
      console.log('Entered disk boot ROM at $' + pc.toString(16).toUpperCase() + ' after ' + Module._getTotalCycles() + ' cycles');
      break;
    }
  }

  // Track nibble reads during boot until screen text appears
  console.log('\nTracking disk reads during boot (waiting for screen text)...');
  let lastLatch = 0;
  let lastPos = Module._getCurrentNibblePosition(0);
  let lastTrack = Module._getDiskTrack(0);
  const readLog = [];
  let crashPC = 0;
  let screenTextFound = false;

  // Helper to check if screen has "DOS" text (indicates disk boot success)
  function hasDOSText() {
    // Look for "DOS" in screen memory
    for (let addr = 0x0400; addr < 0x0800 - 2; addr++) {
      const c1 = Module._readMemory(addr) & 0x7F;
      const c2 = Module._readMemory(addr + 1) & 0x7F;
      const c3 = Module._readMemory(addr + 2) & 0x7F;
      if (c1 === 0x44 && c2 === 0x4F && c3 === 0x53) {  // "DOS"
        return true;
      }
    }
    return false;
  }

  // Helper to dump screen contents
  function dumpScreen() {
    const rowBases = [
      0x0400, 0x0480, 0x0500, 0x0580, 0x0600, 0x0680, 0x0700, 0x0780,
      0x0428, 0x04A8, 0x0528, 0x05A8, 0x0628, 0x06A8, 0x0728, 0x07A8,
      0x0450, 0x04D0, 0x0550, 0x05D0, 0x0650, 0x06D0, 0x0750, 0x07D0
    ];

    console.log('Screen contents:');
    for (let row = 0; row < 24; row++) {
      let line = '';
      const base = rowBases[row];
      for (let col = 0; col < 40; col++) {
        const c = Module._readMemory(base + col) & 0x7F;
        if (c >= 0x20 && c < 0x7F) {
          line += String.fromCharCode(c);
        } else if (c >= 0x00 && c < 0x20) {
          line += String.fromCharCode(c + 0x40);
        } else {
          line += ' ';
        }
      }
      console.log('|' + line + '|');
    }
  }

  const MAX_BATCHES = 50000;  // Run for much longer
  for (let batch = 0; batch < MAX_BATCHES; batch++) {
    const pcBefore = Module._getPC();
    Module._runCycles(100);
    const pcAfter = Module._getPC();

    const pos = Module._getCurrentNibblePosition(0);
    const latch = Module._getLastDiskByte(0);
    const track = Module._getDiskTrack(0);
    const motor = Module._getDiskMotorOn(0);

    // Detect crash
    if (pcAfter === 0 && pcBefore !== 0 && pcBefore !== 0xFFFC) {
      console.log('\n*** CRASH DETECTED at cycle ' + Module._getTotalCycles() + ' ***');
      console.log('Previous PC: $' + pcBefore.toString(16).toUpperCase());
      console.log('Current Track: ' + track + ', Motor: ' + motor);
      dumpScreen();
      crashPC = pcBefore;
      break;
    }

    // Track nibble reads
    if (latch !== lastLatch && motor) {
      if (readLog.length < 500) {
        // Track 0 comparison
        let expectedNibble = 0;
        if (track === 0) {
          // Get what nibble should be at the PREVIOUS position (since readNibble advances)
          const readPos = (pos - 1 + count) % count;
          expectedNibble = track0Nibbles[readPos] | 0x80;
        }

        readLog.push({
          cycle: Module._getTotalCycles(),
          track: track,
          pos: pos,
          readPos: (pos - 1 + count) % count,
          latch: latch,
          expected: expectedNibble
        });
      }
      lastLatch = latch;
    }

    lastPos = pos;
    lastTrack = track;

    // Check for DOS text every 1000 batches
    if (batch % 1000 === 0) {
      if (hasDOSText()) {
        console.log('\nDOS text detected after ' + Module._getTotalCycles() + ' cycles!');
        dumpScreen();
        screenTextFound = true;
        break;
      }
      // Progress indicator
      process.stdout.write('\rCycles: ' + Module._getTotalCycles() + ', Track: ' + track);
    }
  }

  if (!screenTextFound && crashPC === 0) {
    console.log('\nTimeout: No screen text after ' + Module._getTotalCycles() + ' cycles');
  }

  console.log('\nCaptured ' + readLog.length + ' disk byte changes');

  // Show first 30 reads
  console.log('\nFirst 30 disk byte changes:');
  for (let i = 0; i < Math.min(30, readLog.length); i++) {
    const r = readLog[i];
    let status = '';
    if (r.track === 0 && r.expected !== 0) {
      status = r.latch === r.expected ? ' OK' : ' MISMATCH (expected $' + r.expected.toString(16).toUpperCase() + ')';
    }
    console.log('  cycle=' + r.cycle.toString().padStart(8) + ' track=' + r.track + ' pos=' + r.readPos.toString().padStart(4) + ' latch=$' + r.latch.toString(16).toUpperCase().padStart(2, '0') + status);
  }

  // Check for mismatches on track 0
  const track0Reads = readLog.filter(r => r.track === 0 && r.expected !== 0);
  const mismatches = track0Reads.filter(r => r.latch !== r.expected);

  if (mismatches.length > 0) {
    console.log('\n*** Found ' + mismatches.length + ' mismatches on track 0! ***');
    for (let i = 0; i < Math.min(10, mismatches.length); i++) {
      const m = mismatches[i];
      console.log('  pos=' + m.readPos + ': expected=$' + m.expected.toString(16).toUpperCase() + ' actual=$' + m.latch.toString(16).toUpperCase());
    }
  } else if (track0Reads.length > 0) {
    console.log('\nAll ' + track0Reads.length + ' track 0 nibble reads matched!');
  }

  // Look for patterns in the read positions
  console.log('\nNibble position progression (first 50):');
  const positions = [];
  for (let i = 0; i < Math.min(50, readLog.length); i++) {
    positions.push(readLog[i].readPos);
  }
  console.log(positions.join(' -> '));

  Module._free(diskPtr);
}

runTest().catch(err => {
  console.error('Error:', err);
  process.exit(1);
});
