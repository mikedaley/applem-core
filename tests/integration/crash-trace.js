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
  console.log('Loading...');
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

  // Run to earlier point
  console.log('Running to 4,050,000 cycles...');
  while (Module._getTotalCycles() < 4050000) {
    Module._runCycles(1000);
  }
  console.log('At cycle ' + Module._getTotalCycles());

  // Capture last 200 instructions before crash
  const history = [];
  
  for (let i = 0; i < 500000; i++) {
    const pc = Module._getPC();
    const sp = Module._getSP();
    const a = Module._getA();
    const x = Module._getX();
    const y = Module._getY();
    const op = Module._readMemory(pc);
    
    history.push({pc, sp, a, x, y, op});
    if (history.length > 200) history.shift();
    
    Module._stepInstruction();
    
    const newPC = Module._getPC();
    if (newPC === 0) {
      console.log('\n*** CRASH at cycle ' + Module._getTotalCycles() + ' ***');
      console.log('Last 100 instructions:');
      for (let j = Math.max(0, history.length - 100); j < history.length; j++) {
        const h = history[j];
        console.log('$' + h.pc.toString(16).toUpperCase().padStart(4,'0') + 
                    ' [' + h.op.toString(16).padStart(2,'0') + ']' +
                    ' A=' + h.a.toString(16).padStart(2,'0') +
                    ' X=' + h.x.toString(16).padStart(2,'0') +
                    ' Y=' + h.y.toString(16).padStart(2,'0') +
                    ' SP=' + h.sp.toString(16).padStart(2,'0'));
      }
      
      console.log('\nStack ($01C0-$01FF):');
      for (let row = 0; row < 4; row++) {
        let line = '$' + (0x01C0 + row*16).toString(16).toUpperCase() + ': ';
        for (let col = 0; col < 16; col++) {
          line += Module._readMemory(0x01C0 + row*16 + col).toString(16).padStart(2,'0') + ' ';
        }
        console.log(line);
      }
      break;
    }
  }

  Module._free(diskPtr);
}

runTest().catch(err => { console.error(err); process.exit(1); });
