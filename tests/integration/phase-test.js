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
  const createA2EModule = await loadWasmModule();
  const Module = await createA2EModule();
  Module._init();

  const diskPath = join(__dirname, '..', 'public', 'Apple DOS 3.3 August 1980.dsk');
  const diskData = readFileSync(diskPath);
  const diskPtr = Module._malloc(diskData.length);
  Module.HEAPU8.set(diskData, diskPtr);
  const filename = 'test.dsk';
  const filenamePtr = Module._malloc(filename.length + 1);
  Module.stringToUTF8(filename, filenamePtr, filename.length + 1);
  Module._insertDisk(0, diskPtr, diskData.length, filenamePtr);
  Module._free(filenamePtr);

  console.log('=== PHASE/HEAD MOVEMENT TEST ===\n');

  // Manually test phase switching by writing to soft switches
  // Slot 6 soft switches: $C0E0-$C0EF
  // Phase 0: $C0E0 (off), $C0E1 (on)
  // Phase 1: $C0E2 (off), $C0E3 (on)
  // Phase 2: $C0E4 (off), $C0E5 (on)
  // Phase 3: $C0E6 (off), $C0E7 (on)
  // Motor: $C0E8 (off), $C0E9 (on)

  const getTrack = () => Module._getDiskTrack(0);
  const getQuarterTrack = () => Module._getDiskHeadPosition(0);
  const getPhaseStates = () => Module._getDiskPhase(0);

  console.log('Initial state:');
  console.log('  Track: ' + getTrack() + ', Quarter-track: ' + getQuarterTrack() + ', Phases: ' + getPhaseStates().toString(2).padStart(4, '0'));

  // Turn on motor
  Module._readMemory(0xC0E9);  // Motor on
  Module._runCycles(100);

  console.log('\nAfter motor on:');
  console.log('  Motor: ' + Module._getDiskMotorOn(0));

  // Test stepping from track 0 to track 1
  // Starting at track 0 (quarter-track 0), phase 0 should be on
  // To move to track 1 (quarter-track 4):
  // Phase 0 -> Phase 1 (+2 QT) -> Phase 2 (+2 QT) = quarter-track 4

  console.log('\n=== Testing step from track 0 to track 1 ===');
  console.log('Expected sequence: Phase 0 -> 1 -> 2 (quarter-tracks 0 -> 2 -> 4)');

  // Make sure we start at track 0
  Module._readMemory(0xC0E1);  // Phase 0 on
  Module._runCycles(10);
  console.log('\nPhase 0 on:');
  console.log('  Quarter-track: ' + getQuarterTrack() + ', Phases: ' + getPhaseStates().toString(2).padStart(4, '0'));

  // Step to phase 1
  Module._readMemory(0xC0E0);  // Phase 0 off
  Module._readMemory(0xC0E3);  // Phase 1 on
  Module._runCycles(10);
  console.log('\nPhase 1 on (phase 0 off):');
  console.log('  Quarter-track: ' + getQuarterTrack() + ', Track: ' + getTrack() + ', Phases: ' + getPhaseStates().toString(2).padStart(4, '0'));

  // Step to phase 2
  Module._readMemory(0xC0E2);  // Phase 1 off
  Module._readMemory(0xC0E5);  // Phase 2 on
  Module._runCycles(10);
  console.log('\nPhase 2 on (phase 1 off):');
  console.log('  Quarter-track: ' + getQuarterTrack() + ', Track: ' + getTrack() + ', Phases: ' + getPhaseStates().toString(2).padStart(4, '0'));

  // Should now be at track 1 (quarter-track 4)
  console.log('\n=== Expected: Quarter-track 4, Track 1 ===');

  // Test stepping back to track 0
  console.log('\n=== Testing step from track 1 back to track 0 ===');
  console.log('Expected sequence: Phase 2 -> 1 -> 0 (quarter-tracks 4 -> 2 -> 0)');

  // Step to phase 1
  Module._readMemory(0xC0E4);  // Phase 2 off
  Module._readMemory(0xC0E3);  // Phase 1 on
  Module._runCycles(10);
  console.log('\nPhase 1 on (phase 2 off):');
  console.log('  Quarter-track: ' + getQuarterTrack() + ', Track: ' + getTrack() + ', Phases: ' + getPhaseStates().toString(2).padStart(4, '0'));

  // Step to phase 0
  Module._readMemory(0xC0E2);  // Phase 1 off
  Module._readMemory(0xC0E1);  // Phase 0 on
  Module._runCycles(10);
  console.log('\nPhase 0 on (phase 1 off):');
  console.log('  Quarter-track: ' + getQuarterTrack() + ', Track: ' + getTrack() + ', Phases: ' + getPhaseStates().toString(2).padStart(4, '0'));

  console.log('\n=== Expected: Quarter-track 0, Track 0 ===');

  // Now test multiple track stepping
  console.log('\n=== Testing multiple track step (track 0 -> track 5) ===');
  
  // Reset to track 0
  Module._readMemory(0xC0E1);  // Phase 0 on
  Module._runCycles(10);
  
  for (let targetTrack = 1; targetTrack <= 5; targetTrack++) {
    // Step through phases: 0 -> 1 -> 2 -> 3 -> 0 (repeats)
    const phases = [0xC0E1, 0xC0E3, 0xC0E5, 0xC0E7];  // Phase on addresses
    const phaseOff = [0xC0E0, 0xC0E2, 0xC0E4, 0xC0E6];  // Phase off addresses
    
    // Calculate current phase from quarter-track
    const qt = getQuarterTrack();
    const currentPhase = (qt / 2) % 4;
    const nextPhase = (currentPhase + 1) % 4;
    
    // Turn off current phase
    Module._readMemory(phaseOff[currentPhase]);
    // Turn on next phase
    Module._readMemory(phases[nextPhase]);
    Module._runCycles(10);
    
    // Step again to complete the track
    const phase2 = (nextPhase + 1) % 4;
    Module._readMemory(phaseOff[nextPhase]);
    Module._readMemory(phases[phase2]);
    Module._runCycles(10);
    
    console.log('After stepping to track ' + targetTrack + ': Quarter-track=' + getQuarterTrack() + ', Track=' + getTrack());
  }

  Module._free(diskPtr);
}

runTest().catch(err => { console.error(err); process.exit(1); });
