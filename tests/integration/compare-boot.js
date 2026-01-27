/**
 * Compare boot sector loading between disk image and memory
 */
import { readFileSync } from 'fs';
import { createContext, runInContext } from 'vm';

async function loadWasmModule() {
  const wasmJsPath = '/Users/michaeldaley/Source/web-a2e/public/a2e.js';

  const code = readFileSync(wasmJsPath, 'utf8');

  const context = {
    module: { exports: {} },
    exports: {},
    globalThis: global,
    console: console,
    require: (await import('module')).createRequire(import.meta.url),
    __dirname: '/Users/michaeldaley/Source/web-a2e/public',
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

async function main() {
  console.log('Loading WASM module...');
  const createA2EModule = await loadWasmModule();
  const Module = await createA2EModule();

  console.log('Initializing emulator...');
  Module._init();

  // Load the disk image
  const diskPath = '/Users/michaeldaley/Source/web-a2e/public/Apple DOS 3.3 August 1980.dsk';
  const diskData = readFileSync(diskPath);

  // Allocate memory for disk data
  const diskPtr = Module._malloc(diskData.length);
  Module.HEAPU8.set(diskData, diskPtr);

  // Allocate memory for filename
  const filename = 'Apple DOS 3.3 August 1980.dsk';
  const filenamePtr = Module._malloc(filename.length + 1);
  Module.stringToUTF8(filename, filenamePtr, filename.length + 1);

  // Insert the disk
  const inserted = Module._insertDisk(0, diskPtr, diskData.length, filenamePtr);
  Module._free(filenamePtr);

  if (!inserted) {
    console.error('Failed to insert disk!');
    process.exit(1);
  }

  // Run boot cycles in batches to see progress
  console.log('Running boot cycles...');

  // Run first to get to the interesting part
  Module._runCycles(4000000);
  console.log(`At 4M cycles: PC=$${Module._getPC().toString(16).toUpperCase().padStart(4, '0')}`);

  // Now run in smaller batches to catch the crash
  for (let batch = 0; batch < 20; batch++) {
    Module._runCycles(50000);
    const pc = Module._getPC();
    const track = Module._getDiskTrack(0);
    const cycles = Module._getTotalCycles();
    console.log(`${cycles} cycles: PC=$${pc.toString(16).toUpperCase().padStart(4, '0')}, Track=${track}`);

    // Check if crashed
    if (pc === 0) {
      console.log('CRASHED at $0000!');

      // Dump memory around $3900-$397F (boot stage 2 code)
      console.log('\nBoot stage 2 code ($3900-$397F):');
      let line = '';
      for (let i = 0; i < 128; i++) {
        const byte = Module._readMemory(0x3900 + i);
        line += byte.toString(16).padStart(2, '0').toUpperCase() + ' ';
        if ((i + 1) % 16 === 0) {
          console.log('$' + (0x3900 + i - 15).toString(16).toUpperCase() + ': ' + line);
          line = '';
        }
      }

      // Dump memory around where crash happened ($3E50-$3EAF)
      console.log('\nCode near crash location ($3E50-$3EAF):');
      line = '';
      for (let i = 0; i < 96; i++) {
        const byte = Module._readMemory(0x3E50 + i);
        line += byte.toString(16).padStart(2, '0').toUpperCase() + ' ';
        if ((i + 1) % 16 === 0) {
          console.log('$' + (0x3E50 + i - 15).toString(16).toUpperCase() + ': ' + line);
          line = '';
        }
      }

      // Dump stack area
      console.log('\nStack area ($0100-$01FF):');
      const sp = Module._getSP();
      console.log(`SP = $${sp.toString(16).toUpperCase().padStart(2, '0')}`);
      line = '';
      for (let i = sp; i <= 0xFF; i++) {
        const byte = Module._readMemory(0x0100 + i);
        line += byte.toString(16).padStart(2, '0').toUpperCase() + ' ';
      }
      console.log('Stack: ' + line);

      // Check what's at $0236 (the return address on the stack)
      console.log('\nMemory at $0230-$024F:');
      line = '';
      for (let i = 0; i < 32; i++) {
        const byte = Module._readMemory(0x0230 + i);
        line += byte.toString(16).padStart(2, '0').toUpperCase() + ' ';
        if ((i + 1) % 16 === 0) {
          console.log('$' + (0x0230 + i - 15).toString(16).toUpperCase() + ': ' + line);
          line = '';
        }
      }

      // Check what's at $0000 (where we crashed)
      console.log('\nMemory at $0000-$001F:');
      line = '';
      for (let i = 0; i < 32; i++) {
        const byte = Module._readMemory(i);
        line += byte.toString(16).padStart(2, '0').toUpperCase() + ' ';
        if ((i + 1) % 16 === 0) {
          console.log('$' + (i - 15).toString(16).toUpperCase() + ': ' + line);
          line = '';
        }
      }

      break;
    }
  }

  // Read memory at $0800 to see if boot sector loaded
  console.log('\nMemory at $0800 (boot sector destination):');
  let line = '';
  for (let i = 0; i < 32; i++) {
    const byte = Module._readMemory(0x0800 + i);
    line += byte.toString(16).padStart(2, '0').toUpperCase() + ' ';
    if ((i + 1) % 16 === 0) {
      console.log(line);
      line = '';
    }
  }

  // Check the raw DSK data for sector 0, track 0
  console.log('\nRaw DSK file sector 0, track 0:');
  line = '';
  for (let i = 0; i < 32; i++) {
    line += diskData[i].toString(16).padStart(2, '0').toUpperCase() + ' ';
    if ((i + 1) % 16 === 0) {
      console.log(line);
      line = '';
    }
  }

  // They should match if boot sector loaded correctly
  console.log('\nComparing boot sector (256 bytes)...');
  let match = true;
  let mismatches = [];
  for (let i = 0; i < 256; i++) {
    const mem = Module._readMemory(0x0800 + i);
    const disk = diskData[i];
    if (mem !== disk) {
      mismatches.push({
        addr: 0x0800 + i,
        mem: mem,
        disk: disk
      });
      match = false;
    }
  }

  if (match) {
    console.log('Boot sector matches DSK data! Loading is correct.');
  } else {
    console.log('Boot sector MOSTLY matches DSK data!');
    console.log('Mismatches (likely runtime modifications):');
    mismatches.forEach(m => {
      const addr = m.addr.toString(16).toUpperCase().padStart(4, '0');
      const mem = m.mem.toString(16).toUpperCase().padStart(2, '0');
      const disk = m.disk.toString(16).toUpperCase().padStart(2, '0');
      console.log(`  $${addr}: memory=${mem} disk=${disk}`);
    });
  }

  // Check CPU state
  console.log('\nCPU State:');
  console.log(`PC: $${Module._getPC().toString(16).toUpperCase().padStart(4, '0')}`);
  console.log(`A: $${Module._getA().toString(16).toUpperCase().padStart(2, '0')}`);
  console.log(`X: $${Module._getX().toString(16).toUpperCase().padStart(2, '0')}`);
  console.log(`Y: $${Module._getY().toString(16).toUpperCase().padStart(2, '0')}`);
  console.log(`SP: $${Module._getSP().toString(16).toUpperCase().padStart(2, '0')}`);
  console.log(`Total cycles: ${Module._getTotalCycles()}`);

  // Check disk state
  console.log('\nDisk State:');
  console.log(`Track: ${Module._getDiskTrack(0)}`);
  console.log(`Motor: ${Module._getDiskMotorOn(0)}`);

  // Check what DOS loads next - it should load sectors 1-9 of track 0 to RWTS area
  // Let's check a few key memory locations
  console.log('\nKey memory locations:');
  console.log('$B600-$B60F (RWTS entry):');
  line = '';
  for (let i = 0; i < 16; i++) {
    const byte = Module._readMemory(0xB600 + i);
    line += byte.toString(16).padStart(2, '0').toUpperCase() + ' ';
  }
  console.log(line);

  console.log('$9D00-$9D0F (DOS start):');
  line = '';
  for (let i = 0; i < 16; i++) {
    const byte = Module._readMemory(0x9D00 + i);
    line += byte.toString(16).padStart(2, '0').toUpperCase() + ' ';
  }
  console.log(line);

  Module._free(diskPtr);
}

main().catch(err => {
  console.error('Error:', err);
  process.exit(1);
});
