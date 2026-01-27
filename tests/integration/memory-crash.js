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

  console.log('=== MEMORY STATE TEST ===\n');

  // Check ROM at startup
  console.log('At startup:');
  console.log('  $FE89 = $' + Module._readMemory(0xFE89).toString(16).toUpperCase());
  console.log('  $FE8A = $' + Module._readMemory(0xFE8A).toString(16).toUpperCase());
  console.log('  $FE8B = $' + Module._readMemory(0xFE8B).toString(16).toUpperCase());
  console.log('  Soft switches: 0x' + Module._getSoftSwitchState().toString(16).toUpperCase());

  // Run to near crash point
  console.log('\nRunning to 4,050,000 cycles...');
  while (Module._getTotalCycles() < 4050000) {
    Module._runCycles(1000);
  }

  // Check memory state just before crash
  console.log('\nAt cycle ' + Module._getTotalCycles() + ':');
  console.log('  $FE89 = $' + Module._readMemory(0xFE89).toString(16).toUpperCase());
  console.log('  $FE8A = $' + Module._readMemory(0xFE8A).toString(16).toUpperCase());
  console.log('  $FE8B = $' + Module._readMemory(0xFE8B).toString(16).toUpperCase());
  console.log('  Soft switches: 0x' + Module._getSoftSwitchState().toString(16).toUpperCase());

  // Step until crash
  for (let i = 0; i < 500000; i++) {
    const pc = Module._getPC();
    
    // Check memory periodically
    if (i % 10000 === 0) {
      const fe89 = Module._readMemory(0xFE89);
      if (fe89 !== 0xA9) {
        console.log('\n*** $FE89 changed from $A9 to $' + fe89.toString(16).toUpperCase() + ' at cycle ' + Module._getTotalCycles() + ' ***');
        console.log('  PC = $' + pc.toString(16).toUpperCase());
        console.log('  Soft switches: 0x' + Module._getSoftSwitchState().toString(16).toUpperCase());
      }
    }
    
    Module._stepInstruction();
    
    const newPC = Module._getPC();
    if (newPC === 0) {
      console.log('\n*** CRASH at cycle ' + Module._getTotalCycles() + ' ***');
      console.log('  Last PC was $' + pc.toString(16).toUpperCase());
      console.log('  $FE89 at crash: $' + Module._readMemory(0xFE89).toString(16).toUpperCase());
      console.log('  Soft switches at crash: 0x' + Module._getSoftSwitchState().toString(16).toUpperCase());
      
      // Check what JSR target would be
      if (pc >= 0x3740 && pc <= 0x3750) {
        console.log('  Memory at $3744-$3746: $' + 
          Module._readMemory(0x3744).toString(16).padStart(2,'0') + ' ' +
          Module._readMemory(0x3745).toString(16).padStart(2,'0') + ' ' +
          Module._readMemory(0x3746).toString(16).padStart(2,'0'));
      }
      break;
    }
  }

  Module._free(diskPtr);
}

runTest().catch(err => { console.error(err); process.exit(1); });
